#include "builtin_vector.hh"
#include "lisp_ptr.hh"
#include "vm.hh"
#include "eval.hh"
#include "zs_error.hh"
#include "cons_util.hh"
#include "zs_memory.hh"

using namespace std;

namespace builtin {

Lisp_ptr vector_make(ZsArgs args){
  if(args[0].tag() != Ptr_tag::integer){
    throw builtin_type_check_failed(nullptr, Ptr_tag::integer, args[0]);
  }

  auto count = args[0].get<int>();
  if(count < 0){
    throw zs_error("passed size is less than 0");
  }    

  switch(args.size()){
  case 1:
    return {zs_new<Vector>(count, Lisp_ptr{})};
  case 2:
    return {zs_new<Vector>(count, args[1])};
  default:
    throw builtin_argcount_failed(nullptr, 1, 2, args.size());
  }
}

Lisp_ptr vector_vector(ZsArgs args){
  return {zs_new<Vector>(args.begin(), args.end())};
}

Lisp_ptr vector_length(ZsArgs args){
  auto v = args[0].get<Vector*>();
  if(!v){
    throw builtin_type_check_failed(nullptr, Ptr_tag::vector, args[0]);
  }

  // TODO: add range check, and remove cast
  return Lisp_ptr{Ptr_tag::integer,
      static_cast<int>(v->size())};
}

Lisp_ptr vector_ref(ZsArgs args){
  auto v = args[0].get<Vector*>();
  if(!v){
    throw builtin_type_check_failed(nullptr, Ptr_tag::vector, args[0]);
  }

  if(args[1].tag() != Ptr_tag::integer){
    throw builtin_type_check_failed(nullptr, Ptr_tag::integer, args[1]);
  }
  auto ind = args[1].get<int>();

  if(ind < 0 || ind >= static_cast<signed>(v->size())){
    throw zs_error(printf_string("index is out-of-bound ([0, %ld), supplied %d",
                                 v->size(), ind));
  }

  return (*v)[ind];
}

Lisp_ptr vector_set(ZsArgs args){
  auto v = args[0].get<Vector*>();
  if(!v){
    throw builtin_type_check_failed(nullptr, Ptr_tag::vector, args[0]);
  }

  if(args[1].tag() != Ptr_tag::integer){
    throw builtin_type_check_failed(nullptr, Ptr_tag::integer, args[1]);
  }
  auto ind = args[1].get<int>();

  if(ind < 0 || ind >= static_cast<signed>(v->size())){
    throw zs_error(printf_string("index is out-of-bound ([0, %ld), supplied %d",
                                 v->size(), ind));
  }

  (*v)[ind] = args[2];
  return args[2];
}

} // namespace builtin
