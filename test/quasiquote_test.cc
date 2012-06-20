#include <cstdlib>
#include <cassert>

#include "eval.hh"
#include "test_util.hh"
#include "builtin.hh"
#include "printer.hh"

static bool result = true;

Lisp_ptr eval_text(const char* s){
  auto exp = read_from_string(s);
  if(!exp){
    printf("[failed] read error on %s\n", s);
    result = false;
    return {};
  }

  VM.code().push(exp);
  eval();
  auto ret = VM.return_value();
  if(!ret){
    printf("[failed] eval error on %s\n", s);
    result = false;
    return {};
  }

  return ret;
}

void check(const char* input, const char* expect){
  auto e = eval_text(input);
  if(!e){
    result = false;
    return;
  }

  if(!test_on_print(e, expect,
                    [expect](const char* s){
                      printf("[failed] expected %s, but got %s\n", expect, s);
                    })){
    result = false;
    return;
  }
}


int main(){
  install_builtin();

  check("`1", "1");
  check("`,1", "1");
  check("`#(1)", "#(1)");
  check("`()", "()");
  check("`(1)", "(1)");
  check("`(,1)", "(1)");
  
  eval_text("(define (retlist) (list 1 2 3))");
  check("(retlist)", "(1 2 3)");
  check("`(0,(retlist))", "(0 (1 2 3))");
  check("`(,@(retlist))", "(1 2 3)");

  check("`(,())", "(())");
  check("`(,() 1 2 ,() 3)", "(() 1 2 () 3)");
  check("`(,@() 1 2 ,() 3)", "(1 2 () 3)");

  return (result) ? EXIT_SUCCESS : EXIT_FAILURE;
}
