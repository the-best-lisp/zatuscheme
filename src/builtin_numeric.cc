#include <iterator>
#include <vector>
#include <functional>
#include <algorithm>
#include <numeric>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <iostream>
#include <sstream>

#include "builtin_numeric.hh"
#include "vm.hh"
#include "builtin.hh"
#include "zs_error.hh"
#include "lisp_ptr.hh"
#include "eval.hh"
#include "builtin_util.hh"
#include "printer.hh"
#include "equality.hh"
#include "token.hh"
#include "util.hh"

using namespace std;

namespace {

zs_error number_type_check_failed(const char* func_name, Lisp_ptr p){
  return zs_error_arg1(func_name,
                       "arg is not number!",
                       {p});
}

bool is_numeric_type(Lisp_ptr p){
  auto tag = p.tag();
  return (tag == Ptr_tag::integer
          || tag == Ptr_tag::real
          || tag == Ptr_tag::complex);
}

bool is_integer_type(Lisp_ptr p){
  return (p.tag() == Ptr_tag::integer);
}

bool is_rational_type(Lisp_ptr p){
  return (false) // TODO: add rational!
    || is_integer_type(p);
}

bool is_real_type(Lisp_ptr p){
  return (p.tag() == Ptr_tag::real)
    || is_rational_type(p);
}

bool is_complex_type(Lisp_ptr p){
  return (p.tag() == Ptr_tag::complex)
    || is_real_type(p);
}

//
// utilities
//

template<typename T> T coerce(Lisp_ptr);

template<>
int coerce(Lisp_ptr p){
  if(p.tag() == Ptr_tag::integer){
    return p.get<int>();
  }else{
    UNEXP_DEFAULT();
  }
}

template<>
double coerce(Lisp_ptr p){
  if(p.tag() == Ptr_tag::real){
    return *(p.get<double*>());
  }else{
    return static_cast<double>(coerce<int>(p));
  }
}

template<>
Complex coerce(Lisp_ptr p){
  if(p.tag() == Ptr_tag::complex){
    return *(p.get<Complex*>());
  }else{
    return Complex(coerce<double>(p), 0);
  }
}

Lisp_ptr wrap_number(int i){
  return {Ptr_tag::integer, i};
}

Lisp_ptr wrap_number(double d){
  return {new double(d)};
}

Lisp_ptr wrap_number(const Complex& z){
  return {new Complex(z)};
}


template<typename T>
T gcd(T m, T n){
  if(m < 0) m = -m;
  if(n < 0) n = -n;

  if(m < n)
    std::swap(m, n);

  while(n > 0){
    auto mod = m % n;
    m = n;
    n = mod;
  }

  return m;
}


//
// implementation utilities
//

template<typename IFun, typename RFun, typename CFun>
Lisp_ptr number_unary(Lisp_ptr arg1,
                      const char* name, const IFun& ifun,
                      const RFun& rfun, const CFun& cfun,
                      bool no_except = false){
  static const auto no_except_value = Lisp_ptr{false};

  if(!is_numeric_type(arg1)){
    if(no_except){
      return no_except_value;
    }else{
      throw number_type_check_failed(name, arg1);
    }
  }

  if(is_integer_type(arg1)){
    return ifun(coerce<int>(arg1));
  }else if(is_real_type(arg1)){
    return rfun(coerce<double>(arg1));
  }else if(is_complex_type(arg1)){
    return cfun(coerce<Complex>(arg1));
  }else{
    if(no_except){
      return no_except_value;
    }else{
      UNEXP_DEFAULT();
    }
  }
}

template<typename IFun, typename RFun, typename CFun>
inline
Lisp_ptr number_unary(const char* name, const IFun& ifun,
                      const RFun& rfun, const CFun& cfun,
                      bool no_except = false){
  ZsArgs args;
  return number_unary(args[0], name, ifun, rfun, cfun, no_except);
}


template<typename IFun, typename RFun, typename CFun>
Lisp_ptr number_binary(Lisp_ptr arg1, Lisp_ptr arg2,
                       const char* name, const IFun& ifun,
                       const RFun& rfun, const CFun& cfun){
  if(!is_numeric_type(arg1)){
    throw number_type_check_failed(name, arg1);
  }

  if(!is_numeric_type(arg2)){
    throw number_type_check_failed(name, arg2);
  }
  
  if(is_integer_type(arg1) && is_integer_type(arg2)){
    return ifun(coerce<int>(arg1),
                coerce<int>(arg2));
  }else if(is_real_type(arg1) && is_real_type(arg2)){
    return rfun(coerce<double>(arg1),
                coerce<double>(arg2));
  }else if(is_complex_type(arg1) && is_complex_type(arg2)){
    return cfun(coerce<Complex>(arg1),
                coerce<Complex>(arg2));
  }else{
    UNEXP_DEFAULT();
  }
}

template<typename IFun, typename RFun, typename CFun>
Lisp_ptr number_binary(const char* name, const IFun& ifun,
                       const RFun& rfun, const CFun& cfun){
  ZsArgs args;
  return number_binary(args[0], args[1], name, ifun, rfun, cfun);
}


template<typename IFun, typename RFun, typename CFun, typename Iter>
Lisp_ptr number_fold(const Iter& args_begin, const Iter& args_end,
                     Lisp_ptr init,
                     const char* name, const IFun& ifun,
                     const RFun& rfun, const CFun& cfun){
  auto acc = init;

  for(auto i = args_begin, e = args_end; i != e; ++i){
    acc = number_binary(acc, *i, name, ifun, rfun, cfun);
  }

  return acc;
}

template<typename IFun, typename RFun, typename CFun>
Lisp_ptr number_fold(Lisp_ptr init,
                     const char* name, const IFun& ifun,
                     const RFun& rfun, const CFun& cfun){
  ZsArgs args;
  return number_fold(begin(args), end(args), init,
                     name, ifun, rfun, cfun);
}


struct inacceptable_number_type{
  Lisp_ptr operator()(int) const{
    throw zs_error("number error: cannot accept type: integer\n");
  }

  Lisp_ptr operator()(double) const{
    throw zs_error("number error: cannot accept type: real\n");
  }

  Lisp_ptr operator()(Complex) const{
    throw zs_error("number error: cannot accept type: complex\n");
  }

  template<typename T>
  Lisp_ptr operator()(T t, T) const{
    return operator()(t);
  }
};

struct pass_through{
  template<typename T>
  Lisp_ptr operator()(T t) const{
    return wrap_number(t);
  }
};

struct fall_false{
  template<typename T>
  Lisp_ptr operator()(T) const{
    return Lisp_ptr{false};
  }
};


template<template <typename> class Fun>
struct pos_neg_pred{
  Lisp_ptr operator()(int i) const{
    static const Fun<int> fun;
    return Lisp_ptr{fun(i, 0)};
  }

  Lisp_ptr operator()(double d) const{
    static const Fun<double> fun;
    return Lisp_ptr{fun(d, 0)};
  }

  Lisp_ptr operator()(Complex z) const{
    static const Fun<Complex> fun;
    return Lisp_ptr{fun(z, 0)};
  }
};


template<template <typename> class Fun>
struct even_odd_pred{
  Lisp_ptr operator()(int i) const{
    static const Fun<int> fun;
    return Lisp_ptr{fun(i % 2, 0)};
  }
};


// ==== shoudld be rewrite below ==

struct complex_found{
  static constexpr const char* msg = "native func: number compare: complex cannot be ordinated\n";
};

  // TODO: delete this! dirty!
struct complex_cmp{
  template<typename CmpT>
  bool operator()(const CmpT&, const CmpT&) const{
    throw zs_error(complex_found::msg);
  }
};

template<template <typename> class Fun,
         class ComplexComparator = complex_cmp>
struct number_comparator {
  inline bool operator()(Lisp_ptr p1, Lisp_ptr p2){
    if(is_integer_type(p1) && is_integer_type(p2)){
      static const Fun<int> fun;
      return fun(coerce<int>(p1), coerce<int>(p2));
    }else if(is_real_type(p1) && is_real_type(p2)){
      static const Fun<double> fun;
      return fun(coerce<double>(p1), coerce<double>(p2));
    }else if(is_complex_type(p1) && is_complex_type(p2)){
      static const ComplexComparator fun;
      return fun(coerce<Complex>(p1),
                 coerce<Complex>(p2));
    }else{
      UNEXP_DEFAULT();
    }
  }
};


template<typename Fun>
inline Lisp_ptr number_compare(const char* name, Fun&& fun){
  ZsArgs args;

  auto i1 = begin(args);
  const auto e = end(args);

  if(!is_numeric_type(*i1)){
    throw number_type_check_failed(name, *i1);
  }
                              
  for(auto i2 = next(i1); i2 != e; i1 = i2, ++i2){
    if(!is_numeric_type(*i2)){
      throw number_type_check_failed(name, *i2);
    }

    if(!fun(*i1, *i2)){
      return Lisp_ptr{false};
    }
  }

  return Lisp_ptr{true};
}

} // namespace


Lisp_ptr numberp(){
  ZsArgs args;
  return Lisp_ptr{is_numeric_type(args[0])};
}

Lisp_ptr complexp(){
  ZsArgs args;
  return Lisp_ptr{is_complex_type(args[0])};
}

Lisp_ptr realp(){
  ZsArgs args;
  return Lisp_ptr{is_real_type(args[0])};
}

Lisp_ptr rationalp(){
  ZsArgs args;
  return Lisp_ptr{is_rational_type(args[0])};
}

Lisp_ptr integerp(){
  ZsArgs args;
  return Lisp_ptr{is_integer_type(args[0])};
}

Lisp_ptr exactp(){
  ZsArgs args;
  return Lisp_ptr{args[0].tag() == Ptr_tag::integer};
}

Lisp_ptr inexactp(){
  ZsArgs args;
  return Lisp_ptr{args[0].tag() == Ptr_tag::complex
                  || args[0].tag() == Ptr_tag::real};
}


Lisp_ptr number_equal(){
  return number_compare("=",
                        [](Lisp_ptr p, Lisp_ptr q){
                          return eqv_internal(p, q);
                        });
}

Lisp_ptr number_less(){
  return number_compare("<",
                        number_comparator<std::less>()); 
}

Lisp_ptr number_greater(){
  return number_compare(">",
                        number_comparator<std::greater>());
}
  
Lisp_ptr number_less_eq(){
  return number_compare("<=",
                        number_comparator<std::less_equal>());
}
  
Lisp_ptr number_greater_eq(){
  return number_compare(">=",
                        number_comparator<std::greater_equal>());
}


Lisp_ptr zerop(){
  return number_unary("zero?",
                      pos_neg_pred<std::equal_to>(),
                      pos_neg_pred<std::equal_to>(),
                      pos_neg_pred<std::equal_to>(),
                      true);
}

Lisp_ptr positivep(){
  return number_unary("positive?",
                      pos_neg_pred<std::greater>(),
                      pos_neg_pred<std::greater>(),
                      fall_false(),
                      true);
}

Lisp_ptr negativep(){
  return number_unary("negative?",
                      pos_neg_pred<std::less>(),
                      pos_neg_pred<std::less>(),
                      fall_false(),
                      true);
}

Lisp_ptr oddp(){
  return number_unary("odd?",
                      even_odd_pred<std::not_equal_to>(),
                      fall_false(),
                      fall_false(),
                      true);
}

Lisp_ptr evenp(){
  return number_unary("even?",
                      even_odd_pred<std::equal_to>(),
                      fall_false(),
                      fall_false(),
                      true);
}


Lisp_ptr number_max(){
  ZsArgs args;
  return number_fold(next(args.begin()), args.end(),
                     args[0], "max",
                     [](int i1, int i2){ return wrap_number(max(i1, i2)); },
                     [](double d1, double d2){ return wrap_number(max(d1, d2)); },
                     inacceptable_number_type());
}

Lisp_ptr number_min(){
  ZsArgs args;
  return number_fold(next(args.begin()), args.end(),
                     args[0], "min",
                     [](int i1, int i2){ return wrap_number(min(i1, i2)); },
                     [](double d1, double d2){ return wrap_number(min(d1, d2)); },
                     inacceptable_number_type());
}

Lisp_ptr number_plus(){
  return number_fold(Lisp_ptr{Ptr_tag::integer, 0}, "+",
                     [](int i1, int i2){ return wrap_number(i1 + i2); },
                     [](double d1, double d2){ return wrap_number(d1 + d2); },
                     [](Complex z1, Complex z2){ return wrap_number(z1 + z2); });
}

Lisp_ptr number_multiple(){
  return number_fold(Lisp_ptr{Ptr_tag::integer, 1}, "*",
                     [](int i1, int i2){ return wrap_number(i1 * i2); },
                     [](double d1, double d2){ return wrap_number(d1 * d2); },
                     [](Complex z1, Complex z2){ return wrap_number(z1 * z2); });
}

Lisp_ptr number_minus(){
  ZsArgs args;

  if(!is_numeric_type(args[0])){
    throw number_type_check_failed("-", args[0]);
  }

  if(args.size() == 1){
    return number_unary(args[0], "-",
                        [](int i){ return wrap_number(-i); },
                        [](double d){ return wrap_number(-d); },
                        [](Complex z){ return wrap_number(-z); });
  }else{
    return number_fold(next(args.begin()), args.end(),
                       args[0], "-",
                       [](int i1, int i2){ return wrap_number(i1 - i2); },
                       [](double d1, double d2){ return wrap_number(d1 - d2); },
                       [](Complex z1, Complex z2){ return wrap_number(z1 - z2); });
  }
}

Lisp_ptr number_divide(){
  ZsArgs args;

  if(!is_numeric_type(args[0])){
    throw number_type_check_failed("/", args[0]);
  }

  if(args.size() == 1){
    return number_unary(args[0], "/",
                        [](int i){
                          return wrap_number((i == 1) // integer appears only if '1 / 1'
                                             ? 1
                                             : (1.0 / i));
                        },
                        [](double d){ return wrap_number(1.0 / d); },
                        [](Complex z){ return wrap_number(1.0 / z); });
  }else{
    return number_fold(next(args.begin()), args.end(),
                       args[0], "/",
                       [](int i1, int i2){ 
                         return wrap_number((i1 % i2)
                                            ? static_cast<double>(i1) / i2
                                            : i1 / i2);
                       },
                       [](double d1, double d2){ return wrap_number(d1 / d2); },
                       [](Complex z1, Complex z2){ return wrap_number(z1 / z2); });
  }
}

Lisp_ptr number_abs(){
  return number_unary("abs",
                      [](int i){ return wrap_number(std::abs(i));},
                      [](double d){ return wrap_number(std::abs(d));},
                      inacceptable_number_type());
}


Lisp_ptr number_quot(){
  return number_binary("quotient",
                       [](int i1, int i2){ return wrap_number(i1 / i2); },
                       inacceptable_number_type(),
                       inacceptable_number_type());
}

Lisp_ptr number_rem(){
  return number_binary("remainder",
                       [](int i1, int i2) -> Lisp_ptr {
                         auto q = i1 / i2;
                         return wrap_number(i1 - (q * i2));
                       },
                       inacceptable_number_type(),
                       inacceptable_number_type());
}

Lisp_ptr number_mod(){
  return number_binary("modulo", 
                       [](int i1, int i2) -> Lisp_ptr{
                         auto m = i1 % i2;

                         if((m < 0 && i2 > 0) || (m > 0 && i2 < 0)){
                           return wrap_number(m + i2);
                         }else{
                           return wrap_number(m);
                         }
                       },
                       inacceptable_number_type(),
                       inacceptable_number_type());
}

Lisp_ptr number_gcd(){
  return number_fold(Lisp_ptr{Ptr_tag::integer, 0}, "gcd",
                     [](int i1, int i2){
                       return wrap_number(gcd(i1, i2));
                     },
                     inacceptable_number_type(),
                     inacceptable_number_type());
}

Lisp_ptr number_lcm(){
  return number_fold(Lisp_ptr{Ptr_tag::integer, 1}, "lcm",
                     [](int i1, int i2){
                       return wrap_number(abs(i1 * i2 / gcd(i1, i2)));
                     },
                     inacceptable_number_type(),
                     inacceptable_number_type());
}

Lisp_ptr number_numerator(){
  ZsArgs args;
  if(!is_numeric_type(args[0])){
    throw number_type_check_failed("numerator", args[0]);
  }

  throw zs_error("internal error: native func 'numerator' is not implemented.\n");
}

Lisp_ptr number_denominator(){
  ZsArgs args;
  if(!is_numeric_type(args[0])){
    throw number_type_check_failed("denominator", args[0]);
  }

  throw zs_error("internal error: native func 'denominator' is not implemented.\n");
}


Lisp_ptr number_floor(){
  return number_unary("floor",
                      pass_through(),
                      [](double d){ return wrap_number(std::floor(d));},
                      inacceptable_number_type());
}

Lisp_ptr number_ceil(){
  return number_unary("ceiling",
                      pass_through(),
                      [](double d){ return wrap_number(std::ceil(d));},
                      inacceptable_number_type());
}

Lisp_ptr number_trunc(){
  return number_unary("truncate",
                      pass_through(),
                      [](double d){ return wrap_number(std::trunc(d));},
                      inacceptable_number_type());
}

Lisp_ptr number_round(){
  return number_unary("round",
                      pass_through(),
                      [](double d){ return wrap_number(std::round(d));},
                      inacceptable_number_type());
}


Lisp_ptr number_rationalize(){
  ZsArgs args;

  for(auto i = 0; i < 2; ++i){
    if(!is_numeric_type(args[i])){
      throw number_type_check_failed("rationalize", args[i]);
    }
  }
  
  throw zs_error("internal error: native func 'rationalize' is not implemented.\n");
}


Lisp_ptr number_exp(){
  return number_unary("exp",
                      [](int i){ return wrap_number(std::exp(i));},
                      [](double d){ return wrap_number(std::exp(d));},
                      [](Complex z){ return wrap_number(std::exp(z));});
}

Lisp_ptr number_log(){
  return number_unary("log",
                      [](int i){ return wrap_number(std::log(i));},
                      [](double d){ return wrap_number(std::log(d));},
                      [](Complex z){ return wrap_number(std::log(z));});
}

Lisp_ptr number_sin(){
  return number_unary("sin",
                      [](int i){ return wrap_number(std::sin(i));},
                      [](double d){ return wrap_number(std::sin(d));},
                      [](Complex z){ return wrap_number(std::sin(z));});
}

Lisp_ptr number_cos(){
  return number_unary("cos",
                      [](int i){ return wrap_number(std::cos(i));},
                      [](double d){ return wrap_number(std::cos(d));},
                      [](Complex z){ return wrap_number(std::cos(z));});
}

Lisp_ptr number_tan(){
  return number_unary("tan",
                      [](int i){ return wrap_number(std::tan(i));},
                      [](double d){ return wrap_number(std::tan(d));},
                      [](Complex z){ return wrap_number(std::tan(z));});
}

Lisp_ptr number_asin(){
  return number_unary("asin",
                      [](int i){ return wrap_number(std::asin(i));},
                      [](double d){ return wrap_number(std::asin(d));},
                      [](Complex z){ return wrap_number(std::asin(z));});
}

Lisp_ptr number_acos(){
  return number_unary("acos",
                      [](int i){ return wrap_number(std::acos(i));},
                      [](double d){ return wrap_number(std::acos(d));},
                      [](Complex z){ return wrap_number(std::acos(z));});
}

Lisp_ptr number_atan(){
  ZsArgs args;

  switch(args.size()){
  case 1:  // std::atan()
    return number_unary(args[0], "atan",
                        [](int i){ return wrap_number(std::atan(i)); },
                        [](double d){ return wrap_number(std::atan(d)); },
                        [](Complex z){ return wrap_number(std::atan(z)); });
  case 2: // std::atan2()
    return number_binary(args[0], args[1], "atan",
                         [](int i1, int i2){
                           return wrap_number(std::atan2(i1, i2));
                         },
                         [](double d1, double d2){
                           return wrap_number(std::atan2(d1, d2));
                         },
                         inacceptable_number_type());
  default:
    throw builtin_argcount_failed("atan", 1, 2, args.size());
  }
}

Lisp_ptr number_sqrt(){
  return number_unary("sqrt",
                      [](int i){ return wrap_number(std::sqrt(i));},
                      [](double d){ return wrap_number(std::sqrt(d));},
                      [](Complex z){ return wrap_number(std::sqrt(z));});
}


Lisp_ptr number_expt(){
  return number_binary("expt",
                       [](int i1, int i2){
                         return wrap_number(std::pow(i1, i2));
                       },
                       [](double n1, double n2){
                         return wrap_number(std::pow(n1, n2));
                       },
                       [](Complex z1, Complex z2){
                         return wrap_number(std::pow(z1, z2));
                       });
}

Lisp_ptr number_rect(){
  return number_binary("make-rectangular",
                       [](int i1, int i2){
                         return wrap_number(Complex(i1, i2));
                       },
                       [](double n1, double n2){
                         return wrap_number(Complex(n1, n2));
                       },
                       inacceptable_number_type());
}

Lisp_ptr number_polar(){
  return number_binary("make-polar",
                       [](int i1, int i2){
                         return wrap_number(polar(static_cast<double>(i1),
                                                  static_cast<double>(i2)));
                       },
                       [](double n1, double n2){
                         return wrap_number(polar(n1, n2));
                       },
                       inacceptable_number_type());
}


Lisp_ptr number_real(){
  return number_unary("real-part",
                      inacceptable_number_type(),
                      inacceptable_number_type(),
                      [](Complex z){ return wrap_number(z.real());});
}

Lisp_ptr number_imag(){
  return number_unary("imag-part",
                      inacceptable_number_type(),
                      inacceptable_number_type(),
                      [](Complex z){ return wrap_number(z.imag());});
}

Lisp_ptr number_mag(){
  return number_unary("magnitude",
                      inacceptable_number_type(),
                      inacceptable_number_type(),
                      [](Complex z){ return wrap_number(std::abs(z));});
}

Lisp_ptr number_angle(){
  return number_unary("angle",
                      inacceptable_number_type(),
                      inacceptable_number_type(),
                      [](Complex z){ return wrap_number(arg(z));});
}


Lisp_ptr number_i_to_e(){
  // MEMO: add complex<int> type??
  return number_unary("inexact->exact",
                      pass_through(),
                      [](double d){ return wrap_number(static_cast<int>(d));},
                      inacceptable_number_type());
}

Lisp_ptr number_e_to_i(){
  return number_unary("exact->inexact",
                      [](int i){ return wrap_number(static_cast<double>(i));},
                      pass_through(),
                      pass_through());
}


Lisp_ptr number_from_string(){
  ZsArgs args;

  auto str = args[0].get<String*>();
  if(!str){
    throw zs_error_arg1("string->number", "passed arg is not string", {args[0]});
  }

  int radix;

  switch(args.size()){
  case 1:
    radix = 10;
    break;
  case 2: {
    if(!is_integer_type(args[1])){
      throw zs_error_arg1("string->number", "passed radix is not integer", {args[1]});
    }
    radix = coerce<int>(args[1]);
    break;
  }
  default:
    throw builtin_argcount_failed("string->number", 1, 2, args.size());
  }

  istringstream iss(*str);

  auto t = tokenize_number(iss, radix);

  if(t.type() == Token::Type::integer){
    return Lisp_ptr(Ptr_tag::integer, t.get<int>());
  }else if(t.type() == Token::Type::real){
    return {new double(t.get<double>())};
  }else if(t.type() == Token::Type::complex){
    return {new Complex(t.get<Complex>())};
  }else{
    throw zs_error_arg1("string->number", "string cannot be read as number", {args[0]});
  }
}

Lisp_ptr number_to_string(){
  ZsArgs args;

  if(!is_numeric_type(args[0])){
    throw zs_error_arg1("number->string", "passed arg is not number", {args[0]});
  }

  int radix;

  switch(args.size()){
  case 1:
    radix = 10;
    break;
  case 2: {
    if(!is_integer_type(args[1])){
      throw zs_error_arg1("number->string", "passed radix is not number", {args[1]});
    }
    radix = coerce<int>(args[1]);
    break;
  }
  default:
    throw builtin_argcount_failed("number->string", 1, 2, args.size());
  }

  ostringstream oss;
  print(oss, args[0], print_human_readable::f, radix);

  return {new String(oss.str())};
}
