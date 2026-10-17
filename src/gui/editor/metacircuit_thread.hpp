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

#include <algorithm>
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
  void run()
  {
    QMutexLocker locker(&mutex_);
    for(Output& out : outputs_)
      (*out.meta)(*out.circuit);
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
  QMutex mutex_;

};


}


#endif // METACIRCUIT_THREAD_H
