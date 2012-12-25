#ifndef ZS_HH
#define ZS_HH

#include "builtin.hh"
#include "builtin_boolean.hh"
#include "builtin_cons.hh"
#include "builtin_char.hh"
#include "builtin_equal.hh"
#include "builtin_extra.hh"
#include "builtin_numeric.hh"
#include "builtin_port.hh"
#include "builtin_procedure.hh"
#include "builtin_string.hh"
#include "builtin_symbol.hh"
#include "builtin_syntax.hh"
#include "builtin_util.hh"
#include "builtin_vector.hh"
#include "cons.hh"
#include "cons_util.hh"
#include "decl.hh"
#include "delay.hh"
#include "env.hh"
#include "eval.hh"
#include "lisp_ptr.hh"
#include "number.hh"
#include "port.hh"
#include "printer.hh"
#include "procedure.hh"
#include "reader.hh"
#include "symbol.hh"
#include "token.hh"
#include "util.hh"
#include "vm.hh"

void zs_init();

#endif //ZS_HH
