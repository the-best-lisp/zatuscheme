#ifndef S_CLOSURE_HH
#define S_CLOSURE_HH

#include "lisp_ptr.hh"

class SyntacticClosure{
public:
  SyntacticClosure(Env*, Cons* free_names, Lisp_ptr expr);
  SyntacticClosure(const SyntacticClosure&) = delete;
  SyntacticClosure(SyntacticClosure&&) = delete;

  ~SyntacticClosure();

  SyntacticClosure& operator=(const SyntacticClosure&) = delete;
  SyntacticClosure& operator=(SyntacticClosure&&) = delete;

  Env* env() const { return env_; }
  Cons* free_names() const { return free_names_; }
  Lisp_ptr expr() const { return expr_; }

  bool is_alias() const
  { return expr_.tag() == Ptr_tag::symbol; }

private:
  Env* env_;
  Cons* free_names_;
  Lisp_ptr expr_;
};

bool identifierp(Lisp_ptr);

#endif // S_CLOSURE_HH
