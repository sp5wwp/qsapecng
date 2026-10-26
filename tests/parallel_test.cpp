// Solves circuits on 1 to 8 threads (the pool the GUI uses) and checks the
// result is the same, term for term, as without a pool. Cancelling is
// checked too.
//
// usage: parallel_test <circuit.info|circuit.crc>...
//        parallel_test grid <n>     (an n x n RC grid, timed)
//        parallel_test exactsum     (the exact sums of the coefficients)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#include "boost-sapecng/mrt_pool.hpp"
#include "boost-sapecng/mrt_progress.hpp"
#include "model/circuit.h"
#include "model/exactsum.h"
#include "model/metacircuit.h"
#include "parser/crc_circuit.h"
#include "parser/ir_circuit.h"


namespace
{

std::string text(const sapecng::metacircuit& meta)
{
  using M = sapecng::metacircuit;
  return M::as_string(meta.raw().first) + " / "
    + M::as_string(meta.raw().second) + "\n"
    + M::as_string(meta.digit().first) + " / "
    + M::as_string(meta.digit().second) + "\n"
    + M::as_string(meta.mixed().first) + " / "
    + M::as_string(meta.mixed().second);
}

void load(const std::string& name, const std::string& data,
  sapecng::circuit& circ)
{
  std::istringstream in(data);
  std::unique_ptr<sapecng::abstract_parser> parser;
  if(name.size() > 4 && name.substr(name.size() - 4) == ".crc")
    parser.reset(new sapecng::crc_parser(in));
  else
    parser.reset(new sapecng::info_parser(in));
  sapecng::circuit_builder builder(circ);
  parser->parse(builder);
}

// threads == 0: no pool at all (as the command line tools)
std::string solve(const std::string& name, const std::string& data,
  unsigned threads, double* seconds = nullptr, int* permille = nullptr)
{
  sapecng::circuit circ;
  load(name, data, circ);
  sapecng::metacircuit meta;

  const auto start = std::chrono::steady_clock::now();
  if(threads == 0) {
    meta(circ);
  } else {
    boost::mrt_pool pool(threads);
    std::atomic<bool> cancel(false);
    std::atomic<int> done(0);
    std::atomic<long long> total(0);
    boost::mrt_progress progress;
    progress.cancel = &cancel;
    progress.permille = &done;
    progress.pool = &pool;
    progress.total = &total;
    boost::mrt_progress_scope scope(&progress);
    meta(circ);
    progress.flush();
    if(permille)
      *permille = done.load();
  }
  if(seconds)
    *seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
  return text(meta);
}

// cancelled half-way, every thread stops and the solve throws
bool cancels(const std::string& name, const std::string& data)
{
  sapecng::circuit circ;
  load(name, data, circ);
  sapecng::metacircuit meta;
  boost::mrt_pool pool(4);
  std::atomic<bool> cancel(false);
  std::atomic<int> done(0);
  std::atomic<long long> total(0);
  boost::mrt_progress progress;
  progress.cancel = &cancel;
  progress.permille = &done;
  progress.pool = &pool;
  progress.total = &total;
  std::thread stopper([&]() {
      while(done.load() < 100)
        std::this_thread::yield();
      cancel.store(true);
    });
  bool thrown = false;
  try {
    boost::mrt_progress_scope scope(&progress);
    meta(circ);
  } catch(const boost::mrt_cancelled&) {
    thrown = true;
  }
  cancel.store(true);
  done.store(1000);
  stopper.join();
  return thrown;
}

// n x n nodes, R between neighbours, C from each node to ground; the
// source at one corner, the output at the other
std::string grid(int n)
{
  std::ostringstream out;
  auto node = [n](int r, int c) { return 1 + r * n + c; };
  out << "V1 " << node(0, 0) << " 0 1 1\n";
  int k = 0;
  for(int r = 0; r < n; ++r)
    for(int c = 0; c < n; ++c) {
      if(c + 1 < n)
        out << "R" << ++k << " " << node(r, c) << " " << node(r, c + 1)
          << " 1000 1\n";
      if(r + 1 < n)
        out << "R" << ++k << " " << node(r, c) << " " << node(r + 1, c)
          << " 1000 1\n";
      if(r + c > 0)
        out << "C" << node(r, c) << " " << node(r, c) << " 0 1e-6 1\n";
    }
  out << ".OUT " << node(n - 1, n - 1) << "\n.END\n";
  return out.str();
}

}


// the exact sum: rounded once, whatever the order of the terms
bool exact_sums()
{
  bool ok = true;
  auto sum = [](const std::vector<double>& v) {
      sapecng::exact_sum s;
      for(double x : v)
        s.add(x);
      return s.value();
    };
  ok = ok && sum({ 1e16, 1., -1e16 }) == 1.;
  ok = ok && sum(std::vector<double>(10, 0.1)) == 1.;
  ok = ok && sum({ 1e308, 1e308, -1e308 }) != 0.;   // overflow: no crash
  std::mt19937 rng(5);
  std::uniform_real_distribution<double> mantissa(-1., 1.);
  std::uniform_int_distribution<int> exponent(-30, 30);
  for(int round = 0; round < 200 && ok; ++round) {
    std::vector<double> v(1 + round * 7);
    for(double& x : v)
      x = std::ldexp(mantissa(rng), exponent(rng));
    const double a = sum(v);
    std::shuffle(v.begin(), v.end(), rng);
    const double b = sum(v);
    // two halves summed apart, then together (as parts of a solve)
    sapecng::exact_sum h1, h2;
    for(std::size_t i = 0; i < v.size(); ++i)
      (i % 2 ? h1 : h2).add(v[i]);
    h1.add(h2);
    ok = a == b && a == h1.value();
  }
  std::cout << "exact sums: " << (ok ? "ok" : "FAILED") << "\n";
  return ok;
}


// a symbolic result over the term limit: given up with too_large (one
// thread and four), and solved under a limit it fits in
bool term_limit()
{
  const std::string data = grid(3);
  bool ok = true;
  for(unsigned threads : { 0u, 4u }) {
    for(long long limit : { 50LL, 1000000LL }) {
      sapecng::circuit circ;
      load("grid.crc", data, circ);
      sapecng::metacircuit meta;
      std::unique_ptr<boost::mrt_pool> pool;
      if(threads)
        pool.reset(new boost::mrt_pool{ threads });
      std::atomic<bool> cancel(false);
      std::atomic<long long> total(0);
      boost::mrt_progress progress;
      progress.cancel = &cancel;
      progress.pool = pool.get();
      progress.total = threads ? &total : nullptr;
      progress.term_limit = limit;
      bool tooLarge = false;
      long long counted = 0;
      try {
        boost::mrt_progress_scope scope(&progress);
        meta(circ);
      } catch(const sapecng::metacircuit::too_large& e) {
        tooLarge = true;
        counted = e.terms;
      }
      const bool want = limit < 1000;
      std::cout << "limit " << limit << ", " << threads << " threads: "
        << (tooLarge ? "too large (" + std::to_string(counted) + " terms)"
                     : "solved") << "\n";
      ok = ok && tooLarge == want && (!tooLarge || counted > limit);
    }
  }
  return ok;
}


int main(int argc, char** argv)
{
  if(argc == 2 && std::string(argv[1]) == "exactsum")
    return exact_sums() ? 0 : 1;
  if(argc == 2 && std::string(argv[1]) == "limit")
    return term_limit() ? 0 : 1;

  if(argc == 3 && std::string(argv[1]) == "grid") {
    const std::string data = grid(std::atoi(argv[2]));
    double t0 = 0.;
    const std::string reference = solve("grid.crc", data, 0, &t0);
    std::cout << "no pool: " << t0 << " s\n";
    bool ok = true;
    for(unsigned threads = 1; threads <= 8; threads *= 2) {
      double t = 0.;
      int permille = 0;
      const bool same =
        solve("grid.crc", data, threads, &t, &permille) == reference;
      std::cout << threads << " thread(s): " << t << " s, progress "
        << permille << "/1000" << (same ? "" : "  DIFFERENT") << "\n";
      ok = ok && same && permille >= 995;
    }
    const bool stopped = cancels("grid.crc", data);
    std::cout << "cancel: " << (stopped ? "stopped" : "NOT STOPPED") << "\n";
    return (ok && stopped) ? 0 : 1;
  }

  if(argc < 2) {
    std::cerr << "usage: " << argv[0] << " <file>... | grid <n>\n";
    return 2;
  }

  bool ok = true;
  for(int a = 1; a < argc; ++a) {
    const std::string file(argv[a]);
    std::ifstream in(file.c_str());
    if(!in) {
      std::cerr << "cannot open " << file << "\n";
      return 2;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    double t = 0.;
    const std::string reference = solve(file, buffer.str(), 0, &t);
    std::cout << file << "\n  no pool: " << t << " s\n";
    for(unsigned threads : { 1u, 2u, 3u, 4u, 8u }) {
      const bool same = solve(file, buffer.str(), threads, &t) == reference;
      std::cout << "  " << threads << " thread(s): " << t << " s"
        << (same ? "" : "  DIFFERENT") << "\n";
      ok = ok && same;
    }
  }
  return ok ? 0 : 1;
}
