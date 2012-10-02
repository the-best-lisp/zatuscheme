#include "env.hh"
#include <utility>

using namespace std;

Env::Env(Env* e)
  : map_(), next_(e), refcnt_(0){
  if(next_) next_->add_ref();
}

Env::~Env(){
  if(next_ && next_->release() <= 0){
    delete next_;
  }
}

// void Env::steal(Env* new_next){
//   map_.clear();

//   if(next_ && next_->release() <= 0){
//     delete next_;
//   }
//   next_ = new_next;
//   if(next_) next_->add_ref();
// }

Lisp_ptr Env::traverse(Symbol* s, Lisp_ptr p){
  Lisp_ptr old = {};

  for(Env* e = this; e; e = e->next_){
    auto ei = e->map_.find(s);
    if(ei != e->map_.end()){
      old = ei->second;
      if(p){
        e->map_.erase(ei);
        e->map_.insert({s, p});
      }
      break;
    }
  }

  return old;
}

void Env::local_set(Symbol* s, Lisp_ptr p){
  auto it = map_.find(s);
  if(it != map_.end()) map_.erase(it);

  map_.insert({s, p});
}

Env* Env::push(){
  return new Env{this};
}