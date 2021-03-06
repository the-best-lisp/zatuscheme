#ifndef EVAL_HH
#define EVAL_HH

#include "decl.hh"

namespace zs {

// primitives for syntax call
void vm_op_call();
void vm_op_proc_enter();
void vm_op_move_values();
void vm_op_leave_frame();
void vm_op_if();
void vm_op_set();
void vm_op_define();
void vm_op_raise();

// main loop
void start_evaluation();

// for debug
const char* stringify(VMop);
extern bool dump_mode;
extern unsigned instruction_counter;
extern const unsigned gc_invoke_interval;

} // namespace zs

#endif // EVAL_HH
