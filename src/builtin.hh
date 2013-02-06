#ifndef BUILTIN_HH
#define BUILTIN_HH

#include "decl.hh"
#include "procedure.hh"

void install_builtin();

// finding NProcedure with name
const NProcedure* find_builtin_nproc(const char*);
const char* find_builtin_nproc_name(const NProcedure*);

// builtin func struct
struct BuiltinNProc {
  const char* name;
  const NProcedure func;

  // this constructor is required for static initialization
  constexpr BuiltinNProc(const char* n, const NProcedure& f)
    : name(n), func(f){};
};

struct BuiltinIProc {
  const char* name;
  const char* arg_list;
  const char* body;

  // this constructor is required for static initialization
  constexpr BuiltinIProc(const char* n, const char* a, const char* b)
    : name(n), arg_list(a), body(b){};
};

// LOAD from C++ world
void load(InputPort*);

#endif // BUILTIN_HH
