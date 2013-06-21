#include <cassert>
#include <iostream>
#include <functional>

#include "vm.hh"
#include "eval.hh"
#include "zs_error.hh"
#include "symbol.hh"
#include "cons.hh"
#include "cons_util.hh"
#include "procedure.hh"
#include "printer.hh"
#include "s_closure.hh"
#include "s_rules.hh"
#include "builtin.hh"

using namespace std;
using namespace proc_flag;

bool dump_mode = false;
unsigned instruction_counter = 0;
const unsigned gc_invoke_interval = 0x100u;

namespace {

void local_set_with_identifier(Env* e, Lisp_ptr ident, Lisp_ptr value){
  e->local_set(ident, value);
}

/*
  ret = some value
*/
void vm_op_arg_push(){
  auto ret = vm.return_value_1();
  auto& argc = vm.code[vm.code.size() - 1];
  auto& args = vm.code[vm.code.size() - 2];

  if(nullp(args)){
    vm.stack.push_back(argc);
    vm.code.erase(vm.code.end() - 2, vm.code.end());
  }else{
    // auto arg1 = nth_cons_list<0>(args); // the EXPR just evaled.
    auto args_rest = nthcdr_cons_list<1>(args);
    
    vm.stack.push_back(ret);

    args = args_rest;
    argc = {Ptr_tag::vm_argcount, argc.get<int>() + 1};
    vm.code.push_back(vm_op_arg_push);
    vm.code.push_back(nth_cons_list<0>(args_rest));
  }
}


void function_call(Lisp_ptr proc){
  auto args = vm.stack.back();
  vm.stack.pop_back();

  auto args_head = nthcdr_cons_list<1>(args); // skips first symbol

  vm.code.insert(vm.code.end(), {proc, vm_op_proc_enter,
        args_head, {Ptr_tag::vm_argcount, 0},
        vm_op_arg_push, nth_cons_list<0>(args_head)});
}

/*
  ret = expanded proc
  ----
  code = proc
*/
void vm_op_macro_call(){
  vm.code.insert(vm.code.end(),
                 vm.return_value.begin(), vm.return_value.end());
}  

/*
  return = an, an-1, ...
  code = (this), <VMop arg push>, argcount[b], args(b)
  stack = bn, bn-1, ...
  ---
  return = {}
  code = (this), <VMop arg push>, argcount[a+b], args(b)
  stack = bn, bn-1, ..., an, an-1, ...
*/
void vm_op_stack_splicing(){
  auto& op = vm.code[vm.code.size() - 1];
  auto& outer_argc = vm.code[vm.code.size() - 2];
  auto& outer_args = vm.code[vm.code.size() - 3];

  if(op.tag() != Ptr_tag::vm_op
     || outer_argc.tag() != Ptr_tag::vm_argcount){
    throw zs_error("eval internal error: stack-splicing operater is called in invalid context!\n");
  }

  // pushes return-value to vm.stack
  int argc = vm.return_value.size();
  vm.stack.insert(vm.stack.end(), vm.return_value.begin(), vm.return_value.end());

  // formatting vm.code to 'splicing is processed'
  // see vm_op_arg_push()
  auto outer_next_args = nthcdr_cons_list<1>(outer_args);
  auto outer_next_arg1 = nth_cons_list<0>(outer_next_args);

  outer_argc = {Ptr_tag::vm_argcount, outer_argc.get<int>() + argc};
  outer_args = outer_next_args;
  vm.code.push_back(outer_next_arg1);
}

/*
  stack[0] = whole args
  ----
  code = (call kind, proc, macro call)
  stack = (arg1, arg2, ..., arg-bottom)
*/
void macro_call(Lisp_ptr proc){
  auto args = vm.stack.back();
  vm.stack.pop_back();

  int argc = 0;
  for(auto p : nthcdr_cons_list<1>(args)){
    vm.stack.push_back(p);
    ++argc;
  }
  vm.stack.push_back({Ptr_tag::vm_argcount, argc});

  vm.code.insert(vm.code.end(), {proc, vm_op_proc_enter});
}

/*
  stack[0] = whole args
  ----
  code = (call kind, proc)
  stack = (whole args, arg-bottom)
*/
void whole_call(Lisp_ptr proc){
  vm.stack.push_back(vm.frame);
  vm.stack.push_back({Ptr_tag::vm_argcount, 2});
  vm.code.insert(vm.code.end(), {proc, vm_op_proc_enter});
}



void proc_enter_native(const NProcedure* fun){
  auto native_func = fun->get();
  assert(native_func);

  auto info = fun->info();
  assert(info);

  assert(vm.stack.back().tag() == Ptr_tag::vm_argcount);
  auto argc = vm.stack.back().get<int>();

  if(!((info->required_args <= argc) && (argc <= info->max_args))){
    throw builtin_argcount_failed(find_builtin_nproc_name(fun),
                                  info->required_args, info->max_args,
                                  argc);
  }

  try{
    ZsArgs args;
    auto p = native_func(move(args));

    if(info->move_ret == MoveReturnValue::t){
      vm.return_value = {p};
    }
  }catch(const zs_error& e){
    throw zs_error(printf_string("%s: %s",
                                 find_builtin_nproc_name(fun),
                                 e.what()));
  }
}

/*
  stack = (arg1, arg2, ..., <argcount>)
  ----
  In new frame, args are bound.
  code = (body1, body2, ..., leave_frame)
  stack = ()
*/
void proc_enter_interpreted(IProcedure* fun, const ProcInfo* info){
  // tail call check
  if(!vm.code.empty()
     && vm.code.back().get<VMop>() == vm_op_leave_frame){
    // cout << "tail call!" << endl;
    vm.code.pop_back();
    vm_op_leave_frame();
  }

  // == entering frame ==
  auto oldenv = vm.frame;
  if(auto closure = fun->closure()){
    vm.frame = closure->push();
  }else{
    vm.frame = vm.frame->push();
  }

  switch(fun->info()->leaving){
  case Leaving::immediate:
    vm.code.insert(vm.code.end(), {oldenv, vm_op_leave_frame});
    break;
  case Leaving::after_returning_op:
    assert(info->returning == Returning::code
           || info->returning == Returning::stack_splice);
    assert(vm.code.back().tag() == Ptr_tag::vm_op);
    vm.code.insert(prev(vm.code.end()), {oldenv, vm_op_leave_frame});
    break;
  default:
    UNEXP_DEFAULT();
  }

  // == processing args ==
  Lisp_ptr arg_name = fun->arg_list();
  Lisp_ptr argc = vm.stack.back();
  vm.stack.pop_back();

  auto arg_start = vm.stack.end() - argc.get<int>();
  auto arg_end = vm.stack.end();
  auto i = arg_start;

  // normal arg push
  for(; i != arg_end; ++i){
    auto arg_name_cell = arg_name.get<Cons*>();
    if(!arg_name_cell){
      break;
    }
   
    local_set_with_identifier(vm.frame, car(arg_name_cell), *i);
    arg_name = cdr(arg_name_cell);
  }

  // variadic arg push
  if(info->max_args > info->required_args){
    if(!identifierp(arg_name)){
      throw zs_error("eval error: no arg name for variadic arg!\n");
    }

    auto var_args = make_cons_list(i, arg_end);
    local_set_with_identifier(vm.frame, arg_name, var_args);
  }else{
    if(i != arg_end){
      throw zs_error("eval error: corrupted stack -- passed too much args!\n");
    }
  }

  // cleaning stack
  vm.stack.erase(arg_start, arg_end);

  // adds procedure's name
  if(fun->name()){
    local_set_with_identifier(vm.frame, fun->name(), fun);
  }

  // set up lambda body code
  vector<Lisp_ptr> tmpv;
  tmpv.assign(begin(fun->get()), end(fun->get()));

  vm.code.insert(vm.code.end(), tmpv.rbegin(), tmpv.rend());
}


static unsigned get_wind_index(const VM& va, const VM& vb){
  unsigned w = 0;
  while((w < va.extent.size())
        && (w < vb.extent.size())
        && eq_internal(va.extent[w].thunk, vb.extent[w].thunk)){
    ++w;
  }
  return w;
}

void vm_op_restore_values(){
  auto values_p = vm.code.back();
  auto values = values_p.get<Vector*>();
  assert(values);
  vm.code.pop_back();

  vm.return_value = std::move(*values);
  zs_delete(values);
}

void vm_op_replace_vm(){
  auto cont = vm.code.back();
  vm.code.pop_back();
  assert(cont.get<Continuation*>());

  auto values = vm.code.back();
  vm.code.pop_back();
  assert(values.tag() == Ptr_tag::vector);


  auto& next_vm = cont.get<Continuation*>()->get();
  
  // finds dynamic-winds for processing..
  const auto wind_index = get_wind_index(vm, next_vm);

  // ====== replaces vm! ======
  vm = next_vm;

  // restores old return values
  vm.code.insert(vm.code.end(), {values, vm_op_restore_values});

  // processes 'before' windings
  for(unsigned i = wind_index; i < vm.extent.size(); ++i){
    vm.stack.push_back({Ptr_tag::vm_argcount, 0});
    vm.code.insert(vm.code.end(), {vm.extent[i].before, vm_op_proc_enter});
  }
}

/*
  stack = (arg1, arg2, ..., arg-bottom)
  ----
  replaces VM!
*/
void proc_enter_cont(Continuation* c){
  auto& next_vm = c->get();

  // finds dynamic-winds for processing..
  const auto wind_index = get_wind_index(vm, next_vm);

  // saves arguments .
  // They become return-values of the passed continuation.
  ZsArgs args;
  auto ret_values = zs_new<Vector>(begin(args), end(args));
  args.cleanup();

  vm.code.insert(vm.code.end(), {ret_values, c, vm_op_replace_vm});

  // processes 'after' windings
  for(unsigned i = vm.extent.size() - 1;
      (i >= wind_index) && (static_cast<signed>(i) >= 0);
      --i){
    vm.stack.push_back({Ptr_tag::vm_argcount, 0});
    vm.code.insert(vm.code.end(), {vm.extent[i].after, vm_op_proc_enter});
  }
}

void proc_enter_srule(SyntaxRules* srule){
  ZsArgs args;

  if(args.size() != 2)
    throw builtin_argcount_failed("syntax-rules entry", 2, 2, args.size());

  try{
    auto code = srule->apply(args[0], args[1].get<Env*>());
    vm.return_value = {code};
  }catch(const zs_error& e){
    // TODO: add name to syntax-rules struct, and use that.
    throw zs_error(printf_string("%s: %s", "syntax-rules", e.what()));
  }
}

} //namespace

/*
  ret = proc
  stack[0] = args
  ---
  goto proc_call or macro_call
*/
void vm_op_call(){
  auto proc = vm.return_value_1();

  if(!is_procedure(proc.tag())){
    // NOTE: too ad-hoc. should be rewritten.
    // We must process args, even this situation is almost wrong.
    // This is required for using a continuation in args. If no global
    // jump occured, errors are reported after.
    return function_call(proc);
  }

  const ProcInfo* info = get_procinfo(proc);
  assert(info);

  switch(info->passing){
  case Passing::eval:
    return function_call(proc);
  case Passing::quote:
    return macro_call(proc);
  case Passing::whole:
    return whole_call(proc);
  default:
    UNEXP_DEFAULT();
  }
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

  if(!is_procedure(proc.tag())){
    vm.code.pop_back();
    vm.stack.pop_back();
    throw zs_error_arg1("eval error", "the object used for call is not a procedure", {proc});
  }

  assert(!vm.stack.empty());

  auto info = get_procinfo(proc);
  auto argc = vm.stack.back().get<int>();

  if(!(info->required_args <= argc && argc <= info->max_args)){
    auto name = get_procname(proc);
    const char* namestr = nullptr;
    if(auto str = name.get<String*>()){
      namestr = str->c_str();
    }else{
      namestr = "(?)";
    }
    // XXX: incomprehensible
    // This local value is required, for avoiding a strange state.
    // If throws directly, std::uncaught_exception() returns true,
    // but std::current_exception() returns NULL object.
    auto e = builtin_argcount_failed(namestr,
                                     info->required_args,
                                     info->max_args, argc);
    throw e;
  }

  switch(info->returning){
  case Returning::pass:
    break;
  case Returning::code:
    vm.code.push_back(vm_op_macro_call);
    break;
  case Returning::stack_splice:
    vm.code.push_back(vm_op_stack_splicing);
    break;
  default:
    UNEXP_DEFAULT();
  }

  if(auto ifun = proc.get<IProcedure*>()){
    proc_enter_interpreted(ifun, info);
  }else if(auto nfun = proc.get<const NProcedure*>()){
    proc_enter_native(nfun);
  }else if(auto cont = proc.get<Continuation*>()){
    proc_enter_cont(cont);
  }else if(auto srule = proc.get<SyntaxRules*>()){
    proc_enter_srule(srule);
  }else{
    UNEXP_DEFAULT();
  }
}
 
/*
  ret = (args)
  ----
  stack = (args)
  goto proc_enter 
*/
void vm_op_move_values(){
  int argc = vm.return_value.size();
  vm.stack.insert(vm.stack.end(), vm.return_value.begin(), vm.return_value.end());
  vm.stack.push_back({Ptr_tag::vm_argcount, argc});
}
 
/*
  leaves frame.
  no stack operations.
*/
void vm_op_leave_frame(){
  auto oldenv = vm.code.back();
  assert(oldenv.tag() == Ptr_tag::env);
  vm.code.pop_back();

  vm.frame = oldenv.get<Env*>();
}  

/*
  code = [consequent, alternative]
  ----
  stack = (consequent or alternative)
*/
void vm_op_if(){
  auto test_result = vm.return_value_1();

  if(test_result.get<bool>()){
    auto conseq = vm.code.back();
    vm.code.pop_back();
    vm.code.back() = conseq;
  }else{
    vm.code.pop_back();
  }
}

/*
  stack = (variable name)
  ----
  stack = ()
  return-value is setted.
*/
void vm_op_set(){
  auto var = vm.code.back();
  vm.code.pop_back();

  if(!identifierp(var)){
    throw builtin_identifier_check_failed("(set)", vm.code.back());
  }

  auto val = vm.return_value_1();

  if(var.tag() == Ptr_tag::symbol){
    vm.frame->set(var, val);
  }else if(var.tag() == Ptr_tag::syntactic_closure){
    if(vm.frame->is_bound(var)){
      // bound alias
      vm.frame->set(var, val);
    }else{
      // not-bound syntactic closure
      auto sc = var.get<SyntacticClosure*>();
      assert(sc);

      sc->env()->set(sc->expr(), val);
    }
  }else{
    UNEXP_DEFAULT();
  }
}

/*
  stack = (variable name)
  ----
  stack = ()
  return-value is setted.
*/
void vm_op_local_set(){
  auto var = vm.code.back();
  vm.code.pop_back();

  if(!identifierp(var)){
    throw builtin_identifier_check_failed("(local set)", var);
  }

  local_set_with_identifier(vm.frame, var, vm.return_value_1()); 
}

void vm_op_leave_winding(){
  vm.extent.pop_back();
}

void vm_op_save_values_and_enter(){
  auto proc = vm.code[vm.code.size() - 1];
  vm.code.back() = zs_new<Vector>(vm.return_value);
  vm.code.push_back(vm_op_restore_values);
  vm.code.push_back(proc);
  vm.code.push_back(vm_op_proc_enter);
}

void vm_op_get_current_env(){
  vm.return_value = {vm.frame};
}

void vm_op_raise(){
  throw vm.return_value_1();
}

void vm_op_unwind_guard(){
  while(!vm.stack.empty()){
    auto p = vm.stack.back();
    vm.stack.pop_back();
    if(p.get<VMop>() == vm_op_unwind_guard) break;
  }

  if(!vm.exception_handler.empty()){
    vm.exception_handler.pop_back();
  }
}

static void invoke_exception_handler(Lisp_ptr errobj){
  if(vm.exception_handler.empty()){
    throw errobj; // going to std::terminate (or handlers established by debug-build)
  }

  auto handler = vm.exception_handler.back();
  vm.exception_handler.pop_back();

  vm.stack.insert(vm.stack.end(), {errobj, {Ptr_tag::vm_argcount, 1}});
  vm.code.insert(vm.code.end(), {vm_op_raise, handler, vm_op_proc_enter});
}

void eval(){
  while(!vm.code.empty()){
    try{
      if(dump_mode) cout << vm << endl;
      auto p = vm.code.back();
      vm.code.pop_back();

      switch(p.tag()){
      case Ptr_tag::symbol:
        vm.return_value = {vm.frame->find(p)};
        break;
    
      case Ptr_tag::cons:
        if(auto c = p.get<Cons*>()){
          vm.code.push_back(vm_op_call);
          vm.code.push_back(car(c));
          vm.stack.push_back(p);
        }else{
          vm.return_value = {Cons::NIL};
        }
        break;

      case Ptr_tag::vm_op:
        if(auto op = p.get<VMop>()){
          op();
        }
        break;

      case Ptr_tag::syntactic_closure:
        if(identifierp(p) && vm.frame->is_bound(p)){
          // bound alias
          vm.return_value = {vm.frame->find(p)};
        }else{
          // not-bound syntax closure
          auto sc = p.get<SyntacticClosure*>();
          assert(sc);

          auto oldenv = vm.frame;
          Env* newenv;

          if(!sc->free_names()){
            newenv = sc->env();
          }else{
            newenv = sc->env()->push();
            for(auto i : sc->free_names()){
              auto val = vm.frame->find(i);
              local_set_with_identifier(newenv, i, val);
            }
          }

          vm.frame = newenv;
          vm.code.push_back(oldenv);
          vm.code.push_back(vm_op_leave_frame);
          vm.code.push_back(sc->expr());
        }
        break;

        // self-evaluating
      case Ptr_tag::undefined:
      case Ptr_tag::boolean: case Ptr_tag::character:
      case Ptr_tag::i_procedure: case Ptr_tag::n_procedure:
      case Ptr_tag::integer:
      case Ptr_tag::rational:
      case Ptr_tag::real:
      case Ptr_tag::complex:
      case Ptr_tag::string: case Ptr_tag::vector:
      case Ptr_tag::input_port: case Ptr_tag::output_port:
      case Ptr_tag::env:
      case Ptr_tag::continuation:
      case Ptr_tag::syntax_rules:
        vm.return_value = {p};
        break;

        // error
      case Ptr_tag::vm_argcount:
        throw zs_error("eval internal error: vm-argcount is rest on VM code stack!\n");

      case Ptr_tag::notation:
      default:
        UNEXP_DEFAULT();
      }

      ++instruction_counter;
      if((instruction_counter % gc_invoke_interval) == 0)
        gc();
    }catch(const zs_error& e){
      // TODO: wrap with 'Condition' object instead of String.
      invoke_exception_handler(zs_new<String>(e.what()));
    }catch(Lisp_ptr p){
      invoke_exception_handler(p);
    }
  }

  if(!vm.code.empty()){
    cerr << "eval internal warning: VM code stack is broken!\n";
    vm.code.clear();
  }

  if(!vm.stack.empty()){
    cerr << "eval internal warning: VM stack is broken! (stack has values unexpectedly.)\n";
    vm.stack.clear();
  }
}

const char* stringify(VMop op){
  if(op == vm_op_nop){
    return "NOP / arg bottom";
  }else if(op == vm_op_arg_push){
    return "arg push";
  }else if(op == vm_op_macro_call){
    return "macro call";
  }else if(op == vm_op_call){
    return "call";
  }else if(op == vm_op_leave_frame){
    return "leave frame";
  }else if(op == vm_op_restore_values){
    return "restore values";
  }else if(op == vm_op_replace_vm){
    return "replace vm";
  }else if(op == vm_op_proc_enter){
    return "proc enter";
  }else if(op == vm_op_move_values){
    return "move values";
  }else if(op == vm_op_if){
    return "if";
  }else if(op == vm_op_set){
    return "set";
  }else if(op == vm_op_local_set){
    return "local set";
  }else if(op == vm_op_leave_winding){
    return "leave winding";
  }else if(op == vm_op_save_values_and_enter){
    return "save values and enter";
  }else if(op == vm_op_stack_splicing){
    return "splicing args";
  }else if(op == vm_op_get_current_env){
    return "get current env";
  }else if(op == vm_op_raise){
    return "raise";
  }else if(op == vm_op_unwind_guard){
    return "unwind guard";
  }else{
    return "unknown vm-op";
  }
}
