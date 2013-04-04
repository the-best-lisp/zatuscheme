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

//
// function objects
//

template<typename Fun>
inline Lisp_ptr number_pred(Fun&& fun){
  ZsArgs args;

  if(is_numeric_type(args[0])){
    return Lisp_ptr{fun(args[0])};
  }else{
    return Lisp_ptr{false};
  }
}

struct complex_found{
  static constexpr const char* msg = "native func: number compare: complex cannot be ordinated\n";
  template<typename CmpT>
  Lisp_ptr operator()(const CmpT&, const CmpT&) const{
    throw zs_error(msg);
  }
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


template<template <typename> class Fun>
struct pos_neg_pred{
  inline bool operator()(Lisp_ptr p){
    if(is_integer_type(p)){
      static const Fun<int> fun;
      return fun(coerce<int>(p), 0);
    }else if(is_real_type(p)){
      static const Fun<double> fun;
      return fun(coerce<double>(p), 0);
    }else if(is_complex_type(p)){
      static constexpr Fun<double> fun;
      auto c = coerce<Complex>(p);
      return (c.imag() == 0) && fun(c.real(), 0);
    }else{
      UNEXP_DEFAULT();
    }
  }
};


template<template <typename> class Fun>
struct even_odd_pred{
  inline bool operator()(Lisp_ptr p){
    if(is_integer_type(p)){
      static const Fun<int> fun;
      return fun(coerce<int>(p) % 2, 0);
    }else{
      return false;
    }
  }
};


template<typename Fun, typename Iter>
inline
Lisp_ptr number_accumulate(const char* name, Lisp_ptr init, const Fun& fun,
                           const Iter& args_begin, const Iter& args_end){
  auto acc = init;

  for(auto i = args_begin, e = args_end; i != e; ++i){
    if(!is_numeric_type(*i)){
      throw number_type_check_failed(name, *i);
    }

    acc = fun(acc, *i);
    if(!acc){ // assumed 'uncaught_exception' context
      return {};
    }
  }

  return acc;
}

template<class Fun>
inline
Lisp_ptr number_accumulate(const char* name, Lisp_ptr init, const Fun& fun){
  ZsArgs args;
  return number_accumulate(name, init, fun, args.begin(), args.end());
}


template<template <typename> class Cmp>
struct minmax_accum{
  inline Lisp_ptr operator()(Lisp_ptr n1, Lisp_ptr n2) const{
    if(!is_numeric_type(n1)){
      throw number_type_check_failed("min/max", n1);
    }

    if(!is_numeric_type(n2)){
      throw number_type_check_failed("mix/max", n2);
    }

    if(is_integer_type(n1) && is_integer_type(n2)){
      static const Cmp<int> cmp;
      if(cmp(coerce<int>(n1), coerce<int>(n2))){
        return n1;
      }else{
        return n2;
      }
    }else if(is_real_type(n1) && is_real_type(n2)){
      static const Cmp<double> cmp;
      if(cmp(coerce<double>(n1), coerce<double>(n2))){
        return n1;
      }else{
        return n2;
      }
    }else if(is_complex_type(n1) && is_complex_type(n2)){
      throw zs_error(complex_found::msg);
    }else{
      UNEXP_DEFAULT();
    }
  }
};    

template<template <typename> class Op>
struct binary_accum{
  inline Lisp_ptr operator()(Lisp_ptr n1, Lisp_ptr n2) const{
    if(!is_numeric_type(n1)){
      throw number_type_check_failed("(binary numeric)", n1);
    }

    if(!is_numeric_type(n2)){
      throw number_type_check_failed("(binary numeric)", n2);
    }


    if(is_integer_type(n1) && is_integer_type(n2)){
      // TODO: add limit check
      static constexpr Op<int> op;
      return Lisp_ptr{Ptr_tag::integer, op(coerce<int>(n1), coerce<int>(n2))};
    }else if(is_real_type(n1) && is_real_type(n2)){
      static constexpr Op<double> op;
      return Lisp_ptr{new double(op(coerce<double>(n1), coerce<double>(n2)))};
    }else if(is_complex_type(n1) && is_complex_type(n2)){
      static constexpr Op<Complex> op;
      return Lisp_ptr{new Complex(op(coerce<Complex>(n1),
                                             coerce<Complex>(n2)))};
    }else{
      UNEXP_DEFAULT();
    }      
  }
};

template<typename Fun>
inline
Lisp_ptr number_divop(const char* name, Fun&& fun){
  ZsArgs args;

  for(auto i = 0; i < 2; ++i){
    if(!is_integer_type(args[i])){
      throw zs_error_arg1(name, "not integer type", {args[i]});
    }
  }
  
  return Lisp_ptr{Ptr_tag::integer,
      fun(coerce<int>(args[0]), coerce<int>(args[1]))};
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

template<typename RFun, typename CFun>
inline
Lisp_ptr number_binary_op(const char* name, RFun&& rfun, CFun&& cfun){
  ZsArgs args;

  for(auto i = 0; i < 2; ++i){
    if(!is_numeric_type(args[i])){
      throw number_type_check_failed(name, args[i]);
    }
  }
  
  if(is_real_type(args[0]) && is_real_type(args[1])){
    return rfun(coerce<double>(args[0]), coerce<double>(args[1]));
  }else if(is_complex_type(args[0]) && is_complex_type(args[1])){
    return cfun(coerce<Complex>(args[0]),
                coerce<Complex>(args[1]));
  }else{
    UNEXP_DEFAULT();
  }
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
};

struct pass_through{
  template<typename T>
  Lisp_ptr operator()(T t) const{
    return wrap_number(t);
  }
};

template<
  typename IFun, typename RFun, typename CFun
  >
inline
Lisp_ptr number_unary(const char* name, const IFun& ifun,
                      const RFun& rfun, const CFun& cfun,
                      bool no_except = false){
  static const auto no_except_value = Lisp_ptr{false};

  ZsArgs args;
  auto arg1 = args[0];


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
  return number_pred([](Lisp_ptr p){
      return (p.tag() == Ptr_tag::integer);
    });
}

Lisp_ptr inexactp(){
  return number_pred([](Lisp_ptr p){
      return (p.tag() == Ptr_tag::complex || p.tag() == Ptr_tag::real);
    });
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
  return number_pred(pos_neg_pred<std::equal_to>());
}

Lisp_ptr positivep(){
  return number_pred(pos_neg_pred<std::greater>());
}

Lisp_ptr negativep(){
  return number_pred(pos_neg_pred<std::less>());
}

Lisp_ptr oddp(){
  return number_pred(even_odd_pred<std::not_equal_to>());
}

Lisp_ptr evenp(){
  return number_pred(even_odd_pred<std::equal_to>());
}


Lisp_ptr number_max(){
  ZsArgs args;
  return number_accumulate("max", args[0], minmax_accum<std::greater>(),
                           next(begin(args)), end(args));
}

Lisp_ptr number_min(){
  ZsArgs args;
  return number_accumulate("min", args[0], minmax_accum<std::less>(),
                           next(begin(args)), end(args));
}

Lisp_ptr number_plus(){
  return number_accumulate("+", Lisp_ptr{Ptr_tag::integer, 0}, binary_accum<std::plus>());
}

Lisp_ptr number_multiple(){
  return number_accumulate("*", Lisp_ptr{Ptr_tag::integer, 1}, binary_accum<std::multiplies>());
}

Lisp_ptr number_minus(){
  ZsArgs args;

  if(!is_numeric_type(args[0])){
    throw number_type_check_failed("-", args[0]);
  }

  if(args.size() == 1){
    if(is_integer_type(args[0])){
      // TODO: add limit check
      return Lisp_ptr{Ptr_tag::integer, -coerce<int>(args[0])};
    }else if(is_real_type(args[0])){
      auto f = coerce<double>(args[0]);
      return Lisp_ptr{new double(-f)};
    }else if(is_complex_type(args[0])){
      auto c = coerce<Complex>(args[0]);
      return Lisp_ptr{new Complex(-c)};
    }else{
      UNEXP_DEFAULT();
    }
  }else{
    return number_accumulate("-", args[0], binary_accum<std::minus>(),
                             next(args.begin()), args.end());
  }
}

Lisp_ptr number_divide(){
  ZsArgs args;

  if(!is_numeric_type(args[0])){
    throw number_type_check_failed("/", args[0]);
  }

  if(args.size() == 1){
    if(is_integer_type(args[0])){
      // TODO: add type check
      return Lisp_ptr{new double(1.0 / coerce<int>(args[0]))};
    }else if(is_real_type(args[0])){
      return Lisp_ptr{new double(1.0 / coerce<double>(args[0]))};
    }else if(is_complex_type(args[0])){
      return Lisp_ptr{new Complex(1.0 / coerce<Complex>(args[0]))};
    }else{
      UNEXP_DEFAULT();
    }
  }else{
    return number_accumulate("/", args[0], binary_accum<std::divides>(),
                             next(args.begin()), args.end());
  }
}

Lisp_ptr number_abs(){
  return number_unary("abs",
                      [](int i){ return wrap_number(std::abs(i));},
                      [](double d){ return wrap_number(std::abs(d));},
                      inacceptable_number_type());
}


Lisp_ptr number_quot(){
  return number_divop("quotient", std::divides<int>());
}

Lisp_ptr number_rem(){
  return number_divop("remainder",
                      [](int i1, int i2) -> int{
                        auto q = i1 / i2;
                        return i1 - (q * i2);
                      });
}

Lisp_ptr number_mod(){
  return number_divop("modulo", 
                      [](int i1, int i2) -> int{
                        auto m = i1 % i2;

                        if((m < 0 && i2 > 0) || (m > 0 && i2 < 0)){
                          return m + i2;
                        }else{
                          return m;
                        }
                      });
}

Lisp_ptr number_gcd(){
  return number_accumulate("gcd", Lisp_ptr(Ptr_tag::integer, 0),
                           [](Lisp_ptr n1, Lisp_ptr n2) -> Lisp_ptr {
                             if(!is_integer_type(n1) || !is_integer_type(n2)){
                               throw zs_error_arg1("gcd", "passed one is not integer.");
                             }

                             return Lisp_ptr{Ptr_tag::integer,
                                 gcd(coerce<int>(n1), coerce<int>(n2))};
                           });
}

Lisp_ptr number_lcm(){
  return number_accumulate("lcm", Lisp_ptr(Ptr_tag::integer, 1),
                           [](Lisp_ptr n1, Lisp_ptr n2) -> Lisp_ptr {
                             if(!is_integer_type(n1) || !is_integer_type(n2)){
                               throw zs_error_arg1("lcm", "passed one is not integer.");
                             }

                             auto i1 = coerce<int>(n1);
                             auto i2 = coerce<int>(n2);

                             return Lisp_ptr{Ptr_tag::integer,
                                 abs(i1 * i2 / gcd(i1, i2))};
                           });
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

  if(is_numeric_type(args[0])){
    throw number_type_check_failed("atan", args[0]);
  }

  switch(args.size()){
  case 1:  // std::atan()
    if(is_real_type(args[0])){
      return {new double(std::atan(coerce<double>(args[0])))};
    }else if(is_complex_type(args[0])){
      return {new Complex(std::atan(coerce<Complex>(args[0])))};
    }else{
      UNEXP_DEFAULT();
    }

  case 2: {// std::atan2()
    if(is_numeric_type(args[1])){
      throw number_type_check_failed("atan", args[1]);
    }

    if(is_real_type(args[0]) && is_real_type(args[1])){
      return {new double(std::atan2(coerce<double>(args[0]),
                                    coerce<double>(args[2])))};
    }else if(is_complex_type(args[0]) && is_complex_type(args[1])){
      throw zs_error("native func: (atan <complex> <complex>) is not implemented.\n");
    }else{
      UNEXP_DEFAULT();
    }
  }

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
  return number_binary_op("expt",
                          [](double n1, double n2) -> Lisp_ptr{
                            return {new double(std::pow(n1, n2))};
                          },
                          [](const Complex& n1, const Complex& n2) -> Lisp_ptr{
                            return {new Complex(std::pow(n1, n2))};
                          });
}

Lisp_ptr number_rect(){
  return number_binary_op("make-rectangular",
                          [](double n1, double n2) -> Lisp_ptr{
                            return {new Complex(n1, n2)};
                          },
                          complex_found());
}

Lisp_ptr number_polar(){
  return number_binary_op("make-polar",
                          [](double n1, double n2) -> Lisp_ptr{
                            return {new Complex(polar(n1, n2))};
                          },
                          complex_found());
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
