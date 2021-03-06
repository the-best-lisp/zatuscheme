#include <unordered_map>

#include "cons.hh"
#include "env.hh"
#include "procedure.hh"
#include "rational.hh"
#include "s_closure.hh"
#include "s_rules.hh"
#include "symbol.hh"
#include "zs_error.hh"
#include "zs_memory.hh"
#include "vm.hh"

using namespace std;

namespace zs {
namespace {

enum class MarkBit {
  unmarked = 0,
  marked = 1
};

struct MarkObj {
  Ptr_tag tag;
  MarkBit mark;
};

static unordered_map<void*, MarkObj> arena;

}

void zs_m_in(void* p, Ptr_tag tag){
  arena[p] = {tag, MarkBit::unmarked};
}

void zs_m_out(void* p){
  auto i = arena.find(p);
  if(i != end(arena)){
    arena.erase(i);
  }
}


// GC

namespace {

bool gc_is_marked_ptr(void* p){
  auto i = arena.find(p);
  return (i == end(arena)) || (i->second.mark != MarkBit::unmarked);
}

void gc_mark_ptr(void* p){
  auto i = arena.find(p);

  if(i != end(arena) && i->second.mark == MarkBit::unmarked){
    i->second.mark = MarkBit::marked;
  }
}
  
void gc_mark_lp(Lisp_ptr p);
    
void gc_mark_vm(VM* vm){
  for(auto i : vm->code){
    gc_mark_lp(i);
  }

  for(auto i : vm->stack){
    gc_mark_lp(i);
  }

  for(auto i : vm->return_value){
    gc_mark_lp(i);
  }

  for(auto i : vm->extent){
    gc_mark_lp(i.before);
    gc_mark_lp(i.thunk);
    gc_mark_lp(i.after);
  }
  
  gc_mark_env(vm->frame);

  for(auto i : vm->exception_handler){
    gc_mark_lp(i);
  }

  gc_mark_lp(vm->name);
}

} // namespace

void gc_mark_env(Env* e){
  if(!e) return;
  if(gc_is_marked_ptr(e)) return;

  gc_mark_ptr(e);
  for(auto i : e->map_){
    gc_mark_lp(i.first);
    gc_mark_lp(i.second);
  }
  gc_mark_env(e->next_);
}

namespace {

void gc_mark_lp(Lisp_ptr p){
  if(!p.get<void*>()) return;

  if(gc_is_marked_ptr(p.get<void*>()))
    return;

  switch(p.tag()){
    // included in Lisp_ptr
  case Ptr_tag::undefined:
  case Ptr_tag::boolean:
  case Ptr_tag::character:
  case Ptr_tag::integer:
  case Ptr_tag::vm_argcount:
  case Ptr_tag::notation:
    break;

    // no dynamic allocation
  case Ptr_tag::n_procedure:
  case Ptr_tag::vm_op:
    break;

    // not container
  case Ptr_tag::symbol:
  case Ptr_tag::rational:
  case Ptr_tag::real:
  case Ptr_tag::complex:
  case Ptr_tag::string:
  case Ptr_tag::input_port:
  case Ptr_tag::output_port:
    gc_mark_ptr(p.get<void*>());
    break;

    // container
  case Ptr_tag::cons:
    if(auto c = p.get<Cons*>()){
      gc_mark_ptr(c);

      gc_mark_lp(c->car);
      gc_mark_lp(c->cdr);
    }
    break;

  case Ptr_tag::i_procedure: {
    auto iproc = p.get<IProcedure*>();
    gc_mark_ptr(iproc);

    gc_mark_lp(iproc->arg_list());
    gc_mark_lp(iproc->get());
    gc_mark_env(iproc->closure());
    gc_mark_lp(iproc->name());
    break;
  }

  case Ptr_tag::continuation: {
    auto c = p.get<VM*>();
    gc_mark_ptr(c);

    gc_mark_vm(c);
    break;
  }

  case Ptr_tag::vector: {
    auto v = p.get<Vector*>();
    gc_mark_ptr(v);

    for(auto i : *v){
      gc_mark_lp(i);
    }
    break;
  }

  case Ptr_tag::env:
    gc_mark_env(p.get<Env*>());
    break;

  case Ptr_tag::syntactic_closure: {
    auto sc = p.get<SyntacticClosure*>();
    gc_mark_ptr(sc);

    gc_mark_env(sc->env());
    gc_mark_lp(sc->free_names());
    gc_mark_lp(sc->expr());
    break;
  }

  case Ptr_tag::syntax_rules: {
    auto sr = p.get<SyntaxRules*>();
    gc_mark_ptr(sr);

    gc_mark_env(sr->env());
    gc_mark_lp(sr->literals());
    gc_mark_lp(sr->rules());
    break;
  }

  default:
    break;
  }
}

template<Ptr_tag tag>
void gc_tagged_delete(void* p){
  typedef typename to_type<tag>::type TargetT;
  delete static_cast<TargetT>(p);
}

void gc_sweep(){
  auto i = begin(arena), e = end(arena);
  while(i != e){
    auto ii = next(i);
    switch(i->second.mark){
    case MarkBit::unmarked:
      switch(i->second.tag){
      case Ptr_tag::undefined:
        break;
      case Ptr_tag::boolean:
        break;
      case Ptr_tag::character:
        break;
      case Ptr_tag::cons:
        gc_tagged_delete<Ptr_tag::cons>(i->first);
        break;
      case Ptr_tag::symbol: 
        gc_tagged_delete<Ptr_tag::symbol>(i->first);
        break;
      case Ptr_tag::i_procedure: 
        gc_tagged_delete<Ptr_tag::i_procedure>(i->first);
        break;
      case Ptr_tag::n_procedure:
        break;
      case Ptr_tag::continuation: 
        gc_tagged_delete<Ptr_tag::continuation>(i->first);
        break;
      case Ptr_tag::integer: 
        break;
      case Ptr_tag::rational: 
        gc_tagged_delete<Ptr_tag::rational>(i->first);
        break;
      case Ptr_tag::real: 
        gc_tagged_delete<Ptr_tag::real>(i->first);
        break;
      case Ptr_tag::complex: 
        gc_tagged_delete<Ptr_tag::complex>(i->first);
        break;
      case Ptr_tag::string: 
        gc_tagged_delete<Ptr_tag::string>(i->first);
        break;
      case Ptr_tag::vector: 
        gc_tagged_delete<Ptr_tag::vector>(i->first);
        break;
      case Ptr_tag::input_port: 
        gc_tagged_delete<Ptr_tag::input_port>(i->first);
        break;
      case Ptr_tag::output_port: 
        gc_tagged_delete<Ptr_tag::output_port>(i->first);
        break;
      case Ptr_tag::env: 
        gc_tagged_delete<Ptr_tag::env>(i->first);
        break;
      case Ptr_tag::syntactic_closure: 
        gc_tagged_delete<Ptr_tag::syntactic_closure>(i->first);
        break;
      case Ptr_tag::syntax_rules: 
        gc_tagged_delete<Ptr_tag::syntax_rules>(i->first);
        break;
      case Ptr_tag::vm_op: 
        break;
      case Ptr_tag::vm_argcount:
        break;
      case Ptr_tag::notation:
        break;
      default:
        break;
      }
      arena.erase(i);
      break;
    case MarkBit::marked:
      i->second.mark = MarkBit::unmarked;
      break;
    default:
      break;
    }
    i = ii;
  }
}

} // namespace

void gc(){
  gc_mark_vm(&vm);
  gc_sweep();
}

} // namespace zs
