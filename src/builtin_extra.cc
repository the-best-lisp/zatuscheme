#include "builtin_extra.hh"
#include "lisp_ptr.hh"
#include "vm.hh"
#include "builtin_util.hh"
#include "procedure.hh"
#include "util.hh"

using namespace std;
using namespace Procedure;

Lisp_ptr traditional_transformer(){
  ZsArgs args{1};

  auto iproc = args[0].get<IProcedure*>();
  if(!iproc){
    throw zs_error("traditional-transformer: error: called with a wrong type (%s)\n",
                   stringify(args[0].tag()));
  }
  auto info = *iproc->info();
  info.passing = Passing::quote;
  info.returning = Returning::code;

  return new IProcedure(iproc->get(), info,
                        iproc->arg_list(), iproc->closure());
}

Lisp_ptr gensym(){
  static const string gensym_symname = {"(gensym)"};
  ZsArgs args{0};
  return {new Symbol(&gensym_symname)};
}

Lisp_ptr exit_func(){
  ZsArgs args{0};
  // cerr << "exiting..\n";
  vm.stack.clear();
  vm.code.clear();
  return {};
}
