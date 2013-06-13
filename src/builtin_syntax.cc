#include <string>

#include "builtin_syntax.hh"
#include "lisp_ptr.hh"
#include "vm.hh"
#include "eval.hh"
#include "zs_error.hh"
#include "procedure.hh"
#include "printer.hh"
#include "delay.hh"
#include "cons_util.hh"
#include "s_closure.hh"
#include "s_rules.hh"
#include "builtin.hh"
#include "zs_memory.hh"

using namespace std;
using namespace proc_flag;

namespace {

Lisp_ptr lambda_internal(Lisp_ptr args, Lisp_ptr code, Lisp_ptr name){
  auto arg_info = parse_func_arg(args);

  if(arg_info.first < 0){
    throw zs_error_arg1(nullptr, "invalid args!", {args});
  }

  if(!code){
    throw zs_error_arg1(nullptr, "invalid body!");
  }

  if(nullp(code)){
    throw zs_error_arg1(nullptr, "has no exprs.");
  }

  return zs_new<IProcedure>(code, 
                            ProcInfo{arg_info.first, arg_info.second},
                            args, vm.frame, name);
}

} // namespace

namespace builtin {

Lisp_ptr syntax_quote(ZsArgs args){
  if(args[0].tag() == Ptr_tag::syntactic_closure){
    return args[0].get<SyntacticClosure*>()->expr();
  }else{
    return args[0];
  }
}

Lisp_ptr syntax_lambda(ZsArgs wargs){
  auto args = nth_cons_list<1>(wargs[0]);
  auto body = nthcdr_cons_list<2>(wargs[0]);

  return lambda_internal(args, body, {});
}

Lisp_ptr syntax_if(ZsArgs args){
  assert(args.size() == 2 || args.size() == 3);

  Lisp_ptr test = args[0];
  Lisp_ptr conseq = args[1];
  Lisp_ptr alt = (args.size() == 3) ? args[2] : Lisp_ptr();
    
  vm.return_value = {alt, conseq, vm_op_if, test};
  return {};
}

Lisp_ptr syntax_set(ZsArgs args){
  vm.return_value = {args[0], vm_op_set, args[1]};
  return {};
}

Lisp_ptr syntax_define(ZsArgs args){
  auto i1 = nth_cons_list<1>(args[0]);

  // extracting
  if(identifierp(i1)){         // i1 points variable's name.
    auto expr_cons = nthcdr_cons_list<2>(args[0]);

    assert(expr_cons.get<Cons*>());
    if(!nullp(cdr(expr_cons.get<Cons*>()))){
      throw zs_error_arg1(nullptr, "informal syntax: too long");
    }

    vm.code.insert(vm.code.end(), {i1, vm_op_local_set, car(expr_cons.get<Cons*>())});
    return {};
  }else if(i1.tag() == Ptr_tag::cons){ // i1 points (funcname . arg-list)
    auto funcname = nth_cons_list<0>(i1);
    auto arg_list = nthcdr_cons_list<1>(i1);
    auto code = nthcdr_cons_list<2>(args[0]);

    auto value = lambda_internal(arg_list, code, funcname);
    vm.code.insert(vm.code.end(), {funcname, vm_op_local_set, value});
    return {};
  }else{
    throw zs_error_arg1(nullptr, "informal syntax!");
  }
}

Lisp_ptr syntax_delay(ZsArgs args){
  return {zs_new<Delay>(args[0], vm.frame)};
}

Lisp_ptr syntax_quasiquote(ZsArgs args){
  auto arg = nth_cons_list<1>(args[0]);

  if(arg.tag() != Ptr_tag::cons && arg.tag() != Ptr_tag::vector){
    // acting as a normal quote.
    return make_cons_list({intern(*vm.symtable, "quote"), arg});
  }

  const auto quasiquote_sym = intern(*vm.symtable, "quasiquote");
  const auto unquote_sym = intern(*vm.symtable, "unquote");
  const auto unquote_splicing_sym = intern(*vm.symtable, "unquote-splicing");

  GrowList gl;

  const auto qq_elem = [&](Lisp_ptr p){
    gl.push(make_cons_list({quasiquote_sym, p}));
  };

  if(arg.tag() == Ptr_tag::cons){
    if(nullp(arg)){
      return Cons::NIL;
    }

    // check unquote -- like `,x
    auto first_sym = nth_cons_list<0>(arg).get<Symbol*>();
    if(first_sym == unquote_sym
       || first_sym == unquote_splicing_sym){
      return arg;
    }

    // generic lists
    auto i = begin(arg);
    for(; i; ++i){
      qq_elem(*i);
    }
    qq_elem(i.base());

    return push_cons_list(find_builtin_nproc("list*"), gl.extract());
  }else if(arg.tag() == Ptr_tag::vector){
    auto v = arg.get<Vector*>();
    for(auto p : *v){
      qq_elem(p);
    }

    return push_cons_list(find_builtin_nproc("vector"), gl.extract());
  }else{
    UNEXP_DEFAULT();
  }
}

Lisp_ptr syntax_unquote_splicing(ZsArgs args){
  if(args[0].tag() != Ptr_tag::cons){
    throw builtin_type_check_failed(nullptr, Ptr_tag::cons, args[0]);
  }

  vm.return_value.assign(begin(args[0]), end(args[0]));
  return {};
}

Lisp_ptr syntax_syntax_rules(ZsArgs args){
  auto env = args[1].get<Env*>();
  if(!env){
    throw builtin_type_check_failed(nullptr, Ptr_tag::env, args[1]);
  }

  auto literals = nth_cons_list<1>(args[0]);
  auto rest = nthcdr_cons_list<2>(args[0]);

  return zs_new<SyntaxRules>(env, literals, rest);
}
    
Lisp_ptr syntax_internal_memv(ZsArgs args){
  if(args[1].tag() != Ptr_tag::cons){
    throw builtin_type_check_failed(nullptr, Ptr_tag::cons, args[1]);
  }

  for(auto i = begin(args[1]), e = end(args[1]); i != e; ++i){
    if(eqv_internal(args[0], *i))
      return i.base();
  }

  return Lisp_ptr{false};
}

} // namespace builtin
