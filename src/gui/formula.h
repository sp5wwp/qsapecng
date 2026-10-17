/*
    QSapecNG - Qt based SapecNG GUI front-end
    Copyright (C) 2009, Michele Caini
    Copyright (C) 2026, Wojciech Kaczmarski

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


#ifndef FORMULA_H
#define FORMULA_H


#include "model/metacircuit.h"

#include <QString>


namespace qsapecng
{


// Presentation of solver results (polynomials in s whose coefficients are
// sums of number x component-name products).
namespace formula
{


typedef sapecng::metacircuit::expression Expr;

struct Fraction
{
  Expr num;
  Expr den;
};

// Light, exact simplification: zero terms dropped, and a purely numeric
// denominator divided into the numerator, e.g. (-V1) / 0.1 -> -10 V1.
Fraction simplified(const Expr& num, const Expr& den);

// true when the expression is the constant 1
bool isOne(const Expr& e);

enum class Style { Text, Html, Latex };

// "-10 * V1", "C1 * C2 * s^2 + (C1 * R1 + C2 * R2) * s + 1" (Text);
// the same with middle dots, superscripts and a real minus sign (Html);
// "-10 \cdot V_{1}" (Latex). Parentheses only around a sum multiplied by
// a power of s; minus signs as signs, not as "( - x )".
QString render(const Expr& e, Style style);

// whole fraction for the clipboard: "num" or "(num) / (den)" (Text),
// "\frac{num}{den}" or "num" (Latex)
QString clipboardText(const Fraction& f, Style style);


}


}


#endif // FORMULA_H
