#include "zs.hh"
#include "describe.hh"
#include "test_util.hh"

using namespace std;
using namespace zs;

template<typename T>
void fail_message(istream& i, Lisp_ptr p, T expect){
  RESULT = EXIT_FAILURE;

  string buf;

  i.clear();
  i.seekg(0, ios_base::beg);
  std::getline(i, buf);

  cerr << "[failed] input='" << buf << "', expect type='" << stringify(p.tag()) << "'"
       << ", expected = '" << expect << "'\n"
       << "\tgotten: " << p << '\n';
}

void check(const char* input){
  stringstream ss(input);

  with_expect_error([&]() -> void {
      auto n = parse_number(ss);
      if(!n){
        fail_message(ss, n, "(uninitialized)");
      }
    });
}

template<typename T>
void check(const char* input, const T& expect){
  stringstream ss(input);
  auto n = parse_number(ss);
  
  if(!equal(n, wrap_number(expect))){
    fail_message(ss, n, expect);
  }
}

// printing test
void check(int i, int radix, const char* expect){
  stringstream ss;
  print(ss, Lisp_ptr{i}, PrintReadable::f, radix);

  if(ss.str() != expect){
    cerr << "[failed] printed " << ss.str() << ", expected " << expect << "\n";
    RESULT = EXIT_FAILURE;
  }
}

int main(){
  // invalids
  check("hogehoge");
  check(".");

  // int
  check("100", 100);
  check("-100", -100);
  check("1##", static_cast<double>(100));

  check("#b10", 2);
  check("#b-10", -2);
  check("#o10", 8);
  check("#o-10", -8);
  check("#x10", 16);
  check("#x-10", -16);
  check("#x9abcdef", 0x9abcdef);

  // float
  check("-1.1", -1.1);
  check("1.", 1.0);
  check(".1", 0.1);
  check("3.14159265358979e0", 3.14159265358979e0);
  check("0.6s0", 0.6e0);
  check(".1f10", 0.1e10);
  check("3.d2", 3e2);
  check("3#.l-3", 30e-3);


  check("#b1.0");
  check("#o1.0");
  check("#x1.0");
  check("#d1.0", 1.0);

  // complex
  check("1.0+1i", Complex(1, 1));
  check("-2.5+0.0i", Complex(-2.5, 0));
  check("1.0@3", polar(1.0, 3.0));
  check("1.0i");
  check("+1.0i", Complex(0, 1.0));
  check("+1i", Complex(0, 1));
  check("+i", Complex(0, 1));

  // prefix
  check("#e1", 1);
  check("#i1", 1.0);
  check("#e1.0", 1);
  check("#i1.0", 1.0);
  check("#e1.0i");
  check("#i-1.0i", Complex(0, -1.0));

  check("#o#e10", 8);
  check("#i#x10", 16.0);

  check("#x#x10");
  check("#i#e1");

  // printing test
  check(100, 10, "100");
  check(100, 8, "#o144");
  check(100, 16, "#x64");
  check(100, 2, "#b1100100");

  check(-100, 10, "-100");
  check(-100, 8, "#o-144");
  check(-100, 16, "#x-64");
  check(-100, 2, "#b-1100100");

  return RESULT;
}
