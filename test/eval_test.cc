#include <iostream>

#include "zs.hh"
#include "test_util.hh"

using namespace std;

template<typename Fun>
void check(const Fun& fun, const char* expr_s, const char* expect_s = nullptr){
  auto evaled = eval_text(expr_s);
  if(!evaled){
    if(expect_s) result = false;
    return;
  }

  if(!fun(evaled, expect_s)){
    cerr << "not matched!: " << expr_s << " vs " << expect_s << "\n";
    result = false;
    return;
  }
}

bool read_eqv(Lisp_ptr input, const char* expect_s){
  auto expect = read_from_string(expect_s);
  if(!expect){
    cerr << "reader error occured in expect!: " << expect_s << "\n";
    result = false;
    return false;
  }

  return eqv(input, expect);
}

bool test_true(Lisp_ptr p, const char*){
  return !!p;
}

int main(){
  zs_init();

  // === self-evaluating ===

  check(read_eqv, "#t", "#t");
  check(read_eqv, "#f", "#f");

  check(read_eqv, "2", "2");
  check(read_eqv, "1.01", "1.01");
  check(read_eqv, "1.0-3.1i", "1.0-3.1i");

  check(read_eqv, "#\\R", "#\\R");
  check(read_eqv, "#\\Newline", "#\\Newline");

  check_e("\"sss\"", "\"sss\"");
  check_e("\"\"", "\"\"");

  check_e("#(1 2 3)", "#(1 2 3)");
  check_e("#(1 #(11 12 13) 3)", "#(1 #(11 12 13) 3)");

  check(read_eqv, "()", "()");

  // function, port ??


  // === symbol-value ===
  check_e_undef("tabun-tukattenai-namae");


  // === function call ===
  check(read_eqv, "(+ 1 1)", "2");


  // === Special Operator ===
  // syntax: quote
  check_e_undef("(quote)");
  check(read_eqv, "(quote 1)", "1");
  check_e("(quote (1 . 2))", "(1 . 2)");
  check_e("'(1 2 3)", "(1 2 3)");

  // syntax: lambda

  // syntax: if
  check_e_undef("(if)");
  check_e_undef("(if 1)");
  check(read_eqv, "(if #t 1)", "1");
  check_e_undef("(if #f 1)");
  check(read_eqv, "(if #t 1 2)", "1");
  check(read_eqv, "(if #f 1 2)", "2");
  check_e_undef("(if #f 1 2 3)");

  // syntax: define
  check(read_eqv, "(define x 1)", "1");
  check(read_eqv, "x", "1");
  check(read_eqv, "(+ x x)", "2");
  //check(test_undef, "(define else 1)");
  check(read_eqv, "(define else_ 1)", "1");
  check(read_eqv, "else_", "1");

  // syntax: set!
  check(read_eqv, "(set! x 100)", "100");
  check(read_eqv, "x", "100");

  // syntax: begin
  check_e_undef("(begin)");
  check(read_eqv, "(begin 1)", "1");
  check(read_eqv, "(begin 1 2)", "2");
  check(read_eqv, "(begin 1 2 3)", "3");

  // informal syntaxes
  //check_e_undef("else");
  check_e_undef("(else)");
  check_e_undef("(else 1)");
  //check_e_undef("=>");
  check_e_undef("(=>)");
  check_e_undef("(=> 1)");


  // macro call


  // function test (basic syntax only)
  check(test_true, "(define fun (lambda (y) (set! x y) (+ y y)))");
  check(test_true, "fun");
  check_e("(fun 2)", "4");
  check_e("x", "2");
    
  check(test_true, "(define (fun2 x) (+ 1 x))");
  check_e("(fun2 100)", "101");


  return (result) ? EXIT_SUCCESS : EXIT_FAILURE;
}
