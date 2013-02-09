#include <string>
#include <iostream>

#include "builtin_syntax.hh"
#include "lisp_ptr.hh"
#include "vm.hh"
#include "eval.hh"
#include "builtin_util.hh"
#include "zs_error.hh"
#include "procedure.hh"
#include "printer.hh"
#include "delay.hh"
#include "cons_util.hh"
#include "s_closure.hh"
#include "s_rules.hh"
#include "builtin.hh"

using namespace std;
using namespace ProcFlag;

static
Lisp_ptr whole_function_error(const char* opname){
  ZsArgs wargs{1};
  throw zs_error("eval error: '%s' -- cannot be used as operator!!\n",
                 opname);
}


Lisp_ptr syntax_quote(){
  ZsArgs args{1};

  if(args[0].tag() == Ptr_tag::syntactic_closure){
    return args[0].get<SyntacticClosure*>()->expr();
  }else{
    return args[0];
  }
}


static Lisp_ptr lambda_internal(Lisp_ptr args, Lisp_ptr code){
  auto arg_info = parse_func_arg(args);

  if(arg_info.first < 0){
    throw zs_error("eval error: lambda has invalid args!\n");
  }
  if(!code){
    throw zs_error("eval error: lambda has invalid body!\n");
  }
  
  return new IProcedure(code, 
                        {arg_info.first, arg_info.second},
                        args, vm.frame());
}

Lisp_ptr syntax_lambda(){
  ZsArgs wargs{1};

  return bind_cons_list_strict
    (wargs[0],
     [](Lisp_ptr, Lisp_ptr args, ConsIter code){
      return lambda_internal(args, code.base());
    });
}

Lisp_ptr syntax_if(){
  ZsArgs args;

  assert(args.size() == 2 || args.size() == 3);

  Lisp_ptr test = args[0];
  Lisp_ptr conseq = args[1];
  Lisp_ptr alt = (args.size() == 3) ? args[2] : Lisp_ptr();
    
  vm.return_value = {alt, conseq, vm_op_if, test};
  return {};
}

Lisp_ptr syntax_set(){
  ZsArgs args{2};
  vm.return_value = {args[0], vm_op_set, args[1]};
  return {};
}

Lisp_ptr syntax_define(){
  ZsArgs args{1};

  auto p = args[0].get<Cons*>()->cdr();
  Cons* rest = p.get<Cons*>();

  // extracting
  auto first = rest->car();

  if(identifierp(first)){
    bind_cons_list_strict
      (p,
       [](Lisp_ptr var, Lisp_ptr expr){
        vm.code.insert(vm.code.end(), {var, vm_op_local_set, expr});
      });
    return {};
  }else if(first.tag() == Ptr_tag::cons){
    Lisp_ptr code = rest->cdr();
    bind_cons_list_strict
      (first,
       [&](Lisp_ptr var, ConsIter l_args){
        auto value = lambda_internal(l_args.base(), code);
        vm.code.insert(vm.code.end(), {var, vm_op_local_set, value});
      });
    return {};
  }else{
    throw zs_error("eval error: informal define syntax!\n");
  }
}

Lisp_ptr syntax_begin(){
  ZsArgs wargs{1};

  auto body = nthcdr_cons_list<1>(wargs[0]);
  if(!body || nullp(body)){
    throw zs_error("eval error: begin has no exprs.\n");
  }
  
  vm.return_value = {body, vm_op_begin};
  return {};
}

Lisp_ptr syntax_let(){
  return let_internal(Entering::at_jump);
}

static
Lisp_ptr let_star_expand(Lisp_ptr bindings, Lisp_ptr body){
  if(nullp(bindings)){
    return new Cons(find_builtin_nproc("begin"), body);
  }else{
    return bind_cons_list_strict
      (bindings,
       [&](Lisp_ptr b_first, ConsIter b_rest) -> Lisp_ptr {
        return make_cons_list({find_builtin_nproc("let"),
                               make_cons_list({b_first}),
                               let_star_expand(b_rest.base(), body)});
      });
  }
}

Lisp_ptr syntax_let_star(){
  Lisp_ptr bindings, body;

  {
    ZsArgs wargs{1};

    bind_cons_list_strict
      (wargs[0],
       [&](Lisp_ptr, Lisp_ptr bindings1, ConsIter body1){
        bindings = bindings1;
        body = body1.base();
      });
  }

  vm.stack.insert(vm.stack.end(),
                  {let_star_expand(bindings, body), {Ptr_tag::vm_argcount, 1}});

  return let_internal(Entering::at_jump);
}

Lisp_ptr syntax_letrec(){
  // This heavyly depends on the implementation.
  //
  // normal let / funtion call
  //   1. evaluate args
  //      when a lambda form occurs, captures outer environment.
  //   2. enter a new environment.
  //   3. set args to the environment.
  // 
  // early bind
  //   1. enter a new environment.
  //   2. evaluate args
  //      when a lambda form occurs, captures the environment.
  //      so these lambdas refer the same environment.
  //   3. set args to the environment.
  return let_internal(Entering::at_bind);
}

Lisp_ptr syntax_do(){
  ZsArgs wargs{1};

  Lisp_ptr vars, end_test, end_exprs,  commands;

  bind_cons_list_strict
    (wargs[0],
     [&](Lisp_ptr, Lisp_ptr vars1, Lisp_ptr ends, ConsIter commands1) -> void{
      vars = vars1;

      auto end_c = ends.get<Cons*>();
      end_test = end_c->car();
      end_exprs = end_c->cdr();

      commands = commands1.base();
    });

  // TODO: collect this by garbage collector!
  auto loop_sym = new Symbol(new string("do_loop_symbol"));

  // extract vars
  GrowList init_binds;
  GrowList steps;

  for(auto p : vars){
    bind_cons_list_loose
      (p,
       [&](Lisp_ptr v, Lisp_ptr i, Lisp_ptr s) -> void{
        if(!s) s = v;

        init_binds.push(make_cons_list({v, i}));
        steps.push(s);
      });
  }

  // creates loop body
  GrowList gw;

  gw.push(find_builtin_nproc("begin"));
  for(auto p : commands){
    gw.push(p);
  }
  gw.push(push_cons_list(loop_sym, steps.extract()));

  // creates 'named let' style loop
  return
    make_cons_list({
        find_builtin_nproc("let"),
        loop_sym,
        init_binds.extract(),
        make_cons_list({
            find_builtin_nproc("if"),
            end_test,
            push_cons_list(find_builtin_nproc("begin"), end_exprs),
            gw.extract(),
            })
        });
}

Lisp_ptr syntax_delay(){
  ZsArgs wargs{1};
  return {new Delay(wargs[0], vm.frame())};
}

Lisp_ptr syntax_quasiquote(){
  ZsArgs wargs{1};

  auto arg = nth_cons_list<1>(wargs[0]);

  if(arg.tag() != Ptr_tag::cons && arg.tag() != Ptr_tag::vector){
    // acting as a normal quote.
    return make_cons_list({find_builtin_nproc("quote"), arg});
  }


  const auto quasiquote_fun = find_builtin_nproc("quasiquote");
  const auto unquote_fun = find_builtin_nproc("unquote");
  const auto unquote_splicing_fun = find_builtin_nproc("unquote-splicing");

  GrowList gl;

  const auto qq_elem = [&](Lisp_ptr p){
    gl.push(make_cons_list({quasiquote_fun, p}));
  };

  if(arg.tag() == Ptr_tag::cons){
    if(nullp(arg)){
      return Cons::NIL;
    }

    // check unquote -- like `,x
    auto first_val = vm.frame()->find(nth_cons_list<0>(arg));
    if(eq_internal(first_val, unquote_fun)
       || eq_internal(first_val, unquote_splicing_fun)){
      return arg;
    }

    // generic lists
    do_list(arg,
            [&](Cons* cell) -> bool {
              qq_elem(cell->car());
              return true;
            },
            [&](Lisp_ptr last){
              qq_elem(last);
            });

    return push_cons_list(find_builtin_nproc("list*"), gl.extract());
  }else if(arg.tag() == Ptr_tag::vector){
    auto v = arg.get<Vector*>();
    for(auto i = begin(*v); i != end(*v); ++i){
      qq_elem(*i);
    }

    return push_cons_list(find_builtin_nproc("vector"), gl.extract());
  }else{
    UNEXP_DEFAULT();
  }
}

Lisp_ptr syntax_unquote(){
  ZsArgs args{1};
  return args[0];
}

Lisp_ptr syntax_unquote_splicing(){
  ZsArgs args{1};
  if(args[0].tag() != Ptr_tag::cons){
    builtin_type_check_failed("unquote-splicing", Ptr_tag::cons, args[0]);
  }

  vm.return_value.assign(begin(args[0]), end(args[0]));
  return {};
}

Lisp_ptr syntax_else(){
  return whole_function_error("else");
}

Lisp_ptr syntax_arrow(){
  return whole_function_error("=>");
}

Lisp_ptr syntax_define_syntax(){
  ZsArgs args{2};

  if(!identifierp(args[0])){
    throw builtin_identifier_check_failed("define-syntax", args[0]);
  }

  // TODO: check args[1] is a transformer.
  vm.code.insert(vm.code.end(), {args[0], vm_op_local_set, args[1]});
  return Lisp_ptr{true};
}

Lisp_ptr syntax_let_syntax(){
  // TODO: check each arg is a transformer.
  return let_internal(Entering::at_jump);
}

Lisp_ptr syntax_letrec_syntax(){
  // TODO: check each arg is a transformer.
  return let_internal(Entering::at_bind);
}

Lisp_ptr syntax_syntax_rules(){
  ZsArgs args{2};

  auto env = args[1].get<Env*>();
  if(!env){
    throw builtin_type_check_failed("syntax-rules", Ptr_tag::env, args[1]);
  }

  return bind_cons_list_strict
    (args[0],
     [&](Lisp_ptr, Lisp_ptr lits, ConsIter rest){
      return new SyntaxRules(env, lits, rest.base());
    });
}
    
