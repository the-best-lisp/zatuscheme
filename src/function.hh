#ifndef FUNCTION_HH
#define FUNCTION_HH

#include "lisp_ptr.hh"
class Env;
class Stack;

class Function {
public:
  typedef Lisp_ptr(*NativeFunc)(Env&, Stack&);

  enum class Type {
    interpreted,
    native
  };

  struct ArgInfo {
    const Lisp_ptr head;
    const int required_args;
    const bool variadic;

    constexpr ArgInfo()
      : head({}), required_args(-1), variadic(false){}
    constexpr ArgInfo(Lisp_ptr h, int rargs, bool v)
      : head(h), required_args(rargs), variadic(v){}

    explicit operator bool() const{
      return (head) && (required_args < 0);
    }
  };

  explicit Function(Lisp_ptr code, const ArgInfo& a)
    : type_(Type::interpreted), argi_(a), code_(code){}
  explicit constexpr Function(NativeFunc f, const ArgInfo& a)
    : type_(Type::native), argi_(a), n_func_(f){}

  Function(const Function&) = default;
  Function(Function&&) = default;

  ~Function() = default;
  
  Function& operator=(const Function&) = default;
  Function& operator=(Function&&) = default;


  const Type& type() const
  { return type_; }

  const ArgInfo& arg_info() const
  { return argi_; }

  template<typename T>
  T get() const;
  
private:
  const Type type_;
  const ArgInfo argi_;
  union{
    Lisp_ptr code_;
    NativeFunc n_func_;
  };
};

Function::ArgInfo parse_func_arg(Lisp_ptr);

const char* stringify(Function::Type);

#include "function.i.hh"

#endif //FUNCTION_HH