#include "describe.hh"
#include <ostream>

using namespace std;

std::ostream& operator<<(std::ostream& o, Ptr_tag t){
  return (o << stringify(t));
}

// std::ostream& operator<<(std::ostream& o, Lisp_ptr p){
//   return (o << "[" << p.tag() << "] " << p.get<void*>());
// }

std::ostream& operator<<(std::ostream& o, const ProcInfo& info){
  return (o << "[required_args=" << info.required_args << ", max_args=" << info.max_args << "]");
}

std::ostream& operator<<(std::ostream& o, proc_flag::Variadic v){
  return (o << boolalpha << static_cast<bool>(v) << noboolalpha);
}

std::ostream& operator<<(std::ostream& o, Token::Type t){
  return (o << stringify(t));
}

std::ostream& operator<<(std::ostream& o, Token::Notation n){
  return (o << stringify(n));
}

std::ostream& operator<<(std::ostream& o, const Token& tok){
  const auto t = tok.type();

  o << "Token: " << t << "(";
  switch(t){
  case Token::Type::uninitialized:
    break;
  case Token::Type::identifier:
  case Token::Type::string:
    o << tok.get<string>();
    break;
  case Token::Type::boolean:
    o << boolalpha << tok.get<bool>() << noboolalpha;
    break;
  case Token::Type::integer:
    o << tok.get<int>();
    break;
  case Token::Type::rational: {
    auto q = tok.get<Rational>();
    o << q.numerator() << '/' << q.denominator();
    break;
  }
  case Token::Type::real:
    o << tok.get<double>();
    break;
  case Token::Type::complex:
    o << tok.get<Complex>();
    break;
  case Token::Type::character:
    o << tok.get<char>();
    break;
  case Token::Type::notation:
    o << tok.get<Token::Notation>();
    break;
  default:
    UNEXP_DEFAULT();
  }
  o << ')';

  return o;
}
