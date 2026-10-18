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


#ifndef RESULTPANEL_H
#define RESULTPANEL_H


#include "gui/formula.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QWidget>


class QTabBar;
class QTextBrowser;
class QUrl;


namespace qsapecng
{


// The transfer function to one output (Out marker, voltmeter, ammeter).
struct OutputResult
{
  QString name;              // Out1, VM1 ...; empty when the circuit has none
  formula::Fraction mixed;   // "Result": symbolic components by name
  formula::Fraction raw;     // every component by name
  formula::Fraction digit;   // numeric
  bool singular = false;     // no unique solution: the fractions mean nothing
};


// Output of one Resolve: one result per output.
struct SolveResult
{
  QString file;
  QList<OutputResult> outputs;

  bool anySingular() const
    {
      for(const OutputResult& o : outputs)
        if(o.singular)
          return true;
      return false;
    }
};


// Shows the result of the active schematic as fractions, with a copy
// link per expression and a notice when the circuit changed since.
class ResultPanel: public QWidget
{

  Q_OBJECT

public:
  explicit ResultPanel(QWidget* parent = 0);

  void showResult(const SolveResult& result, bool outOfDate);
  void showPlaceholder();

  // the output whose result is shown (one tab per output when there are
  // several)
  QString currentOutput() const;

signals:
  // a Plot link was clicked (WorkPlane::Kind)
  void plotRequested(int function);
  // right-click > Clear
  void clearRequested();

protected:
  void changeEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private slots:
  void linkClicked(const QUrl& url);

private:
  void render();
  void updateTabs();
  const OutputResult* current() const;

  QTabBar* tabs_;
  QString currentName_;   // kept across results
  QTextBrowser* view_;
  bool sideBySide_ = false;
  // long sections shown in full ("output/section"), kept across results
  QSet<QString> expanded_;
  SolveResult result_;
  bool hasResult_;
  bool outOfDate_;

};


}


#endif // RESULTPANEL_H
