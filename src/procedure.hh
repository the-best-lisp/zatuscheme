#ifndef PROCEDURE_HH
#define PROCEDURE_HH

#include <utility>
#include <limits>
#include "lisp_ptr.hh"
#include "env.hh"
#include "vm.hh"

typedef Lisp_ptr(*NativeFunc)(ZsArgs);

namespace proc_flag {
  enum class Variadic : bool { f = false, t = true };

  enum class Passing : unsigned char{
    eval, quote, whole
  };

  enum class Returning : unsigned char{
    pass, code, stack_splice
  };

  enum class MoveReturnValue : bool { f = false, t = true };

  enum class Leaving : unsigned char {
    immediate, after_returning_op
  };
}

struct ProcInfo {
  int required_args;
  int max_args;
  proc_flag::Passing passing;
  proc_flag::Returning returning;
  proc_flag::MoveReturnValue move_ret;
  proc_flag::Leaving leaving;

  constexpr ProcInfo(int rargs,
                     int margs,
                     proc_flag::Passing p = proc_flag::Passing::eval,
                     proc_flag::Returning r = proc_flag::Returning::pass,
                     proc_flag::MoveReturnValue m = proc_flag::MoveReturnValue::t,
                     proc_flag::Leaving l = proc_flag::Leaving::immediate)
    : required_args(rargs),
      max_args(margs),
      passing(p),
      returning(r),
      move_ret(m),
      leaving(l){}

  // TODO: use delegating constructor
  constexpr ProcInfo(int rargs,
                     proc_flag::Variadic v = proc_flag::Variadic::f,
                     proc_flag::Passing p = proc_flag::Passing::eval,
                     proc_flag::Returning r = proc_flag::Returning::pass,
                     proc_flag::MoveReturnValue m = proc_flag::MoveReturnValue::t,
                     proc_flag::Leaving l = proc_flag::Leaving::immediate)
    : required_args(rargs),
      max_args((v == proc_flag::Variadic::t) ? std::numeric_limits<decltype(max_args)>::max() : rargs),
      passing(p),
      returning(r),
      move_ret(m),
      leaving(l){}
};

// static_assert(sizeof(ProcInfo) == (sizeof(int) + sizeof(int) + sizeof(int)),
//               "ProcInfo became too big!!");

std::pair<int, proc_flag::Variadic> parse_func_arg(Lisp_ptr);

class IProcedure{
public:
  IProcedure(Lisp_ptr code, const ProcInfo& pi, Lisp_ptr al, Env* e, Lisp_ptr n)
    : info_(pi), code_(code), arg_list_(al),  env_(e), name_(n){}

  IProcedure(const IProcedure&) = default;
  IProcedure(IProcedure&&) = default;

  ~IProcedure(){}
  
  IProcedure& operator=(const IProcedure&) = default;
  IProcedure& operator=(IProcedure&&) = default;

  const ProcInfo* info() const
  { return &info_; }

  Lisp_ptr arg_list() const
  { return arg_list_; }

  Lisp_ptr get() const
  { return code_; }

  Env* closure() const
  { return env_; }
  
  Lisp_ptr name() const
  { return name_; }
  
private:
  ProcInfo info_;
  Lisp_ptr code_;
  Lisp_ptr arg_list_;
  Env* env_;
  Lisp_ptr name_;
};

class NProcedure{
public:
  constexpr NProcedure(NativeFunc f, const ProcInfo& pi)
    : info_(pi), n_func_(f){}

  NProcedure(const NProcedure&) = default;
  NProcedure(NProcedure&&) = default;

  ~NProcedure() = default;
  
  NProcedure& operator=(const NProcedure&) = default;
  NProcedure& operator=(NProcedure&&) = default;

  const ProcInfo* info() const
  { return &info_; }

  NativeFunc get() const
  { return n_func_; }

private:
  const ProcInfo info_;
  const NativeFunc n_func_;
};

class Continuation{
public:
  Continuation(const VM&);

  Continuation(const Continuation&) = delete;
  Continuation(Continuation&&) = delete;

  ~Continuation();
  
  Continuation& operator=(const Continuation&) = delete;
  Continuation& operator=(Continuation&&) = delete;

  const ProcInfo* info() const
  { return &cont_procinfo; }

  const VM& get() const
  { return vm_; }

private:
  static constexpr ProcInfo cont_procinfo = ProcInfo{0, proc_flag::Variadic::t};
  const VM vm_;
};

const ProcInfo* get_procinfo(Lisp_ptr);
Lisp_ptr get_procname(Lisp_ptr);

#endif //PROCEDURE_HH
