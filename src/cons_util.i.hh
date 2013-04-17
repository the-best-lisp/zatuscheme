#ifndef CONS_UTIL_I_HH
#define CONS_UTIL_I_HH

#ifndef CONS_UTIL_HH
#error "Please include via parent file"
#endif

#include <utility>
#include <algorithm>
#include <type_traits>
#include "zs_error.hh"
#include "equality.hh"

inline
bool nullp(Lisp_ptr p){
  // When Lisp_ptr::get<Cons*>() became constexpr,
  // '<void*>' should be replaced with '<Cons*>'
  static_assert(Cons::NIL.get<void*>() == nullptr,
                "NIL's pointer part is not nullptr!");
  return (p.tag() == Ptr_tag::cons)
    && (p.get<void*>() == nullptr);
}

// experimental third version
template<typename T>
inline
T typed_destruct_cast(ConsIter i){
  return (*i).get<T>();
}

template<>
inline
Lisp_ptr typed_destruct_cast(ConsIter i){
  return (i) ? (*i) : Lisp_ptr{};
}

template<>
inline
ConsIter typed_destruct_cast(ConsIter i){
  return i;
}

enum class typed_destruct_mode {
  loose, strict, strict_variadic
};

constexpr bool is_strict(typed_destruct_mode mode){
  return (mode != typed_destruct_mode::loose);
}

constexpr typed_destruct_mode to_variadic(typed_destruct_mode mode){
  return (mode == typed_destruct_mode::strict) ? typed_destruct_mode::strict_variadic : mode;
}

template<typed_destruct_mode mode, typename... F_Args>
struct typed_destruct;

template<typed_destruct_mode mode, typename F_Arg1, typename... F_Args>
struct typed_destruct<mode, F_Arg1, F_Args...>{
  typedef typed_destruct<mode, F_Args...> NextF;
  typedef typed_destruct<to_variadic(mode), F_Args...> NextV;

  // if Iter != F_Arg1 :: fixed args
  template<typename Iter, typename Fun, typename... Args>
  auto operator()(Iter b, Iter e, Fun f, Args... args) const
    -> decltype(NextF()(b, e, f, args..., F_Arg1()))
  {
    if(is_strict(mode) && (b == e)){
      throw zs_error(printf_string("eval internal error: cons list is shorter(%lu) than expected(%lu)\n",
                                   sizeof...(Args),
                                   sizeof...(F_Args) + 1 + sizeof...(Args)));
    }

    auto arg1 = typed_destruct_cast<F_Arg1>(b);
    if(!std::is_convertible<decltype(*b), F_Arg1>::value && !arg1){
      throw zs_error("eval internal error: cons list has unexpected object\n");
    }

    return NextF()(++b, e, f, args..., arg1);
  }

  // if Iter == F_Arg1 :: variadic args
  template<typename Fun, typename... Args>
  auto operator()(F_Arg1 b, F_Arg1 e, Fun f, Args... args) const
    -> decltype(NextV()(b, e, f, args..., F_Arg1()))
  {
    auto arg1 = b;
    return NextV()(++b, e, f, args..., arg1);
  }
};

template<typed_destruct_mode mode>
struct typed_destruct<mode>{
  template<typename Iter, typename Fun, typename... Args>
  auto operator()(Iter b, Iter e, Fun f, Args... args) const
    -> decltype(f(args...))
  {
    if((mode == typed_destruct_mode::strict) && (b != e)){
      throw zs_error(printf_string("eval internal error: cons list is longer than expected(%lu)\n",
                                   sizeof...(Args)));
    }

    return f(args...);
  }
};

// http://stackoverflow.com/questions/6512019/can-we-get-the-type-of-a-lambda-argument
template<typed_destruct_mode mode,
         typename Iter, typename Fun, typename Ret, typename... Args>
Ret entry_typed_destruct(Iter b, Iter e, Fun fun, Ret (Fun::*)(Args...)){
  return typed_destruct<mode, Args...>()(b, e, fun);
}
  
template<typed_destruct_mode mode,
         typename Iter, typename Fun, typename Ret, typename... Args>
Ret entry_typed_destruct(Iter b, Iter e, Fun fun, Ret (Fun::*)(Args...) const){
  return typed_destruct<mode, Args...>()(b, e, fun);
}
  
template<typed_destruct_mode mode,
         typename Iter, typename Fun, typename Ret, typename... Args>
Ret entry_typed_destruct(Iter b, Iter e, Fun fun, Ret (*)(Args...)){
  return typed_destruct<mode, Args...>()(b, e, fun);
}

template<typename Fun>
auto bind_cons_list_loose(Lisp_ptr p, Fun fun)
  -> decltype(entry_typed_destruct<typed_destruct_mode::loose>
              (ConsIter(), ConsIter(), fun, &Fun::operator()))
{
  return entry_typed_destruct<typed_destruct_mode::loose>
    (begin(p), end(p), fun, &Fun::operator());
}

template<typename Fun>
auto bind_cons_list_strict(Lisp_ptr p, Fun fun)
  -> decltype(entry_typed_destruct<typed_destruct_mode::strict>
              (ConsIter(), ConsIter(), fun, &Fun::operator()))
{
  return entry_typed_destruct<typed_destruct_mode::strict>
    (begin(p), end(p), fun, &Fun::operator());
}
  

// make_cons_list 
template<typename Iter>
Lisp_ptr make_cons_list(Iter b, Iter e){
  if(b == e){
    return Cons::NIL;
  }

  auto i = b;
  GrowList gw;

  while(1){
    gw.push(Lisp_ptr{*i});

    ++i;
    if(i == e) break;
  }

  return gw.extract();
}

inline
Lisp_ptr make_cons_list(std::initializer_list<Lisp_ptr> lis){
  return make_cons_list(begin(lis), end(lis));
}

inline
Lisp_ptr push_cons_list(Lisp_ptr p, Lisp_ptr q){
  return Lisp_ptr(new Cons(p, q));
}


// nth family
template<unsigned n>
constexpr
Lisp_ptr nth_cons_list(Lisp_ptr p){
  // This cast is for telling a type to the compiler. 
  return static_cast<Lisp_ptr>(nthcdr_cons_list<n>(p))
    .get<Cons*>()->car();
}

template<>
constexpr
Lisp_ptr nthcdr_cons_list<0u>(Lisp_ptr p){
  return p;
}

template<unsigned n>
constexpr
Lisp_ptr nthcdr_cons_list(Lisp_ptr p){
  return nthcdr_cons_list<n-1>(p.get<Cons*>()->cdr());
}


// GrowList class
inline
void GrowList::invalidate(){
  assert(head && next);
  head = {};
  next = nullptr;
}  

inline
GrowList::GrowList()
  : head(Cons::NIL), next(&head)
{}

inline
Lisp_ptr GrowList::extract(){
  return extract_with_tail(Cons::NIL);
}

inline
Lisp_ptr GrowList::extract_with_tail(Lisp_ptr p){
  *next = p;
  auto ret = head;
  invalidate();
  return ret;
}


// ConsIter class
inline
bool operator==(const ConsIter& i1, const ConsIter& i2){
  return eq_internal(i1.base(), i2.base());
}

inline
bool operator!=(const ConsIter& i1, const ConsIter& i2){
  return !eq_internal(i1.base(), i2.base());
}

#endif //CONS_UTIL_I_HH
