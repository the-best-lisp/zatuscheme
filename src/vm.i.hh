#ifndef VM_I_HH
#define VM_I_HH

#ifndef VM_HH
#error "Please include via parent file"
#endif

inline
void VM_t::enter_frame(Env* e){
  frame_history_.push(frame);
  frame = e;
  frame->add_ref();
}  

inline
void VM_t::leave_frame(){
  if(frame->release() <= 0){
    delete frame;
  }
  frame = frame_history_.top();
  frame_history_.pop();

}  

inline
Lisp_ptr VM_t::find(Symbol* s){
  return traverse(s, Lisp_ptr{});
}

inline
void VM_t::set(Symbol* s, Lisp_ptr p){
  auto old = traverse(s, p);
  if(!old) local_set(s, p);
}

#endif // VM_I_HH
