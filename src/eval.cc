#include <cstdio>
#include <memory>
#include <cassert>

#include "vm.hh"
#include "eval.hh"
#include "util.hh"
#include "symbol.hh"
#include "cons.hh"
#include "procedure.hh"
#include "printer.hh"
#include "builtin_util.hh"
#include "delay.hh"

using namespace std;
using namespace Procedure;

namespace {

void vm_op_proc_enter();


void vm_op_arg_push();
void vm_op_arg_push_and_set();

/*
  ret = some value
  ----
  1. ret を stack に移す
     is_set == true なら ret を set!
  2. argcount++
  3. (car code) を stack top に
  4. (cdr code) が nil でなければ、次の展開を仕込む
*/
void vm_args_push_base(bool is_set){
  auto ret = vm.return_value[0];
  auto args = vm.code.back();
  auto& argc = vm.code[vm.code.size() - 2];

  auto args_c = args.get<Cons*>();
  auto arg1 = args_c->car();
  auto args_rest = args_c->cdr();

  vm.stack.push_back(ret);
  argc = Lisp_ptr{Ptr_tag::vm_argcount, argc.get<int>() + 1};

  if(is_set){
    auto& arg_syms = vm.code[vm.code.size() - 3];
    vm.local_set(arg_syms.get<Cons*>()->car().get<Symbol*>(), ret);
    arg_syms = arg_syms.get<Cons*>()->cdr();
  }

  if(args_c){
    if(nullp(args_rest)){
      vm.code.back() = arg1;
    }else{
      vm.code.back() = args_rest;
      vm.code.push_back((is_set) ? vm_op_arg_push_and_set : vm_op_arg_push);
      vm.code.push_back(arg1);
    }
  }else{
    if(is_set){
      vm.code.pop_back();
    }
  }
}

void vm_op_arg_push(){
  vm_args_push_base(false);
}

void vm_op_arg_push_and_set(){
  vm_args_push_base(true);
}


static
bool check_argcount(const char* name, int argc, const ProcInfo* info){ 
  if((argc < info->required_args)
     || (!info->variadic && argc > info->required_args)){
    fprintf(zs::err,
            "%s error: number of passed args is mismatched!!"
            " (required %d args, %s, passed %d)\n",
            name, 
            info->required_args,
            (info->variadic) ? "variadic" : "not variadic",
            argc);
    return false;
  }
  return true;
}

/*
  stack[0] = whole args
  ----
  * normal call
    code = (argN, arg-move, argN-1, arg-move, ..., call kind, proc)
    stack = (arg-bottom)

  * sequencial call (let*)
    code = (argN, arg-set-move, symN, argN-1, arg-set-move, symN-1, ..., call kind, proc)
    stack = (arg-bottom)
*/
void function_call(Lisp_ptr proc, const ProcInfo* info, Lisp_ptr arg_head){
  auto args = vm.stack.back();
  vm.stack.pop_back();

  if(info->early_bind){
    auto iproc = proc.get<IProcedure*>();
    assert(iproc);

    if(auto closure = iproc->closure()){
      vm.enter_frame(closure->push());
    }else{
      vm.enter_frame(vm.frame()->push());
    }
  }

  vm.code.push_back(proc);
  vm.code.push_back(vm_op_proc_enter);

  Lisp_ptr arg1, arg_rest;

  bind_cons_list(args,
                 [](Cons*){},
                 [&](Cons* c){
                   arg1 = c->car();
                   arg_rest = c->cdr();
                 });

  if(!arg1 || nullp(arg1)){
    vm.stack.push_back({Ptr_tag::vm_argcount, 0});
  }else if(nullp(arg_rest)){
    vm.code.push_back({Ptr_tag::vm_argcount, 1});
    // if(info->sequencial){
    //   vm.code.push_back(arg_head);
    //   vm.code.push_back(arg_rest);
    //   vm.code.push_back(vm_op_arg_push_and_set);
    // }else{
    //   vm.code.push_back(arg_rest);
    //   vm.code.push_back(vm_op_arg_push);
    // }
    vm.code.push_back(arg1);
  }else{
    vm.code.push_back({Ptr_tag::vm_argcount, 1});
    if(info->sequencial){
      vm.code.push_back(arg_head);
      vm.code.push_back(arg_rest);
      vm.code.push_back(vm_op_arg_push_and_set);
    }else{
      vm.code.push_back(arg_rest);
      vm.code.push_back(vm_op_arg_push);
    }
    vm.code.push_back(arg1);
  }
}

/*
  ret = expanded proc
  ----
  code = proc
*/
void vm_op_macro_call(){
  vm.code.push_back(vm.return_value[0]);
}  

/*
  stack[0] = whole args
  ----
  code = (call kind, proc, macro call)
  stack = (arg1, arg2, ..., arg-bottom)
*/
void macro_call(Lisp_ptr proc, const ProcInfo* info){
  auto args = vm.stack.back();
  vm.stack.pop_back();

  auto argc = list_to_stack("macro-call", args.get<Cons*>()->cdr(), vm.stack);
  if(!check_argcount("macro-call", argc, info)){
    for(int i = 0; i < argc; ++i){
      vm.stack.pop_back();
    }
    vm.return_value[0] = {};
    return;
  }    
  vm.stack.push_back({Ptr_tag::vm_argcount, argc});

  vm.code.push_back(vm_op_macro_call);
  vm.code.push_back(proc);
  vm.code.push_back(vm_op_proc_enter);
}

/*
  stack[0] = whole args
  ----
  code = (call kind, proc)
  stack = (whole args, arg-bottom)
*/
void whole_function_call(Lisp_ptr proc){
  vm.code.push_back(proc);
  vm.code.push_back(vm_op_proc_enter);

  //vm.stack.push_back(args);
  vm.stack.push_back({Ptr_tag::vm_argcount, 1});
}

/*
  stack[0] = whole args
  ----
  code = (call kind, proc, macro call)
  stack = (whole args, arg-bottom)
*/
void whole_macro_call(Lisp_ptr proc){
  vm.code.push_back(vm_op_macro_call);
  whole_function_call(proc);
}

/*
  ret = proc
  stack[0] = args
  ---
  goto proc_call or macro_call
*/
void vm_op_call(){
  auto proc = vm.return_value[0];

  if(!is_procedure(proc)){
    fprintf(zs::err, "eval error: (# # ...)'s first element is not procedure (got: %s)\n",
            stringify(proc.tag()));
    fprintf(zs::err, "      expr: "); print(zs::err, vm.stack.back()); fputc('\n', zs::err);
    
    vm.return_value[0] = {};
    vm.stack.pop_back();
    return;
  }

  const ProcInfo* info = get_procinfo(proc);
  Lisp_ptr args = get_arg_list(proc);

  switch(info->calling){
  case Calling::function:
    return function_call(proc, info, args);
  case Calling::macro:
    return macro_call(proc, info);
  case Calling::whole_function:
    return whole_function_call(proc);
  case Calling::whole_macro:
    return whole_macro_call(proc);
  default:
    UNEXP_DEFAULT();
  }
}

/*
  stack = (arg1, arg2, ..., arg-bottom)
  ----
  ret = returned value
  code = ()
  stack = ()
*/
void proc_enter_native(const NProcedure* fun){
  auto native_func = fun->get();
  assert(native_func);

  native_func();
  // if(!vm.return_value())
  //   fprintf(zs::err, "eval warning: native func returned undef!\n");
}

/*
  stack = (arg1, arg2, ..., arg-bottom)
  ----
  In new frame, args are bound.
  code = (body1, body2, ..., leave_frame)
  stack = ()
*/
void proc_enter_interpreted(IProcedure* fun){
  const auto& argi = fun->info();

  // tail call check
  if(!vm.code.empty()
     && vm.code.back().get<VMop>() == vm_op_leave_frame){
    vm.code.pop_back();
    vm.leave_frame();
  }

  if(!fun->info()->early_bind){
    if(auto closure = fun->closure()){
      vm.enter_frame(closure->push());
    }else{
      vm.enter_frame(vm.frame()->push());
    }
  }

  vm.code.push_back(vm_op_leave_frame);

  Lisp_ptr arg_name = fun->arg_list();
  Lisp_ptr argc = vm.stack.back();
  vm.stack.pop_back();
  int i;

  // normal arg push
  for(i = 0; i < argc.get<int>(); ++i){
    if(vm.stack.empty()){
      fprintf(zs::err, "eval internal error: no args and no managed funcall!\n");
      vm.return_value[0] = {};
      return;
    }

    auto a = vm.stack.back();
    vm.stack.pop_back();

    auto arg_name_cell = arg_name.get<Cons*>();
    if(!arg_name_cell){
      break;
    }
   
    vm.local_set(arg_name_cell->car().get<Symbol*>(), a);
    arg_name = arg_name_cell->cdr();
  }

  // variadic arg push
  if(argi->variadic){
    if(!arg_name.get<Symbol*>()){
      fprintf(zs::err, "eval error: no arg name for variadic arg!\n");
      vm.return_value[0] = {};
      return;
    }

    vm.stack.push_back({Ptr_tag::vm_argcount, argc.get<int>() - i});
    vm.local_set(arg_name.get<Symbol*>(), stack_to_list<false>(vm.stack));
  }else{  // clean stack
    if(i != argc.get<int>()){
      fprintf(zs::err, "eval error: corrupted stack -- no bottom found!\n");
      for(; i < argc.get<int>(); ++i){
        vm.stack.pop_back();
      }
      vm.return_value[0] = {};
      return;
    }
  }
  
  // set up lambda body code
  // list_to_stack("funcall", fun->get(), vm.code);
  vm.code.push_back(fun->get());
  vm_op_begin();
}

/*
  stack = (arg1, arg2, ..., arg-bottom)
  ----
  replaces VM!
*/
void proc_enter_cont(Continuation* c){
  std::vector<Lisp_ptr> tmp;
  stack_to_vector(vm.stack, tmp);

  vm = c->get();
  vm.return_value = move(tmp);
}

/*
  code = (proc)
  ----
  code = ()
   and goto handler.
*/
void vm_op_proc_enter(){
  auto proc = vm.code.back();
  vm.code.pop_back();

  assert(!vm.stack.empty());

  if(auto ifun = proc.get<IProcedure*>()){
    proc_enter_interpreted(ifun);
  }else if(auto nfun = proc.get<const NProcedure*>()){
    proc_enter_native(nfun);
  }else if(auto cont = proc.get<Continuation*>()){
    proc_enter_cont(cont);
  }else{
    fprintf(zs::err, "eval internal error: corrupted code stack -- no proc found for entering!\n");
    vm.return_value[0] = {};
    return;
  }
}
 
/*
  ret = (args)
  ----
  stack = (args)
  goto proc_enter 
*/
void vm_op_move_values(){
  int argc = 0;

  for(auto i = vm.return_value.rbegin(), e = vm.return_value.rend();
      i != e; ++i){
    vm.stack.push_back(*i);
    ++argc;
  }
  vm.stack.push_back({Ptr_tag::vm_argcount, argc});

  vm.return_value.resize(1);
}
 
/*
  ret = list
  ----
  stack = (list[0], list[1], ...)
*/
void vm_op_arg_push_list(){
  list_to_stack("unquote-splicing", vm.return_value[0], vm.stack);
}

static const VMop vm_op_quasiquote_list = procedure_list_star;
static const VMop vm_op_quasiquote_vector = procedure_vector;

} // namespace

/*
  leaves frame.
  no stack operations.
*/
void vm_op_leave_frame(){
  vm.leave_frame();
}  

/*
  stack = (consequent, alternative)
  ----
  stack = ()
  code = (consequent or alternative)
*/
void vm_op_if(){
  auto test_result = vm.return_value[0];

  if(test_result.get<bool>()){
    vm.code.push_back(vm.stack.back());
    vm.stack.pop_back();
    vm.stack.pop_back();
  }else{
    vm.stack.pop_back();
    vm.code.push_back(vm.stack.back());
    vm.stack.pop_back();
  }
}

/*
  stack = (variable name)
  ----
  stack = ()
  return-value is setted.
*/
void vm_op_set(){
  auto var = vm.stack.back().get<Symbol*>();
  vm.stack.pop_back();
  if(!var){
    fprintf(zs::err, "eval error: internal error occured (set!'s varname is dismissed)\n");
    return;
  }

  vm.set(var, vm.return_value[0]);
}

/*
  stack = (variable name)
  ----
  stack = ()
  return-value is setted.
*/
void vm_op_local_set(){
  auto var = vm.stack.back().get<Symbol*>();
  vm.stack.pop_back();
  if(!var){
    fprintf(zs::err, "eval error: internal error occured (set!'s varname is dismissed)\n");
    return;
  }

  vm.local_set(var, vm.return_value[0]);
}

/*
  code = [#1=(...)]
  ---
  (cdr #1) is not null.
    code = [(car #1) <vm-op-begin> (cdr #1)]
  (cdr #1) is null.
    code = [(car #1)]
 */
void vm_op_begin(){
  auto next = vm.code.back();
  auto next_c = next.get<Cons*>();
  auto next_car = next_c->car();
  auto next_cdr = next_c->cdr();

  if(!nullp(next_cdr)){
    vm.code.back() = next_cdr;
    vm.code.push_back(vm_op_begin);
    vm.code.push_back(next_car);
  }else{
    vm.code.back() = next_car;
  }
}

/*
  code[0] = template
  ----
  * vector, list
      code = (quasiquote, template[0], arg_push, # normal
              template[1], arg_push,             # unquote
              template[2], arg_push_list,        # unquote-splicing
              ...,
              stack_to_{list or vector}
              )
      stack[0] = arg_bottom
  * default
      return = template
*/
void vm_op_quasiquote(){
#if 0
  const auto unquote_sym = intern(vm.symtable(), "unquote");
  const auto unquote_splicing_sym = intern(vm.symtable(), "unquote-splicing");

  const auto qq_elem = [&](Lisp_ptr p){
    if(auto l = p.get<Cons*>()){
      if(auto l_first_sym = l->car().get<Symbol*>()){
        if(l_first_sym == unquote_sym){
          vm.code.push_back(vm_op_arg_push);
          vm.code.push_back(l->cdr().get<Cons*>()->car());
          return;
        }else if(l_first_sym  == unquote_splicing_sym){
          vm.code.push_back(vm_op_arg_push_list);
          vm.code.push_back(l->cdr().get<Cons*>()->car());
          return;
        }
      }
    }

    vm.code.push_back(vm_op_arg_push);
    vm.code.push_back(p);
    vm.code.push_back(vm_op_quasiquote);
  };

  auto p = vm.code.back();
  vm.code.pop_back();

  if(p.tag() == Ptr_tag::cons){
    if(nullp(p)){
      vm.return_value[0] = Cons::NIL;
      return;
    }

    // check unquote -- like `,x
    if(auto first_sym = p.get<Cons*>()->car().get<Symbol*>()){
      if(first_sym == unquote_sym){
        auto rest = p.get<Cons*>()->cdr().get<Cons*>()->car();
        vm.code.push_back(rest);
        return;
      }else if(first_sym == unquote_splicing_sym){
        fprintf(zs::err, "eval error: unquote-splicing is not supported out of list");
        vm.return_value[0] = {};
        return;
      }
    }

    // generic lists
    vm.stack.push_back(vm_op_arg_bottom);
    vm.code.push_back(vm_op_quasiquote_list);

    do_list(p,
            [&](Cons* c) -> bool {
              qq_elem(c->car());
              return true;
            },
            [&](Lisp_ptr last){
              qq_elem(last);
            });
  }else if(p.tag() == Ptr_tag::vector){
    vm.stack.push_back(vm_op_arg_bottom);
    vm.code.push_back(vm_op_quasiquote_vector);

    auto v = p.get<Vector*>();
    for(auto i = begin(*v); i != end(*v); ++i){
      qq_elem(*i);
    }
  }else{
    vm.return_value[0] = p;
  }
#endif
}

/*
  return  = forced value
  stack[0] = delay
*/
void vm_op_force(){
  auto delay = vm.stack.back().get<Delay*>();
  vm.stack.pop_back();

  if(!delay){
    fprintf(zs::err, "eval error: internal error occured ('force' found before not a delay\n");
    return;
  }

  delay->force(vm.return_value[0]);
}

void let_internal(Sequencial sequencial, EarlyBind early_bind){
#if 0
  auto arg = pick_args_1();
  if(!arg) return;

  // skips first 'let' symbol
  auto arg_c = arg.get<Cons*>();
  assert(arg_c);

  auto arg2 = arg_c->cdr();
  if(arg2.tag() != Ptr_tag::cons || nullp(arg2)){
    fprintf(zs::err, "eval error: informal LET syntax -- (LET . <%s>).\n",
            (nullp(arg2)) ? "nil" : stringify(arg2.tag()));
    vm.return_value[0] = {};
    return;
  }
  arg_c = arg2.get<Cons*>();

  // checks named let
  Lisp_ptr name = {};

  if(arg_c->car().tag() == Ptr_tag::symbol){
    name = arg_c->car();

    auto arg3 = arg_c->cdr();
    if(arg3.tag() != Ptr_tag::cons || nullp(arg3)){
      fprintf(zs::err, "eval error: informal LET syntax -- (LET <name> . <%s>).\n",
              (nullp(arg3)) ? "nil" : stringify(arg3.tag()));
      vm.return_value[0] = {};
      return;
    }
    
    arg_c = arg3.get<Cons*>();
  }
    
  // picks elements
  auto binds = arg_c->car();
  auto body = arg_c->cdr();

  if(body.tag() != Ptr_tag::cons || nullp(body)){
    fprintf(zs::err, "eval error: informal syntax for LET's body!.\n");
    vm.return_value[0] = {};
    return;
  }

  // parses binding list
  int len = 0;

  Cons* syms_c = new Cons;
  Lisp_ptr syms = syms_c;
  Cons* syms_c_p = syms_c;

  Cons* vals_c = new Cons;
  Lisp_ptr vals = vals_c;
  Cons* vals_c_p = vals_c;

  if(!do_list(binds,
              [&](Cons* cell) -> bool{
                auto bind = cell->car();
                if(bind.tag() != Ptr_tag::cons){
                  fprintf(zs::err, "eval error: informal object (%s) found in let binding.\n",
                          stringify(bind.tag()));
                  return false;
                }

                auto c = bind.get<Cons*>();
                if(!c){
                  fprintf(zs::err, "eval error: null found in let binding.\n");
                  return false;
                }
                 
                ++len;

                auto nsc = new Cons;
                syms_c->rplaca(c->car());
                syms_c->rplacd(nsc);
                syms_c_p = syms_c;
                syms_c = nsc;

                auto nvc = new Cons;
                vals_c->rplaca(c->cdr().get<Cons*>()->car());
                vals_c->rplacd(nvc);
                vals_c_p = vals_c;
                vals_c = nvc;

                return true;
              },
              [&](Lisp_ptr dot_cdr) -> bool{
                if(!nullp(dot_cdr)){
                  return false;
                }

                if(syms_c == syms_c_p){
                  assert(vals_c == vals_c_p);
                  delete syms_c;
                  delete vals_c;
                  syms = Cons::NIL;
                  vals = Cons::NIL;
                }else{
                  delete syms_c;
                  delete vals_c;
                  syms_c_p->rplacd(Cons::NIL);
                  vals_c_p->rplacd(Cons::NIL);
                }

                return true;
              })){
    fprintf(zs::err, "eval error: let binding was failed!\n");
    free_cons_list(syms);
    free_cons_list(vals);
    vm.return_value[0] = {};
    return;
  }

  if(name){
    vm.enter_frame(vm.frame()->push());
  }

  auto proc = new IProcedure(body, 
                             {Calling::function, len, Variadic::f, sequencial, early_bind},
                             syms, vm.frame());

  if(name){
    vm.local_set(name.get<Symbol*>(), proc);
    vm.code.push_back(vm_op_leave_frame);
  }

  vm.code.push_back(vm_op_call);
  vm.code.push_back(proc);
  vm.stack.push_back(push_cons_list({}, vals));
  vm.return_value[0] = {};
#endif
}

void eval(){
  while(!vm.code.empty()){
    // print(stdout, vm);
    // fflush(stdout);

    auto p = vm.code.back();
    vm.code.pop_back();

    switch(p.tag()){
    case Ptr_tag::undefined:
      fprintf(zs::err, "eval error: undefined is passed!\n");
      vm.return_value[0] = {};
      break;

    case Ptr_tag::symbol:
      vm.return_value[0] = vm.find(p.get<Symbol*>());
      break;
    
    case Ptr_tag::cons: {
      auto c = p.get<Cons*>();
      if(!c){
        vm.return_value[0] = Cons::NIL;
        break;
      }

      vm.code.push_back(vm_op_call);
      vm.code.push_back(c->car());
      vm.stack.push_back(p);
      break;
    }

    case Ptr_tag::vm_op:
      if(auto op = p.get<VMop>()){
        op();
      }
      break;

    case Ptr_tag::vm_argcount:
      vm.stack.push_back(vm.return_value[0]);
      vm.stack.push_back(p);
      break;

    case Ptr_tag::boolean: case Ptr_tag::character:
    case Ptr_tag::i_procedure: case Ptr_tag::n_procedure:
    case Ptr_tag::number:
    case Ptr_tag::string: case Ptr_tag::vector:
    case Ptr_tag::port: case Ptr_tag::env:
    case Ptr_tag::delay:
    case Ptr_tag::continuation:
    default:
      vm.return_value[0] = p;
      break;
    }
  }

  if(!vm.stack.empty()){
    fprintf(zs::err, "eval internal warning: VM stack is broken! (stack has values unexpectedly.)\n");
    do{
      vm.stack.pop_back();
    }while(!vm.stack.empty());
  }
}


void apply_func(){
#if 0
  std::vector<Lisp_ptr> args;
  stack_to_vector(vm.stack, args);

  if(!is_procedure(args[0])){
    fprintf(zs::err, "apply error: first arg is not procedure (%s)\n",
            stringify(args[0].tag()));
    vm.return_value[0] = {};
    return;
  }

  auto info = get_procinfo(args[0]);

  // simulating function_call()
  vm.code.push_back(args[0]);
  vm.code.push_back(vm_op_proc_enter);

  int argc = 0;

  for(auto i = 1u; i < args.size(); ++i){
    auto dl =
      do_list(args[i],
              [&](Cons* cell) -> bool{
                vm.code.push_back(vm_op_arg_push);
                vm.code.push_back(cell->car());
                ++argc;
                return true;
              },
              [&](Lisp_ptr dot_cdr) -> bool{
                if(!nullp(dot_cdr)){
                  fprintf(zs::err, "apply error: dot list passed.\n");
                  return false;
                }
                return true;
              });
    
    if(!dl || !check_argcount("apply", argc, info)){
      clean_funcall_stack();
      vm.return_value[0] = {};
      return;
    }
  }

  vm.stack.push_back(vm_op_arg_bottom);
#endif
}

void func_force(){
  auto arg = pick_args_1();
  auto d = arg.get<Delay*>();
  if(!d){
    vm.return_value[0] = arg;
    return;
  }
  
  if(d->forced()){
    vm.return_value[0] = d->get();
    return;
  }

  vm.stack.push_back(arg);
  vm.code.push_back(vm_op_force);
  vm.enter_frame(d->env());
  vm.code.push_back(vm_op_leave_frame);
  vm.code.push_back(d->get());
}

void call_with_values(){
#if 0
  auto args = pick_args<2>();

  if(!is_procedure(args[0])){
    fprintf(zs::err, "call-with-values error: first arg is not procedure (%s)\n",
            stringify(args[0].tag()));
    vm.return_value[0] = {};
    return;
  }

  auto info = get_procinfo(args[0]);
  if(info->required_args != 0){
    fprintf(zs::err, "call-with-values error: first arg takes 1 or more args (%d)\n",
            info->required_args);
    vm.return_value[0] = {};
    return;
  }    

  if(!is_procedure(args[1])){
    fprintf(zs::err, "call-with-values error: second arg is not procedure (%s)\n",
            stringify(args[1].tag()));
    vm.return_value[0] = {};
    return;
  }

  // second proc call
  vm.code.push_back(args[1]);
  vm.code.push_back(vm_op_proc_enter);
  vm.code.push_back(vm_op_move_values);

  // first proc, calling with zero args.
  vm.code.push_back(args[0]);
  vm.code.push_back(vm_op_proc_enter);
  vm.stack.push_back(vm_op_arg_bottom);
#endif
}

void call_cc(){
#if 0
  auto args = pick_args<1>();

  if(!is_procedure(args[0])){
    fprintf(zs::err, "call/cc error: first arg is not procedure (%s)\n",
            stringify(args[0].tag()));
    vm.return_value[0] = {};
    return;
  }

  auto info = get_procinfo(args[0]);
  if(info->required_args != 1){
    fprintf(zs::err, "call/cc error: first arg mush take 1 arg (%d)\n",
            info->required_args);
    vm.return_value[0] = {};
    return;
  }

  auto cont = new Continuation(vm);

  vm.code.push_back(args[0]);
  vm.code.push_back(vm_op_proc_enter);

  vm.stack.push_back(vm_op_arg_bottom);
  vm.stack.push_back(cont);
#endif
}

const char* stringify(VMop op){
  if(op == vm_op_nop){
    return "NOP / arg bottom";
  }else if(op == vm_op_proc_enter){
    return "proc enter";
  }else if(op == vm_op_arg_push){
    return "arg push";
  }else if(op == vm_op_arg_push_and_set){
    return "arg push and set";
  }else if(op == vm_op_macro_call){
    return "macro call";
  }else if(op == vm_op_call){
    return "call";
  }else if(op == vm_op_leave_frame){
    return "leave frame";
  }else if(op == vm_op_begin){
    return "begin";
  }else if(op == vm_op_proc_enter){
    return "proc enter";
  }else if(op == vm_op_move_values){
    return "move values";
  }else if(op == vm_op_arg_push_list){
    return "arg push list";
  }else if(op == vm_op_quasiquote_list){
    return "quasiquote list";
  }else if(op == vm_op_quasiquote_vector){
    return "quasiquote vector";
  }else if(op == vm_op_if){
    return "if";
  }else if(op == vm_op_set){
    return "set";
  }else if(op == vm_op_local_set){
    return "local set";
  }else if(op == vm_op_quasiquote){
    return "quasiquote";
  }else if(op == vm_op_force){
    return "force";
  }else{
    return "unknown vm-op";
  }
}
