#ifndef PRINTER_HH
#define PRINTER_HH

#include <iosfwd>
#include "lisp_ptr.hh"

enum class print_human_readable{ f, t };

void print(std::ostream&, Lisp_ptr,
           print_human_readable flag = print_human_readable::f,
           int radix = 10);

// for debug;
inline
std::ostream& operator<<(std::ostream& o, Lisp_ptr p){
  print(o, p, print_human_readable::t);
  return o;
}

#endif //PRINTER_HH
