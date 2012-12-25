#include <sstream>
#include <istream>
#include <iostream>
#include <algorithm>

#include "builtin.hh"
#include "util.hh"
#include "number.hh"
#include "procedure.hh"
#include "lisp_ptr.hh"
#include "eval.hh"
#include "builtin_util.hh"
#include "reader.hh"
#include "printer.hh"
#include "vm.hh"
#include "port.hh"

#include "builtin_boolean.hh"
#include "builtin_char.hh"
#include "builtin_cons.hh"
#include "builtin_equal.hh"
#include "builtin_extra.hh"
#include "builtin_numeric.hh"
#include "builtin_port.hh"
#include "builtin_procedure.hh"
#include "builtin_string.hh"
#include "builtin_symbol.hh"
#include "builtin_syntax.hh"
#include "builtin_vector.hh"

using namespace std;
using namespace Procedure;

namespace {

static const char null_env_symname[] = "null-env-value";
static const char r5rs_env_symname[] = "r5rs-env-value";
static const char interaction_env_symname[] = "interaction-env-value";

static
Lisp_ptr env_pick_2(const char* name){
  ZsArgs args{1};
  auto num = args[0].get<Number*>();
  if(!num){
    throw builtin_type_check_failed(name, Ptr_tag::number, args[0]);
  }

  if(num->type() != Number::Type::integer){
    throw make_zs_error("native func: %s: passed number is not exact integer\n", name);
  }

  auto ver = num->get<Number::integer_type>();
  if(ver != 5l){
    throw make_zs_error("native func: %s: passed number is not 5 (supplied %ld)\n",
                        name, ver);
  }

  return vm.find(intern(vm.symtable(), name));
}

Lisp_ptr env_r5rs(){
  return env_pick_2(r5rs_env_symname);
}

Lisp_ptr env_null(){
  return env_pick_2(null_env_symname);
}

Lisp_ptr env_interactive(){
  ZsArgs args{0};
  return vm.find(intern(vm.symtable(), interaction_env_symname));
}
  

Lisp_ptr eval_func(){
  ZsArgs args{2};
  auto env = args[1].get<Env*>();
  if(!env){
    throw builtin_type_check_failed("eval", Ptr_tag::env, args[1]);
  }

  auto oldenv = vm.frame();
  vm.set_frame(env);
  vm.code.insert(vm.code.end(),
                 {oldenv, vm_op_leave_frame, args[0]});
  return vm_op_nop;
}


Lisp_ptr load_func(){
  ZsArgs args{1};
  auto str = args[0].get<String*>();
  if(!str){
    throw builtin_type_check_failed("load", Ptr_tag::string, args[0]);
  }

  stringstream f(*str, ios_base::in);
  if(!f){
    throw zs_error("load error: failed at opening file\n");
  }

  load(&f);
  return vm_op_nop;
}

} //namespace

static const BuiltinFunc
builtin_syntax_funcs[] = {
#include "builtin_equal.defs.hh"
#include "builtin_syntax.defs.hh"
};

static const BuiltinFunc
builtin_funcs[] = {
  {"eval", {
      eval_func,
      {Calling::function, 2}}},

  {"scheme-report-environment", {
      env_r5rs,
      {Calling::function, 1}}},
  {"null-environment", {
      env_null,
      {Calling::function, 1}}},
  {"interaction-environment", {
      env_interactive,
      {Calling::function, 0}}},

  {"load", {
      load_func,
      {Calling::function, 1}}},

#include "builtin_boolean.defs.hh"
#include "builtin_char.defs.hh"
#include "builtin_cons.defs.hh"
#include "builtin_numeric.defs.hh"
#include "builtin_port.defs.hh"
#include "builtin_procedure.defs.hh"
#include "builtin_string.defs.hh"
#include "builtin_symbol.defs.hh"
#include "builtin_vector.defs.hh"
};

static const BuiltinFunc
builtin_extra_funcs[] = {
#include "builtin_extra.defs.hh"
};


static void install_builtin_load(const char* ld[], size_t s){
  for(size_t i = 0; i < s; ++i){
    stringstream ss({ld[i]}, ios_base::in);
    load(&ss);
  }
}

void install_builtin(){
  static constexpr auto install_builtin_native = [](const BuiltinFunc& bf){
    vm.local_set(intern(vm.symtable(), bf.name), {&bf.func});
  };    

  // null-environment
  for_each(std::begin(builtin_syntax_funcs), std::end(builtin_syntax_funcs),
           install_builtin_native);
  vm.local_set(intern(vm.symtable(), null_env_symname), vm.frame());

  // r5rs-environment
  vm.set_frame(vm.frame()->push());
  for_each(std::begin(builtin_funcs), std::end(builtin_funcs),
           install_builtin_native);
  install_builtin_load(builtin_cons_load, builtin_cons_load_size);
  install_builtin_load(builtin_procedure_load, builtin_procedure_load_size);
  install_builtin_load(builtin_port_load, builtin_port_load_size);

  vm.local_set(intern(vm.symtable(), CURRENT_INPUT_PORT_SYMNAME), &std::cin);
  vm.local_set(intern(vm.symtable(), CURRENT_OUTPUT_PORT_SYMNAME), &std::cout);
  vm.local_set(intern(vm.symtable(), r5rs_env_symname), vm.frame());

  // interaction-environment
  vm.set_frame(vm.frame()->push());
  for_each(std::begin(builtin_extra_funcs), std::end(builtin_extra_funcs),
           install_builtin_native);
  install_builtin_load(builtin_extra_load, builtin_extra_load_size);
  vm.local_set(intern(vm.symtable(), interaction_env_symname), vm.frame());
}

void load(InputPort* p){
  while(1){
    auto form = read(*p);
    if(!form){
      if(!*p){
        // cerr << "load error: failed at reading a form. abandoned.\n";
      }
      break;
    }

    vm.code.push_back(form);
    eval();
    if(!vm.return_value[0]){
      cerr << "load error: failed at evaluating a form. skipped.\n";
      cerr << "\tform: \n";
      print(cerr, form);
      continue;
    }
  }
}

