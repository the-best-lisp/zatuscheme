#include "cons.hh"

static_assert(sizeof(Cons) == sizeof(Lisp_ptr) * 2,
              "cons cell sizing failed");

// for non-constexpr context
constexpr Lisp_ptr Cons::NIL;
