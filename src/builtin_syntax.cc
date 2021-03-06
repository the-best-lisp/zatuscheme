#include <string>

#include "builtin_syntax.hh"
#include "cons_util.hh"
#include "eval.hh"
#include "lisp_ptr.hh"
#include "procedure.hh"
#include "s_closure.hh"
#include "s_rules.hh"
#include "vm.hh"
#include "zs_error.hh"
#include "zs_memory.hh"

using namespace std;
using namespace zs::proc_flag;

namespace zs {
namespace {

Lisp_ptr lambda_internal(Lisp_ptr args, Lisp_ptr code){
  auto arg_info = parse_func_arg(args);

  if(arg_info.first < 0){
    throw_zs_error(args, "invalid args!");
  }

  if(!code || nullp(code)){
    throw_zs_error(code, "invalid body!");
  }

  return zs_new<IProcedure>(code, 
                            ProcInfo{arg_info.first, arg_info.second},
                            args, vm.frame);
}

} // namespace

namespace builtin {

Lisp_ptr quote(ZsArgs args){
  if(args[0].tag() == Ptr_tag::syntactic_closure){
    return args[0].get<SyntacticClosure*>()->expr();
  }else{
    return args[0];
  }
}

Lisp_ptr lambda(ZsArgs args){
  return lambda_internal(args[0], args[1]);
}

Lisp_ptr if_(ZsArgs args){
  vm.code.insert(vm.code.end(),
                 {(args.size() == 3) ? args[2] : Lisp_ptr(), // alt
                     args[1],   // conseq
                     vm_op_if,
                     args[0]});  // test
  return {};
}

Lisp_ptr set(ZsArgs args){
  vm.code.insert(vm.code.end(),
                 {args[0], vm_op_set, args[1]});
  return {};
}

Lisp_ptr define(ZsArgs args){
  if(identifierp(args[0])){
    auto& ident = args[0];
    auto& expr = args[1];

    if(!nullp(args[2])){
      throw_zs_error(args[2], "informal length");
    }

    vm.code.insert(vm.code.end(), {ident, vm_op_define, expr});
    return {};
  }else if(args[0].tag() == Ptr_tag::cons){
    auto& largs = args[0];
    auto code = zs_new<Cons>(args[1], args[2]);
    vm.code.insert(vm.code.end(),
                   {nth_cons_list<0>(largs), // funcname
                    vm_op_define,
                    lambda_internal(nthcdr_cons_list<1>(largs), // arg_list
                                    code)});
    return {};
  }else{
    throw_zs_error(args[0], "informal syntax!");
  }
}

Lisp_ptr unquote_splicing(ZsArgs args){
  check_type(Ptr_tag::cons, args[0]);

  vm.return_value.assign(begin(args[0]), end(args[0]));
  return {};
}

Lisp_ptr syntax_rules(ZsArgs args){
  return zs_new<SyntaxRules>(vm.frame, args[0], args[1]);
}
    
Lisp_ptr memv(ZsArgs args){
  check_type(Ptr_tag::cons, args[1]);

  for(auto i = begin(args[1]); i; ++i){
    if(zs::eqv(args[0], *i))
      return i.base();
  }

  return Lisp_ptr{false};
}

Lisp_ptr list_star(ZsArgs args){
  GrowList gl;

  for(auto i = 0; i < args.size() - 1; ++i){
    gl.push(args[i]);
  }

  return gl.extract_with_tail(args[args.size() - 1]);
}

} // namespace builtin
} // namespace zs
