/*
    SapecNG - Next Generation Symbolic Analysis Program for Electric Circuit
    Copyright (C) 2026, QSapecNG contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef EXACTSUM_H
#define EXACTSUM_H


#include <cmath>
#include <vector>


namespace sapecng
{


// A sum of doubles kept exactly (Shewchuk's non-overlapping partials, as
// Python's math.fsum): the result is the exact sum rounded once, so it does
// not depend on the order the terms are added in. The solver adds up the
// terms of each coefficient on several threads, in no fixed order.
class exact_sum
{

public:
  void add(double x)
  {
    plain_ += x;
    if(!std::isfinite(x) || overflow_) {
      overflow_ = true;
      return;
    }
    std::size_t i = 0;
    for(std::size_t k = 0; k < partials_.size(); ++k) {
      double y = partials_[k];
      if(std::fabs(x) < std::fabs(y)) {
        const double t = x;
        x = y;
        y = t;
      }
      const double hi = x + y;
      if(!std::isfinite(hi)) {
        overflow_ = true;
        return;
      }
      const double lo = y - (hi - x);
      if(lo != 0.)
        partials_[i++] = lo;
      x = hi;
    }
    partials_.resize(i + 1);
    partials_[i] = x;
  }

  void add(const exact_sum& other)
  {
    if(other.overflow_) {
      overflow_ = true;
      plain_ += other.plain_;
      return;
    }
    for(double y : other.partials_)
      add(y);
  }

  // the exact sum, rounded to the nearest double (ties to even)
  double value() const
  {
    if(overflow_)
      return plain_;   // an infinity or a NaN: no exact sum to give
    std::size_t n = partials_.size();
    if(n == 0)
      return 0.;
    double hi = partials_[--n];
    double lo = 0.;
    while(n > 0) {
      const double x = hi;
      const double y = partials_[--n];
      hi = x + y;
      const double yr = hi - x;
      lo = y - yr;
      if(lo != 0.)
        break;
    }
    // half-way between two doubles: the partials below decide
    if(n > 0 && ((lo < 0. && partials_[n - 1] < 0.)
        || (lo > 0. && partials_[n - 1] > 0.)))
    {
      const double y = lo * 2.;
      const double x = hi + y;
      const double yr = x - hi;
      if(y == yr)
        hi = x;
    }
    return hi;
  }

private:
  std::vector<double> partials_;
  double plain_ = 0.;      // used only after an overflow
  bool overflow_ = false;

};


}


#endif // EXACTSUM_H
