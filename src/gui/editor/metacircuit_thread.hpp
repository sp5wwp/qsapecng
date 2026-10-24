/*
    QSapecNG - Qt based SapecNG GUI front-end
    Copyright (C) 2009, Michele Caini

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


#ifndef METACIRCUIT_THREAD_H
#define METACIRCUIT_THREAD_H


#include "model/metacircuit.h"
#include "parser/parser.h"

#include <QMutexLocker>
#include <QThread>
#include <QMutex>

#include "boost-sapecng/mrt_pool.hpp"
#include "boost-sapecng/mrt_progress.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <vector>


namespace qsapecng
{


class MetaCircuit_Thread: public QThread
{

  Q_OBJECT

public:
  MetaCircuit_Thread(QObject* parent = 0): QThread(parent) { }

  // the most cores (threads) a solve uses, for all its outputs together
  inline void setMaxThreads(int n) { maxThreads_ = std::max(1, n); }

  // asks a running solve to stop; it ends soon after, cancelled()
  inline void cancel() { cancel_.store(true); }
  inline bool cancelled() const { return cancelled_.load(); }

  // 0 - 1000 over all the outputs, and how many are done; safe while running
  inline int progress() const
    {
      if(permille_.empty())
        return 0;
      long sum = 0;
      for(const auto& p : permille_)
        sum += std::min(1000, p->load(std::memory_order_relaxed));
      return int(sum / long(permille_.size()));
    }
  inline int outputsDone() const { return done_.load(); }
  inline int outputTotal() const { return int(permille_.size()); }

  // builds one circuit per output of the netlist (Out markers, voltmeters,
  // ammeters): the parser is run once per output
  inline void apply(sapecng::abstract_parser& parser)
    {
      QMutexLocker locker(&mutex_);
      outputs_.clear();
      unsigned int count = 1;
      for(unsigned int k = 0; k < count; ++k) {
        Output out;
        out.circuit.reset(new sapecng::circuit);
        out.meta.reset(new sapecng::metacircuit);
        sapecng::circuit_builder builder(*out.circuit, k);
        parser.parse(builder);
        if(k == 0)
          count = std::max<std::size_t>(1, out.circuit->outputNames().size());
        if(k < out.circuit->outputNames().size())
          out.name = out.circuit->outputNames().at(k);
        outputs_.push_back(std::move(out));
      }
      permille_.clear();
      for(std::size_t k = 0; k < outputs_.size(); ++k)
        permille_.emplace_back(new std::atomic<int>(0));
      done_.store(0);
    }

  inline int outputCount()
    { QMutexLocker locker(&mutex_); return int(outputs_.size()); }
  inline std::string outputName(int k)
    { QMutexLocker locker(&mutex_); return valid(k) ? outputs_[k].name : std::string(); }

  inline sapecng::metacircuit::expression raw_numerator(int k = 0)
    { QMutexLocker locker(&mutex_); return valid(k) ? outputs_[k].meta->raw().first : empty(); }
  inline sapecng::metacircuit::expression raw_denominator(int k = 0)
    { QMutexLocker locker(&mutex_); return valid(k) ? outputs_[k].meta->raw().second : empty(); }
  inline sapecng::metacircuit::expression digit_numerator(int k = 0)
    { QMutexLocker locker(&mutex_); return valid(k) ? outputs_[k].meta->digit().first : empty(); }
  inline sapecng::metacircuit::expression digit_denominator(int k = 0)
    { QMutexLocker locker(&mutex_); return valid(k) ? outputs_[k].meta->digit().second : empty(); }
  inline sapecng::metacircuit::expression mixed_numerator(int k = 0)
    { QMutexLocker locker(&mutex_); return valid(k) ? outputs_[k].meta->mixed().first : empty(); }
  inline sapecng::metacircuit::expression mixed_denominator(int k = 0)
    { QMutexLocker locker(&mutex_); return valid(k) ? outputs_[k].meta->mixed().second : empty(); }
  inline bool singular(int k = 0)
    { QMutexLocker locker(&mutex_); return !valid(k) || outputs_[k].meta->singular(); }

protected:
  // all the outputs share one pool of maxThreads_ threads: several outputs
  // are solved at the same time, and a single big one is split among them
  void run()
  {
    QMutexLocker locker(&mutex_);
    cancel_.store(false);
    cancelled_.store(false);
    done_.store(0);
    for(auto& p : permille_)
      p->store(0);

    boost::mrt_pool pool{ unsigned(maxThreads_) };
    boost::mrt_pool::group all;
    for(std::size_t k = 0; k < outputs_.size(); ++k)
      pool.submit_root(all, [this, k, &pool]() { solve(k, pool); });
    pool.wait_all(all);   // runs outputs too; their failures are theirs
  }

private:
  void solve(std::size_t k, boost::mrt_pool& pool)
  {
    std::atomic<long long> total(0);
    boost::mrt_progress progress;
    progress.cancel = &cancel_;
    progress.permille = permille_[k].get();
    progress.pool = &pool;
    progress.total = &total;
    try {
      boost::mrt_progress_scope scope(&progress);
      (*outputs_[k].meta)(*outputs_[k].circuit);
    } catch(const boost::mrt_cancelled&) {
      cancelled_.store(true);
      cancel_.store(true);   // the other outputs stop too
    } catch(...) {
      // out of memory and the like: this output stays without a result
    }
    permille_[k]->store(1000);
    done_.fetch_add(1);
  }

private:
  struct Output
  {
    std::string name;
    std::unique_ptr<sapecng::circuit> circuit;
    std::unique_ptr<sapecng::metacircuit> meta;
  };

  inline bool valid(int k) const
    { return k >= 0 && k < int(outputs_.size()); }
  static sapecng::metacircuit::expression empty()
    { return sapecng::metacircuit::expression(); }

  std::vector<Output> outputs_;
  std::vector< std::unique_ptr< std::atomic<int> > > permille_;
  std::atomic<bool> cancel_{false};
  std::atomic<bool> cancelled_{false};
  std::atomic<int> done_{0};
  int maxThreads_ = 1;
  QMutex mutex_;

};


}


#endif // METACIRCUIT_THREAD_H
