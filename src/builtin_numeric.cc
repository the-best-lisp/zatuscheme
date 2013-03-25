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
#include "number.hh"
#include "lisp_ptr.hh"
#include "eval.hh"
#include "builtin_util.hh"
#include "printer.hh"

using namespace std;

namespace {

zs_error number_type_check_failed(const char* func_name, Lisp_ptr p){
  return zs_error_arg1(func_name,
                       printf_string("arg is not %s!", stringify(Ptr_tag::number)),
                       {p});
}

template<typename Fun>
inline Lisp_ptr number_pred(Fun&& fun){
  ZsArgs args;
  auto num = args[0].get<Number*>();
  if(!num){
    return Lisp_ptr{false};
  }

  return Lisp_ptr{fun(num)};
}


struct complex_found{
  static constexpr const char* msg = "native func: number compare: complex cannot be ordinated\n";
  bool operator()(const Number::complex_type&, const Number::complex_type&) const{
    throw zs_error(msg);
  }
};

template<template <typename> class Fun,
         class ComplexComparator = complex_found>
struct number_comparator {
  inline bool operator()(const Number* n1, const Number* n2){
    if(n1->type() == Number::Type::integer && n2->type() == Number::Type::integer){
      static const Fun<Number::integer_type> fun;
      return fun(n1->get<Number::integer_type>(), n2->get<Number::integer_type>());
    }else if(n1->type() <= Number::Type::real && n2->type() <= Number::Type::real){
      static const Fun<Number::real_type> fun;
      return fun(n1->coerce<Number::real_type>(), n2->coerce<Number::real_type>());
    }else{
      static const ComplexComparator fun;
      return fun(n1->coerce<Number::complex_type>(), n2->coerce<Number::complex_type>());
    }
  }
};


template<typename Fun>
inline Lisp_ptr number_compare(const char* name, Fun&& fun){
  ZsArgs args;

  auto i1 = begin(args);
  const auto e = end(args);

  auto n1 = i1->get<Number*>();
  if(!n1 || n1->type() < Number::Type::integer){
    throw number_type_check_failed(name, *i1);
  }
                              
  for(auto i2 = next(i1); i2 != e; i1 = i2, ++i2){
    auto n2 = i2->get<Number*>();
    if(!n2 || n2->type() < Number::Type::integer){
      throw number_type_check_failed(name, *i2);
    }

    if(!fun(n1, n2)){
      return Lisp_ptr{false};
    }

    n1 = n2;
  }

  return Lisp_ptr{true};
}


template<template <typename> class Fun>
struct pos_neg_pred{
  inline bool operator()(Number* num){
    switch(num->type()){
    case Number::Type::complex:
      return false;
    case Number::Type::real: {
      static constexpr Fun<Number::real_type> fun;
      return fun(num->get<Number::real_type>(), 0);
    }
    case Number::Type::integer: {
      static constexpr Fun<Number::integer_type> fun;
      return fun(num->get<Number::integer_type>(), 0);
    }
    case Number::Type::uninitialized:
    default:
      UNEXP_DEFAULT();
    }
  }
};

template<template <typename> class Fun>
struct even_odd_pred{
  inline bool operator()(Number* num){
    static constexpr Fun<Number::integer_type> fun;

    switch(num->type()){
    case Number::Type::complex:
    case Number::Type::real:
      return false;
    case Number::Type::integer:
      return fun(num->get<Number::integer_type>() % 2, 0);
    case Number::Type::uninitialized:
    default:
      UNEXP_DEFAULT();
    }
  }
};


template<typename Fun, typename Iter>
inline
Lisp_ptr number_accumulate(const char* name, Number&& init, Fun&& fun,
                           const Iter& args_begin, const Iter& args_end){
  for(auto i = args_begin, e = args_end;
      i != e; ++i){
    auto n = i->get<Number*>();
    if(!n){
      throw number_type_check_failed(name, *i);
    }

    if(!fun(init, *n)){ // assumed 'uncaught_exception' context
      return {};
    }
  }

  return {new Number(init)};
}

template<class Fun>
inline
Lisp_ptr number_accumulate(const char* name, Number&& init, Fun&& fun){
  ZsArgs args;
  return number_accumulate(name, move(init), move(fun),
                           args.begin(), args.end());
}


template<template <typename> class Cmp>
struct minmax_accum{
  inline bool operator()(Number& n1, const Number& n2){
    if(n1.type() == Number::Type::uninitialized){
      n1 = n2;
      return true;
    }

    if(n2.type() == Number::Type::uninitialized){
      return true;
    }

    if(n1.type() == Number::Type::integer && n2.type() == Number::Type::integer){
      static constexpr Cmp<Number::integer_type> cmp;

      if(cmp(n2.get<Number::integer_type>(), n1.get<Number::integer_type>()))
        n1 = n2;
      return true;
    }

    if(n1.type() <= Number::Type::real && n2.type() <= Number::Type::real){
      static constexpr Cmp<Number::real_type> cmp;

      if(cmp(n2.coerce<Number::real_type>(), n1.coerce<Number::real_type>())){
        n1 = Number{n2.coerce<Number::real_type>()};
      }else if(n1.type() != Number::Type::real){
        n1 = Number{n1.coerce<Number::real_type>()};
      }
      return true;
    }

    throw zs_error(complex_found::msg);
  }
};    

template<template <typename> class Op>
struct binary_accum{
  inline bool operator()(Number& n1, const Number& n2){
    if(n1.type() == Number::Type::uninitialized){
      n1 = n2;
      return true;
    }

    if(n2.type() == Number::Type::uninitialized){
      return true;
    }

    // n1 type <= n2 type
    if(n1.type() == Number::Type::integer && n2.type() == Number::Type::integer){
      // static constexpr auto imax = numeric_limits<Number::integer_type>::max();
      // static constexpr auto imin = numeric_limits<Number::integer_type>::min();
      static constexpr Op<Number::integer_type> op;

      long long tmp = op(n1.get<Number::integer_type>(), n2.get<Number::integer_type>());
      // if(tmp > imax || tmp < imin){
      //   cerr << "integer operation fallen into float\n";
      //   n1 = Number{static_cast<Number::real_type>(tmp)};
      // }else{
        n1.get<Number::integer_type>() = static_cast<Number::integer_type>(tmp);
      // }

      return true;
    }

    if(n1.type() == Number::Type::real && n2.type() <= Number::Type::real){
      static constexpr Op<Number::real_type> op;
      n1.get<Number::real_type>() = 
        op(n1.get<Number::real_type>(), n2.coerce<Number::real_type>());
      return true;
    }

    if(n1.type() == Number::Type::complex && n2.type() <= Number::Type::complex){
      static constexpr Op<Number::complex_type> op;
      n1.get<Number::complex_type>() = 
        op(n1.get<Number::complex_type>(), n2.coerce<Number::complex_type>());
      return true;
    }

    // n1 type > n2 type
    if(n1.type() < Number::Type::real && n2.type() == Number::Type::real){
      static constexpr Op<Number::real_type> op;
      n1 = Number{op(n1.coerce<Number::real_type>(), n2.get<Number::real_type>())};
      return true;
    }

    if(n1.type() < Number::Type::complex && n2.type() == Number::Type::complex){
      static constexpr Op<Number::complex_type> op;
      n1 = Number{op(n1.coerce<Number::complex_type>(), n2.get<Number::complex_type>())};
      return true;
    }

    // ???
    throw zs_error_arg1("+-*/", "failed at numeric conversion!");
  }
};

template<typename Fun>
inline
Lisp_ptr number_divop(const char* name, Fun&& fun){
  ZsArgs args;
  Number* n[2];

  for(auto i = 0; i < 2; ++i){
    n[i] = args[i].get<Number*>();
    if(!n[i]){
      throw number_type_check_failed(name, args[i]);
    }
    if(n[i]->type() != Number::Type::integer){
      throw zs_error_arg1(name, "not integer type", {n[i]});
    }
  }
  
  return {new Number{fun(n[0]->get<Number::integer_type>(),
                         n[1]->get<Number::integer_type>())}};
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

template<typename Fun>
inline
Lisp_ptr number_rounding(const char* name, Fun&& fun){
  ZsArgs args;

  auto n = args[0].get<Number*>();
  if(!n){
    throw number_type_check_failed(name, args[0]);
  }

  switch(n->type()){
  case Number::Type::integer:
    return {n};
  case Number::Type::real:
    return {new Number(fun(n->get<Number::real_type>()))};
  case Number::Type::complex:
    throw zs_error(complex_found::msg);
  case Number::Type::uninitialized:
  default:
    UNEXP_DEFAULT();
  }
}

template<typename Fun>
inline
Lisp_ptr number_unary_op(const char* name, Fun&& fun){
  ZsArgs args;

  auto n = args[0].get<Number*>();
  if(!n){
    throw number_type_check_failed(name, args[0]);
  }

  switch(n->type()){
  case Number::Type::integer:
  case Number::Type::real:
    return {new Number(fun(n->coerce<Number::real_type>()))};
  case Number::Type::complex:
    return {new Number(fun(n->get<Number::complex_type>()))};
  case Number::Type::uninitialized:
  default:
    UNEXP_DEFAULT();
  }
}

struct exp_fun{
  template<typename T>
  inline T operator()(const T& t1) const{
    return std::exp(t1);
  }
};

struct log_fun{
  template<typename T>
  inline T operator()(const T& t1) const{
    return std::log(t1);
  }
};

struct sin_fun{
  template<typename T>
  inline T operator()(const T& t1) const{
    return std::sin(t1);
  }
};

struct cos_fun{
  template<typename T>
  inline T operator()(const T& t1) const{
    return std::cos(t1);
  }
};

struct tan_fun{
  template<typename T>
  inline T operator()(const T& t1) const{
    return std::tan(t1);
  }
};

struct asin_fun{
  template<typename T>
  inline T operator()(const T& t1) const{
    return std::asin(t1);
  }
};

struct acos_fun{
  template<typename T>
  inline T operator()(const T& t1) const{
    return std::acos(t1);
  }
};

struct sqrt_fun{
  template<typename T>
  inline T operator()(const T& t1) const{
    return std::sqrt(t1);
  }
};

template<typename RFun, typename CFun>
inline
Lisp_ptr number_binary_op(const char* name, RFun&& rfun, CFun&& cfun){
  ZsArgs args;
  Number* n[2];

  for(auto i = 0; i < 2; ++i){
    n[i] = args[i].get<Number*>();
    if(!n[i]){
      throw number_type_check_failed(name, args[i]);
    }
  }
  
  if(n[0]->type() == Number::Type::uninitialized
     || n[1]->type() == Number::Type::uninitialized){
    UNEXP_DEFAULT();
  }

  if(n[0]->type() <= Number::Type::real
     || n[1]->type() <= Number::Type::real){
    return {new Number(rfun(n[0]->coerce<Number::real_type>(),
                            n[1]->coerce<Number::real_type>()))};
  }

  if(n[0]->type() <= Number::Type::complex
     || n[1]->type() <= Number::Type::complex){
    return Lisp_ptr{cfun(n[0]->coerce<Number::complex_type>(),
                         n[1]->coerce<Number::complex_type>())};
  }

  UNEXP_DEFAULT();
}

template<typename Fun>
inline
Lisp_ptr number_unary_op_complex(const char* name, Fun&& fun){
  ZsArgs args;

  auto n = args[0].get<Number*>();
  if(!n){
    throw number_type_check_failed(name, args[0]);
  }

  switch(n->type()){
  case Number::Type::integer:
  case Number::Type::real:
  case Number::Type::complex:
    return {new Number(fun(n->coerce<Number::complex_type>()))};
  case Number::Type::uninitialized:
  default:
    UNEXP_DEFAULT();
  }
}

template<typename Fun>
Lisp_ptr number_i_e(const char* name, Fun&& fun){
  ZsArgs args;

  auto n = args[0].get<Number*>();
  if(!n){
    throw number_type_check_failed(name, args[0]);
  }

  return {new Number(fun(*n))};
}

} // namespace


Lisp_ptr complexp(){
  return number_pred([](Number* n){
      return n->type() >= Number::Type::integer;
    });
}

Lisp_ptr realp(){
  return number_pred([](Number* n){
      return n->type() >= Number::Type::integer
        && n->type() <= Number::Type::real;
    });
}

Lisp_ptr rationalp(){
  return number_pred([](Number* n){
      return n->type() >= Number::Type::integer
        && n->type() < Number::Type::real;
    });
}

Lisp_ptr integerp(){
  return number_pred([](Number* n){
      return n->type() == Number::Type::integer;
    });
}

Lisp_ptr exactp(){
  return number_pred([](Number* n){
      return n->type() == Number::Type::integer;
    });
}

Lisp_ptr inexactp(){
  return number_pred([](Number* n){
      return n->type() == Number::Type::complex
        || n->type() == Number::Type::real;
    });
}


Lisp_ptr number_equal(){
  return number_compare("=",
                        [](const Number* n1, const Number* n2){
                          return eqv(*n1, *n2);
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
  return number_pred([](Number* num) -> bool {
      switch(num->type()){
      case Number::Type::complex: {
        auto c = num->get<Number::complex_type>();
        return (c.real() == 0 && c.imag() == 0);
      }
      case Number::Type::real:
        return num->get<Number::real_type>() == 0;
      case Number::Type::integer:
        return num->get<Number::integer_type>() == 0;
      case Number::Type::uninitialized:
      default:
        UNEXP_DEFAULT();
      }
    });
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
  return number_accumulate("max", Number(), minmax_accum<std::greater>());
}

Lisp_ptr number_min(){
  return number_accumulate("min", Number(), minmax_accum<std::less>());
}

/*
Lisp_ptr number_plus(){
  return number_accumulate("+", Number(0l), binary_accum<std::plus>());
}
*/

// TODO: make other ops!
Lisp_ptr number_plus(){
  ZsArgs args;
  int init = 0;

  for(auto i = begin(args), e = end(args); i != e; ++i){
    switch(i->tag()){
    case Ptr_tag::integer:
      init += i->get<int>();
      break;
    case Ptr_tag::real:
      init += static_cast<int>(*i->get<double*>());
      break;
    case Ptr_tag::complex:
      // init += ...
      break;
    default:
      throw number_type_check_failed("+", *i);
    }
  }

  return Lisp_ptr{Ptr_tag::integer, init};
}

Lisp_ptr number_multiple(){
  return number_accumulate("*", Number(1l), binary_accum<std::multiplies>());
}

Lisp_ptr number_minus(){
  ZsArgs args;

  auto n = args[0].get<Number*>();
  if(!n){
    throw number_type_check_failed("-", args[0]);
  }

  if(args.size() == 1){
    switch(n->type()){
    case Number::Type::integer: {
      static constexpr auto imin = numeric_limits<Number::integer_type>::min();
      auto i = n->get<Number::integer_type>();
      if(i == imin){
        cerr << "warning: integer operation fallen into float\n";
        return {new Number(-static_cast<Number::real_type>(imin))};
      }else{
        return {new Number(-i)};
      }
    }
    case Number::Type::real:
      return {new Number(-n->get<Number::real_type>())};
    case Number::Type::complex: {
      auto c = n->get<Number::complex_type>();
      return {new Number(Number::complex_type(-c.real(), -c.imag()))};
    }
    case Number::Type::uninitialized:
    default:
      UNEXP_DEFAULT();
    }
  }else{
    return number_accumulate("-", Number(*n), binary_accum<std::minus>(),
                             next(args.begin()), args.end());
  }
}

Lisp_ptr number_divide(){
  ZsArgs args;

  auto n = args[0].get<Number*>();
  if(!n){
    throw number_type_check_failed("/", args[0]);
  }

  if(args.size() == 1){
    switch(n->type()){
    case Number::Type::integer:
      return {new Number(1.0 / n->get<Number::integer_type>())};
    case Number::Type::real:
      return {new Number(1.0 / n->get<Number::real_type>())};
    case Number::Type::complex: {
      auto c = n->get<Number::complex_type>();
      return {new Number(1.0 / c)};
    }
    case Number::Type::uninitialized:
    default:
      UNEXP_DEFAULT();
    }
  }else{
    return number_accumulate("/", 
                             n->type() == Number::Type::integer
                             ? Number(n->coerce<Number::real_type>()) : Number(*n),                    
                             binary_accum<std::divides>(),
                             next(args.begin()),
                             args.end());
  }
}

Lisp_ptr number_abs(){
  ZsArgs args;

  auto n = args[0].get<Number*>();
  if(!n){
    throw number_type_check_failed("abs", args[0]);
  }

  switch(n->type()){
  case Number::Type::integer: {
    auto i = n->get<Number::integer_type>();
    if(i >= 0){
      return n;
    }else{
      static constexpr auto imin = numeric_limits<Number::integer_type>::min();
      if(i == imin){
        cerr << "warning: integer operation fallen into float\n";
        return new Number(-static_cast<Number::real_type>(imin));
      }else{
        return new Number(-i);
      }
    }
  }
  case Number::Type::real: {
    auto d = n->get<Number::real_type>();
    return {(d >= 0) ? n : new Number(-d)};
  }
  case Number::Type::complex: {
    throw zs_error(complex_found::msg);
  }
  case Number::Type::uninitialized:
  default:
    UNEXP_DEFAULT();
  }
}


Lisp_ptr number_quot(){
  return number_divop("quotient", std::divides<Number::integer_type>());
}

Lisp_ptr number_rem(){
  return number_divop("remainder",
                      [](Number::integer_type i1, Number::integer_type i2) -> Number::integer_type{
                        auto q = i1 / i2;
                        return i1 - (q * i2);
                      });
}

Lisp_ptr number_mod(){
  return number_divop("modulo", 
                      [](Number::integer_type i1, Number::integer_type i2) -> Number::integer_type{
                        auto m = i1 % i2;

                        if((m < 0 && i2 > 0) || (m > 0 && i2 < 0)){
                          return m + i2;
                        }else{
                          return m;
                        }
                      });
}

Lisp_ptr number_gcd(){
  return number_accumulate("gcd", Number(0l),
                           [](Number& n1, const Number& n2) -> bool {
                             if(n1.type() != Number::Type::integer
                                || n2.type() != Number::Type::integer){
                               throw zs_error_arg1("gcd", "passed one is not integer.");
                             }

                             auto i1 = n1.get<Number::integer_type>();
                             auto i2 = n2.get<Number::integer_type>();

                             n1 = Number{gcd(i1, i2)};
                             return true;
                           });
}

Lisp_ptr number_lcm(){
  return number_accumulate("lcm", Number(1l),
                           [](Number& n1, const Number& n2) -> bool {
                             if(n1.type() != Number::Type::integer
                                || n2.type() != Number::Type::integer){
                               throw zs_error_arg1("lcm", "passed one is not integer.");
                             }

                             auto i1 = n1.get<Number::integer_type>();
                             auto i2 = n2.get<Number::integer_type>();

                             n1 = Number{abs(i1 * i2 / gcd(i1, i2))};
                             return true;
                           });
}

Lisp_ptr number_numerator(){
  ZsArgs args;
  auto num = args[0].get<Number*>();
  if(!num){
    throw number_type_check_failed("numerator", args[0]);
  }

  throw zs_error("internal error: native func 'numerator' is not implemented.\n");
}

Lisp_ptr number_denominator(){
  ZsArgs args;
  auto num = args[0].get<Number*>();
  if(!num){
    throw number_type_check_failed("denominator", args[0]);
  }

  throw zs_error("internal error: native func 'denominator' is not implemented.\n");
}


Lisp_ptr number_floor(){
  return number_rounding("floor", [](Number::real_type d){ return std::floor(d); });
}

Lisp_ptr number_ceil(){
  return number_rounding("ceiling", [](Number::real_type d){ return std::ceil(d); });
}

Lisp_ptr number_trunc(){
  return number_rounding("truncate", [](Number::real_type d){ return std::trunc(d); });
}

Lisp_ptr number_round(){
  return number_rounding("round", [](Number::real_type d){ return std::round(d); });
}


Lisp_ptr number_rationalize(){
  ZsArgs args;
  Number* n[2];

  for(auto i = 0; i < 2; ++i){
    n[i] = args[i].get<Number*>();
    if(!n[i]){
      throw number_type_check_failed("rationalize", args[i]);
    }
  }
  
  throw zs_error("internal error: native func 'rationalize' is not implemented.\n");
}


Lisp_ptr number_exp(){
  return number_unary_op("exp", exp_fun());
}

Lisp_ptr number_log(){
  return number_unary_op("log", log_fun());
}

Lisp_ptr number_sin(){
  return number_unary_op("sin", sin_fun());
}

Lisp_ptr number_cos(){
  return number_unary_op("cos", cos_fun());
}

Lisp_ptr number_tan(){
  return number_unary_op("tan", tan_fun());
}

Lisp_ptr number_asin(){
  return number_unary_op("asin", asin_fun());
}

Lisp_ptr number_acos(){
  return number_unary_op("acos", acos_fun());
}

Lisp_ptr number_atan(){
  ZsArgs args;

  auto n1 = args[0].get<Number*>();
  if(!n1){
    throw number_type_check_failed("atan", args[0]);
  }

  switch(args.size()){
  case 1:  // std::atan()
    switch(n1->type()){
    case Number::Type::integer:
    case Number::Type::real:
      return {new Number(std::atan(n1->coerce<Number::real_type>()))};
    case Number::Type::complex: {
      return {new Number(std::atan(n1->get<Number::complex_type>()))};
    }
    case Number::Type::uninitialized:
    default:
      UNEXP_DEFAULT();
    }

  case 2: {// std::atan2()
    auto n2 = args[1].get<Number*>();
    if(!n2){
      throw number_type_check_failed("atan", args[1]);
    }

    switch(n2->type()){
    case Number::Type::integer:
    case Number::Type::real:
      return {new Number(std::atan2(n1->coerce<Number::real_type>(),
                                    n2->coerce<Number::real_type>()))};
    case Number::Type::complex:
      throw zs_error("native func: (atan <complex> <complex>) is not implemented.\n");
    case Number::Type::uninitialized:
    default:
      UNEXP_DEFAULT();
    }
  }

  default:
    throw builtin_argcount_failed("atan", 1, 2, args.size());
  }
}

Lisp_ptr number_sqrt(){
  return number_unary_op("sqrt", sqrt_fun());
}


Lisp_ptr number_expt(){
  return number_binary_op("expt",
                          [](Number::real_type n1, Number::real_type n2){
                            return std::pow(n1, n2);
                          },
                          [](const Number::complex_type& n1, const Number::complex_type& n2){
                            return Lisp_ptr{new Number(std::pow(n1, n2))};
                          });
}

Lisp_ptr number_rect(){
  return number_binary_op("make-rectangular",
                          [](Number::real_type n1, Number::real_type n2){
                            return Number::complex_type(n1, n2);
                          },
                          complex_found());
}

Lisp_ptr number_polar(){
  return number_binary_op("make-polar",
                          [](Number::real_type n1, Number::real_type n2){
                            return polar(n1, n2);
                          },
                          complex_found());
}


Lisp_ptr number_real(){
  return number_unary_op_complex("real-part",
                                 [](const Number::complex_type& z){
                                   return z.real();
                                 });
}

Lisp_ptr number_imag(){
  return number_unary_op_complex("imag-part",
                                 [](const Number::complex_type& z){
                                   return z.imag();
                                 });
}

Lisp_ptr number_mag(){
  return number_unary_op_complex("magnitude",
                                 [](const Number::complex_type& z){
                                   return std::abs(z);
                                 });
}

Lisp_ptr number_angle(){
  return number_unary_op_complex("angle",
                                 [](const Number::complex_type& z){
                                   return arg(z);
                                 });
}


Lisp_ptr number_i_to_e(){
  return number_i_e("inexact->exact", to_exact);
}

Lisp_ptr number_e_to_i(){
  return number_i_e("exact->inexact", to_inexact);
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
    auto num = args[1].get<Number*>();
    if(!num){
      throw zs_error_arg1("string->number", "passed radix is not number", {args[1]});
    }
    if(num->type() != Number::Type::integer){
      throw zs_error_arg1("string->number", "passed radix is not integer", {args[1]});
    }
    radix = num->get<Number::integer_type>();
    break;
  }
  default:
    throw builtin_argcount_failed("string->number", 1, 2, args.size());
  }

  istringstream iss(*str);
  return {new Number{parse_number(iss, radix)}};
}

Lisp_ptr number_to_string(){
  ZsArgs args;

  auto n = args[0].get<Number*>();
  if(!n){
    throw zs_error_arg1("number->string", "passed arg is not number", {args[0]});
  }

  int radix;

  switch(args.size()){
  case 1:
    radix = 10;
    break;
  case 2: {
    auto num = args[1].get<Number*>();
    if(!num){
      throw zs_error_arg1("number->string", "passed radix is not number", {args[1]});
    }
    if(num->type() != Number::Type::integer){
      throw zs_error_arg1("number->string", "passed radix is not integer", {args[1]});
    }
    radix = num->get<Number::integer_type>();
    break;
  }
  default:
    throw builtin_argcount_failed("number->string", 1, 2, args.size());
  }

  ostringstream oss;
  print(oss, *n, radix);

  return {new String(oss.str())};
}
