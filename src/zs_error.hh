#ifndef ZS_ERROR_HH
#define ZS_ERROR_HH

#include <string>
#include <exception>
#include <initializer_list>
#include <cstdlib>
#include <cassert>
#include "decl.hh"
#include "lisp_ptr.hh"

std::string printf_string(const char*, ...)
  __attribute__ ((format (printf, 1, 2)))
  ;

// error classes
class zs_error : public std::exception{
protected:
  explicit zs_error();

public:
  explicit zs_error(const std::string&);
  explicit zs_error(std::string&&);
  zs_error(const zs_error&);
  zs_error(zs_error&&);

  virtual ~zs_error() noexcept;

  zs_error& operator=(const zs_error&) noexcept;
  zs_error& operator=(zs_error&&) noexcept;

  virtual const char* what() const noexcept; // override

protected:
  std::string str_;
};

// error functions
#define UNEXP_DEFAULT() do{\
    assert(((void)"unexpected default case!", 0));      \
    abort();\
  }while(0)

#define UNEXP_CONVERSION(to) do{\
    assert(((void)"unexpected conversion to "to"!", 0));\
    abort();\
  }while(0)

Lisp_ptr zs_error_arg1(const char* context, const std::string& str,
                       std::initializer_list<Lisp_ptr>);
Lisp_ptr zs_error_arg1(const char* context, const std::string& str);

Lisp_ptr builtin_type_check_failed(const char*, Ptr_tag, Lisp_ptr);
Lisp_ptr builtin_argcount_failed(const char*, int required, int max, int passed);
Lisp_ptr builtin_identifier_check_failed(const char*, Lisp_ptr);
zs_error builtin_range_check_failed(int max, int passed);

#endif // ZS_ERROR_HH
