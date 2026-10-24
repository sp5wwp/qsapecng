/*
    SapecNG - Next Generation Symbolic Analysis Program for Electric Circuit
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


#include "model/metacircuit.h"

#include <algorithm>
#include <atomic>
#include <iterator>
#include "boost-sapecng/mrt.hpp"


namespace sapecng
{


std::string metacircuit::as_string(const expression& e)
{
  std::string e_str = "";

  if(e.size() != 0) {
    expression::const_reverse_iterator it = e.rbegin();

    e_str += as_string(it->second);
    if(it->first != 0) {
      e_str += " * s";
      if(it->first > 1)
        e_str += "^" + stringify(it->first);
    }

    for(++it; it != e.rend(); ++it) {
      e_str += " + " + as_string(it->second);
      if(it->first != 0) {
        e_str += " * s";
        if(it->first > 1)
          e_str += "^" + stringify(it->first);
      }
    }
  }

  if(e_str.size() == 0)
    e_str = "0";

  return e_str;
}


std::string metacircuit::stringify(double x)
{
  std::ostringstream o;
  if (!(o << x));
//     throw BadConversion("stringify(double)");
  return o.str();
} 


std::string metacircuit::as_string(const degree_expression& e)
{
  std::string e_str = "";

  if(e.size() != 0) {
    if(e.size() > 1)
      e_str += "( ";

    degree_expression::const_iterator it = e.begin();
    e_str += as_string(*(it++));

    for(; it != e.end(); ++it)
      e_str += " + " + as_string(*it);

    if(e.size() > 1)
      e_str += " )";
  }

  return e_str;
}


std::string metacircuit::as_string(const basic_expression& e)
{
  std::string e_str = "";

  int adjust_value = 1;
  if(e.first < 0) {
    adjust_value = -1;
    e_str += "( - ";
  }

  bool has_sym_part = (e.second.size() != 0);
  bool has_num_part = false;
  if(!has_sym_part || (e.first * adjust_value) != 1) {
    e_str += stringify(adjust_value * e.first);
    has_num_part = true;
  }

  std::list<std::string>::const_iterator it = e.second.begin();

  if(!has_num_part)
    e_str += *(it++);

  for(; it != e.second.end(); ++it)
    e_str += " * " + *it;

  if(e.first < 0)
    e_str += " )";

  return e_str;
}


void metacircuit::operator()(const circuit& circuit)
{
  // clear data
  singular_ = false;
  raw_.first.clear();
  raw_.second.clear();
  digit_.first.clear();
  digit_.second.clear();
  mixed_.first.clear();
  mixed_.second.clear();

  // use circuit
  if(num_edges(circuit.vGraph()) == num_edges(circuit.iGraph())
      && num_vertices(circuit.vGraph()) == num_vertices(circuit.iGraph())
    )
  {

    const Graph& refG = circuit.iGraph();
    const std::vector<circuit::edge_descriptor>& refO = circuit.iGraph_order();

    std::vector< bool > forcedIn(num_edges(refG), false);
    // put in forced edges
    typedef
    boost::property_map<sapecng::Graph, boost::edge_type_t>::const_type
    FM_type;

    FM_type FM = get(boost::edge_type, refG);

    for(std::vector<bool>::size_type i = 0; i < forcedIn.size(); ++i) {
      if(FM[ refO[i] ] == sapecng::F)
        forcedIn[i] = true;
    }
    //
    typedef std::vector<bool> Seq;

    // the trees, in the same order however many threads look for them
    boost::tree_segment<Seq> found;
    boost::mrt_two_graphs_common_spanning_trees
        <
          sapecng::Graph,
          std::vector< sapecng::circuit::edge_descriptor >,
          boost::segment_collector<Seq>,
          std::vector< bool >
        >
      (
        circuit.iGraph(),
        circuit.iGraph_order(),
        circuit.vGraph(),
        circuit.vGraph_order(),
        boost::segment_collector<Seq>(found),
        forcedIn
      );

    std::vector<Seq> coll;
    auto take = [&coll](Seq&& tree) { coll.push_back(std::move(tree)); };
    found.drain(take);

    boost::mrt_progress* progress = boost::mrt_current_progress();
    std::atomic<bool>* cancel = progress ? progress->cancel : nullptr;
    boost::mrt_pool* pool = progress ? progress->pool : nullptr;

    // what a tree adds: worked out in parallel (the signs take most of the
    // time), then summed up in order so the result does not depend on the
    // number of threads
    struct term
    {
      int degree = 0;
      int part = 0;   // 1: numerator, 2: denominator, 0: neither
      basic_expression raw, digit, mixed;
    };
    const Graph& iG = circuit.iGraph();
    const Graph& vG = circuit.vGraph();
    const std::vector<circuit::edge_descriptor>& iO = circuit.iGraph_order();
    const std::vector<circuit::edge_descriptor>& vO = circuit.vGraph_order();
    auto make_term = [&](const Seq& inL, term& t) {
      t.degree = 0;
      t.raw = basic_expression(1., std::list<std::string>());
      t.digit = basic_expression(1., std::list<std::string>());
      t.mixed = basic_expression(1., std::list<std::string>());

      int sign = 1;
      sign *= get_sign(num_vertices(iG), iG, iO, inL);
      sign *= get_sign(num_vertices(vG), vG, vO, inL);

      t.raw.first *= sign;
      t.digit.first *= sign;
      t.mixed.first *= sign;

      bool numerator_part = false;
      bool denominator_part = false;
      for(Seq::size_type j = 0; j < inL.size(); ++j) {
        if(inL[j] && (FM[ refO[j] ] == sapecng::GREF))
          numerator_part = true;

        if(inL[j] && (FM[ refO[j] ] == sapecng::YREF))
          denominator_part = true;

        if((inL[j] && FM[ refO[j] ] == sapecng::Y)
            || (!inL[j] && FM[ refO[j] ] == sapecng::Z)
          )
        {
          // read all data, creating complex expressions
          if(get(boost::edge_symbolic, refG)[ refO[j] ]) {
            if(get(boost::edge_name, refG)[ refO[j] ].size())
              t.mixed.second.push_back(get(boost::edge_name, refG)[ refO[j] ]);
          } else {
            t.mixed.first *= get(boost::edge_weight, refG)[ refO[j] ];
          }

          t.digit.first *= get(boost::edge_weight, refG)[ refO[j] ];
          if(get(boost::edge_name, refG)[ refO[j] ].size())
            t.raw.second.push_back(get(boost::edge_name, refG)[ refO[j] ]);
          else
            t.raw.first *= get(boost::edge_weight, refG)[ refO[j] ];

          t.degree += get(boost::edge_degree, refG)[ refO[j] ];
        }

      }

      t.raw.second.sort();
      t.digit.second.sort();
      t.mixed.second.sort();

      t.part = numerator_part ? 1 : (denominator_part ? 2 : 0);
    };

    term_index rawNum, rawDen, digitNum, digitDen, mixedNum, mixedDen;
    const std::size_t block = std::size_t(1) << 14;
    std::vector<term> terms;
    for(std::size_t base = 0; base < coll.size(); base += block) {
      if(cancel && cancel->load(std::memory_order_relaxed))
        throw boost::mrt_cancelled();

      const std::size_t n = std::min(block, coll.size() - base);
      terms.assign(n, term());
      boost::mrt_pool::for_each(pool, n, 256,
        [&](std::size_t i) {
          if((i & 255) == 0 && cancel
              && cancel->load(std::memory_order_relaxed))
            throw boost::mrt_cancelled();
          make_term(coll[base + i], terms[i]);
        });

      for(const term& t : terms) {
        if(t.part == 1) {
          append(t.degree, raw_.first, t.raw, rawNum);
          append(t.degree, digit_.first, t.digit, digitNum);
          append(t.degree, mixed_.first, t.mixed, mixedNum);
        } else if(t.part == 2) {
          append(t.degree, raw_.second, t.raw, rawDen);
          append(t.degree, digit_.second, t.digit, digitDen);
          append(t.degree, mixed_.second, t.mixed, mixedDen);
        }
      }
    }
    coll.clear();
    coll.shrink_to_fit();

    const bool rawOk = simplify(raw_);
    const bool digitOk = simplify(digit_);
    simplify(mixed_);
    // structurally (raw) or for these values (digit)
    singular_ = !rawOk || !digitOk;
  }

}


void metacircuit::append(int deg, expression& e, const basic_expression& be,
    term_index& index)
{
  const term_index::key_type key(deg, be.second);
  term_index::iterator found = index.find(key);
  if(found != index.end()) {
    found->second->first += be.first;
  } else {
    degree_expression& terms = e[deg];
    terms.push_back(be);
    index.insert(std::make_pair(key, std::prev(terms.end())));
  }
}


bool metacircuit::simplify(std::pair<expression, expression>& p)
{
  compress(p.first);
  compress(p.second);
  const bool denominatorOk = (p.second.size() != 0);

  if(p.first.size() == 0) {

    expression en;
    degree_expression den;
    basic_expression ben = std::make_pair(0., std::list<std::string>());
    den.push_back(ben);
    en[0] = den;

    expression ed;
    degree_expression ded;
    basic_expression bed = std::make_pair(1., std::list<std::string>());
    ded.push_back(bed);
    ed[0] = ded;

    p.first = en;
    p.second = ed;

  } else if(p.second.size() == 0) {

    expression en;
    degree_expression den;
    basic_expression ben = std::make_pair(1., std::list<std::string>());
    den.push_back(ben);
    en[0] = den;

    expression ed;
    degree_expression ded;
    basic_expression bed = std::make_pair(0., std::list<std::string>());
    ded.push_back(bed);
    ed[0] = ded;

    p.first = en;
    p.second = ed;

  } else {

    while(p.first.find(0) == p.first.end()
        && p.second.find(0) == p.second.end())
    {
      expression en;
      for(expression::iterator
          it = p.first.begin(); it != p.first.end(); ++it)
        en[it->first - 1] = it->second;

      expression ed;
      for(expression::iterator
          it = p.second.begin(); it != p.second.end(); ++it)
        ed[it->first - 1] = it->second;

      p.first = en;
      p.second = ed;
    }

    if(group_minus(p.first) && group_minus(p.second)) {
      toggle_minus(p.first);
      toggle_minus(p.second);
    }

    double div = find_div(p.first);
    if(div != 0 && div != 1) {
      normalize(p.first, div);
      normalize(p.second, div);
    }

    std::list<std::string> candidates = p.first.begin()->second.front().second;
    for(std::list<std::string>::const_iterator
        it = candidates.begin(); it != candidates.end(); ++it)
    {
      if(has_candidate(p.first, *it)
          && has_candidate(p.second, *it))
      {
        remove_candidate(p.first, *it);
        remove_candidate(p.second, *it);
      }
    }

    if(p.first == p.second) {

      expression en;
      degree_expression den;
      basic_expression ben = std::make_pair(1., std::list<std::string>());
      den.push_back(ben);
      en[0] = den;

      expression ed;
      degree_expression ded;
      basic_expression bed = std::make_pair(1., std::list<std::string>());
      ded.push_back(bed);
      ed[0] = ded;

      p.first = en;
      p.second = ed;

    }

  }

  return denominatorOk;
}


void metacircuit::compress(expression& e)
{
  // Drop terms with a zero coefficient, then powers of s left without terms.
  // erase() invalidates the erased iterator, so continue from the one it
  // returns (advancing the erased one crashed the solver, issue #6).
  for(expression::iterator it = e.begin(); it != e.end(); ) {
    it->second.remove_if(detail::basic_checker());
    if(detail::degree_checker()(*it))
      it = e.erase(it);
    else
      ++it;
  }
}


bool metacircuit::group_minus(const expression& e)
{
  bool has_positive = false;;
  for(expression::const_iterator
      it = e.begin(); it != e.end() && !has_positive; ++it)
  {
    for(degree_expression::const_iterator
        dit = it->second.begin(); dit != it->second.end(); ++dit)
    {
      if(dit->first > 0)
        has_positive = true;
    }
  }

  return !has_positive;
}


void metacircuit::toggle_minus(expression& e)
{
  for(expression::iterator it = e.begin(); it != e.end(); ++it)
  {
    for(degree_expression::iterator
        dit = it->second.begin(); dit != it->second.end(); ++dit)
      dit->first *= -1;
  }
}


bool metacircuit::has_candidate(const expression& e, const std::string& c)
{
  bool miss = false;
  for(expression::const_iterator
      it = e.begin(); it != e.end() && !miss; ++it)
  {
    for(degree_expression::const_iterator
        dit = it->second.begin(); dit != it->second.end() && !miss; ++dit)
    {
      if(std::find(dit->second.begin(), dit->second.end(), c)
          == dit->second.end())
        miss = true;
    }
  }

  return !miss;
}


void metacircuit::remove_candidate(expression& e, const std::string& c)
{
  for(expression::iterator it = e.begin(); it != e.end(); ++it)
  {
    for(degree_expression::iterator
        dit = it->second.begin(); dit != it->second.end(); ++dit)
    {
      std::list<std::string>::iterator f =
        std::find(dit->second.begin(), dit->second.end(), c);
      if(f != dit->second.end())
        dit->second.erase(f);
    }
  }
}


double metacircuit::find_div(const expression& e)
{
  degree_expression top_expr = e.rbegin()->second;
  degree_expression::iterator dit = top_expr.begin();

  bool miss = false;
  double base = std::abs(dit->first);
  for(++dit; dit != top_expr.end() && !miss; ++dit)
    if(std::abs(dit->first) != base)
      miss = true;

  if(!miss)
    return base;

  return 1.;
}


void metacircuit::normalize(expression& e, double div)
{
  for(expression::iterator it = e.begin(); it != e.end(); ++it)
  {
    for(degree_expression::iterator
        dit = it->second.begin(); dit != it->second.end(); ++dit)
      dit->first /= div;
  }
}


int metacircuit::get_sign(
    size_t vertices,
    const Graph& graph,
    const std::vector< circuit::edge_descriptor >& order,
    const std::vector<bool>& inL
  )
{
  size_t row = vertices;
  size_t col = vertices - 1;

  int* matrix = new int[row * col];
  for(size_t i = 0; i < row; ++i)
    for(size_t j = 0; j < col; ++j)
      matrix[col * i + j] = 0;

  for(std::vector<bool>::size_type i = 0, j = 0; i < inL.size(); ++i) {
    if(inL[i]) {
      matrix[col * source(order[i], graph) + j] = -1;
      matrix[col * target(order[i], graph) + j] = +1;
      ++j;
    }
  }

  int det = 1;
  for(size_t ofs = 0; ofs < col; ++ofs) {
    for(size_t iter = ofs; iter < row; ++iter) {
      if(matrix[iter * col + ofs] != 0) {
        if(iter != ofs) {
          for(size_t cnt = ofs; cnt < col; ++cnt) {
            int swap = matrix[iter * col + cnt];
            matrix[iter * col + cnt] = matrix[ofs * col + cnt];
            matrix[ofs * col + cnt] = swap;
          }
          det *= -1;
        }
        for(iter = ofs + 1; iter < row; ++iter) {
          if(matrix[iter * col + ofs] != 0) {
            int weight = -1 * matrix[ofs * col + ofs] / matrix[iter * col + ofs];
            for(size_t cnt = ofs; cnt < col; ++cnt)
              matrix[iter * col + cnt] += matrix[ofs * col + cnt] * weight;
          }
        }
        iter = row;
      }
    }
  }

  for(size_t iter = 0; iter < col; ++iter)
    det *= matrix[iter * col + iter];

  delete [] matrix;

  return det;
}


}
