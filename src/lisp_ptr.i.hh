#ifndef LISP_PTR_I_HH
#define LISP_PTR_I_HH

#ifndef LISP_PTR_HH
#error "Please include via parent file"
#endif

#include <cassert>
#include <type_traits>

// Lisp_ptr constructors
inline constexpr
Lisp_ptr::Lisp_ptr()
 : tag_(Ptr_tag::undefined), u_(0){}

inline constexpr
Lisp_ptr::Lisp_ptr(bool b)
  : tag_(to_tag<bool>()), u_(b){}

inline constexpr
Lisp_ptr::Lisp_ptr(char c)
  : tag_(to_tag<char>()), u_(c){}

inline constexpr
Lisp_ptr::Lisp_ptr(int i)
  : tag_(to_tag<int>()), u_(i){}

template<typename T>
inline constexpr
Lisp_ptr::Lisp_ptr(T p)
  : tag_(to_tag<T>()), u_(p){
  static_assert(!std::is_fundamental<T>::value,
                "Lisp_ptr cannot accept the specified type.");
}

template<>
inline constexpr
Lisp_ptr::Lisp_ptr<Notation>(Notation n)
  : tag_(to_tag<Notation>()), u_(static_cast<int>(n)){}

// Lisp_ptr getters
template<>
inline
VMArgcount Lisp_ptr::get<VMArgcount>() const {
  assert(tag() == to_tag<VMArgcount>());
  return static_cast<VMArgcount>(u_.i_);
}

template<>
inline
Notation Lisp_ptr::get<Notation>() const {
  assert(tag() == to_tag<Notation>());
  return static_cast<Notation>(u_.i_);
}

template<>
inline constexpr
intptr_t Lisp_ptr::get<intptr_t>() const{
  return u_.i_;
}

template<>
inline constexpr
void* Lisp_ptr::get<void*>() const{
  return u_.ptr_;
}

template<>
inline constexpr
const void* Lisp_ptr::get<const void*>() const{
  return u_.ptr_;
}

template<>
inline constexpr
void (*Lisp_ptr::get<void(*)(void)>())(void) const{
  return u_.f_;
}

namespace lisp_ptr_detail {

template<typename T>
inline constexpr
T lisp_ptr_cast(const Lisp_ptr& p,
                typename std::enable_if<std::is_integral<T>::value>::type* = nullptr){
  return static_cast<T>(p.get<intptr_t>());
}

template<typename T>
inline constexpr
T lisp_ptr_cast(const Lisp_ptr& p,
                typename std::enable_if<std::is_pointer<T>::value>::type* = nullptr,
                typename std::enable_if<
                  std::is_function<typename std::remove_pointer<T>::type>::value
                  >::type* = nullptr){
  static_assert(std::is_same<T, void(*)(void)>::value,
                "inacceptable function-pointer type");
  return static_cast<T>(p.get<void(*)(void)>());
}

template<typename T>
inline constexpr
T lisp_ptr_cast(const Lisp_ptr& p,
                typename std::enable_if<std::is_pointer<T>::value>::type* = nullptr,
                typename std::enable_if<
                  !std::is_function<typename std::remove_pointer<T>::type>::value
                  >::type* = nullptr,
                typename std::enable_if<
                  !std::is_const<typename std::remove_pointer<T>::type>::value
                  >::type* = nullptr){
  return static_cast<T>(p.get<void*>());
}

template<typename T>
inline constexpr
T lisp_ptr_cast(const Lisp_ptr& p,
                typename std::enable_if<std::is_pointer<T>::value>::type* = nullptr,
                typename std::enable_if<
                  !std::is_function<typename std::remove_pointer<T>::type>::value
                  >::type* = nullptr,
                typename std::enable_if<
                  std::is_const<typename std::remove_pointer<T>::type>::value
                  >::type* = nullptr){
  return static_cast<T>(p.get<const void*>());
}

}

template<typename T>
inline
T Lisp_ptr::get() const {
  static_assert(to_tag<T>() != Ptr_tag::undefined, "inacceptable type");
  assert(tag() == to_tag<T>());
  return lisp_ptr_detail::lisp_ptr_cast<T>(*this);
}

#endif // LISP_PTR_I_HH
