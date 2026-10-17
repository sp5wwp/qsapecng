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


#ifndef PLOTDIALOG_H
#define PLOTDIALOG_H


#include "gui/workplane/workplane.h"

#include <QDialog>


class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QSpinBox;


namespace qsapecng
{


// Run > New Plot and Plot > Settings: what to plot, the frequency sweep
// and the units of the axes.
class PlotDialog: public QDialog
{

  Q_OBJECT

public:
  PlotDialog(const PlotSettings& settings, bool newPlot, QWidget* parent = 0);

  PlotSettings settings() const;

private slots:
  void validate();

private:
  QComboBox* kind_;
  QCheckBox* autoRange_;
  QLineEdit* start_;
  QLineEdit* stop_;
  QSpinBox* points_;
  QCheckBox* angular_;
  QCheckBox* xLog_;
  QCheckBox* db_;
  QCheckBox* yLog_;
  QCheckBox* degrees_;
  QDialogButtonBox* buttons_;

};


}


#endif // PLOTDIALOG_H
