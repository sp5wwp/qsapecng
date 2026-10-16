// Regression test for issue #6 (segmentation fault on Resolve).
//
// metacircuit::compress() removes zero coefficients and then the powers of s
// left without terms. It used to erase a map entry and then advance the
// erased iterator, which crashed depending on the standard library.
// Built with _GLIBCXX_DEBUG (see tests/CMakeLists.txt) the old code aborts
// deterministically.

#include <iostream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include "model/circuit.h"

// simplify() is private; expose it for this test only.
#define private public
#include "model/metacircuit.h"
#undef private


int main()
{
  using M = sapecng::metacircuit;

  M::expression num, den;
  num[1].push_back(M::basic_expression(0., {"x"}));   // cancelled: must vanish
  num[2].push_back(M::basic_expression(2., {"y"}));
  den[0].push_back(M::basic_expression(1., {}));

  std::pair<M::expression, M::expression> p(num, den);
  M meta;
  meta.simplify(p);

  std::cout << M::as_string(p.first) << " / " << M::as_string(p.second) << "\n";

  if(p.first.size() != 1 || p.first.count(2) != 1) {
    std::cerr << "FAIL: expected only the s^2 term in the numerator\n";
    return 1;
  }
  return 0;
}
