// [ QSapecNG ] progress and cancelling of a running mrt enumeration.
// Part of QSapecNG, GPL v3 or later.

#ifndef MRT_PROGRESS_H
#define MRT_PROGRESS_H

#include <atomic>
#include <exception>


namespace boost
{


// [ QSapecNG ] progress and cancelling of a running enumeration, for the
// thread that runs it. The search is a binary tree of choices (an edge in
// or out): each call gets a share of the whole, half for each of its two
// sub-searches, and adds what it does not hand on when it is done. The sum
// rises steadily to 1, whatever the number of trees.
struct mrt_progress
{
  double done = 0.;
  double weight = 1.;
  std::atomic<bool>* cancel = nullptr;
  std::atomic<int>* permille = nullptr;   // done, in 1/1000 (read elsewhere)
};

struct mrt_cancelled: public std::exception
{
  const char* what() const noexcept { return "cancelled"; }
};

inline mrt_progress*& mrt_current_progress()
{
  static thread_local mrt_progress* p = nullptr;
  return p;
}


}


#endif // MRT_PROGRESS_H
