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


#include "gui/plotdialog.h"
#include "gui/units.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>


namespace qsapecng
{


PlotDialog::PlotDialog(const PlotSettings& s, bool newPlot, QWidget* parent)
  : QDialog(parent)
{
  setWindowTitle(newPlot ? tr("New Plot") : tr("Plot Settings"));

  kind_ = new QComboBox;
  kind_->addItem(tr("Frequency response (magnitude)"), WorkPlane::MagnitudeKind);
  kind_->addItem(tr("Phase response"), WorkPlane::PhaseKind);
  kind_->addItem(tr("Bode plot (magnitude and phase)"), WorkPlane::BodeKind);
  kind_->addItem(tr("Poles and zeros"), WorkPlane::PoleZeroKind);
  kind_->setCurrentIndex(qMax(0, kind_->findData(s.kind)));

  // frequency sweep
  autoRange_ = new QCheckBox(tr("&Automatic: a decade beyond the poles and zeros"));
  autoRange_->setChecked(s.autoRange);
  auto field = [](double value) {
    QLineEdit* edit = new QLineEdit(
      units::formatForEdit(QString::number(value, 'g', 8)));
    edit->setValidator(new units::ValueValidator(QStringLiteral("Hz"), edit));
    edit->setToolTip(tr("Frequency in Hz; prefixes allowed: 10, 1k, 2.5MEG"));
    return edit;
  };
  start_ = field(s.fStart);
  stop_ = field(s.fStop);
  points_ = new QSpinBox;
  points_->setRange(10, 10000);
  points_->setValue(s.pointsPerDecade);
  points_->setToolTip(tr("Points per decade, spaced logarithmically"));
  angular_ = new QCheckBox(tr("Frequency in &rad/s"));
  angular_->setChecked(s.angular);
  xLog_ = new QCheckBox(tr("Logarithmic &frequency axis"));
  xLog_->setChecked(s.xLog);

  QFormLayout* sweep = new QFormLayout;
  sweep->addRow(autoRange_);
  sweep->addRow(tr("&Start (Hz):"), start_);
  sweep->addRow(tr("St&op (Hz):"), stop_);
  sweep->addRow(tr("&Points per decade:"), points_);
  sweep->addRow(angular_);
  sweep->addRow(xLog_);
  QGroupBox* sweepBox = new QGroupBox(tr("Frequency"));
  sweepBox->setLayout(sweep);

  // vertical axes
  db_ = new QCheckBox(tr("Magnitude in &dB"));
  db_->setChecked(s.magnitudeDb);
  yLog_ = new QCheckBox(tr("&Logarithmic magnitude axis"));
  yLog_->setChecked(s.yLog);
  degrees_ = new QCheckBox(tr("Phase in de&grees"));
  degrees_->setChecked(s.phaseDegrees);
  QVBoxLayout* axes = new QVBoxLayout;
  axes->addWidget(db_);
  axes->addWidget(yLog_);
  axes->addWidget(degrees_);
  QGroupBox* axesBox = new QGroupBox(tr("Vertical axes"));
  axesBox->setLayout(axes);

  buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  if(newPlot)
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("&Plot"));
  connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

  QFormLayout* top = new QFormLayout;
  top->addRow(tr("P&lot:"), kind_);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->addLayout(top);
  layout->addWidget(sweepBox);
  layout->addWidget(axesBox);
  layout->addWidget(buttons_);

  for(QCheckBox* c : { autoRange_, db_ })
    connect(c, &QCheckBox::toggled, this, &PlotDialog::validate);
  connect(start_, &QLineEdit::textChanged, this, &PlotDialog::validate);
  connect(stop_, &QLineEdit::textChanged, this, &PlotDialog::validate);
  validate();
}


void PlotDialog::validate()
{
  start_->setEnabled(!autoRange_->isChecked());
  stop_->setEnabled(!autoRange_->isChecked());
  yLog_->setEnabled(!db_->isChecked());   // dB values can be negative

  double a = 0., b = 0.;
  const bool ok = autoRange_->isChecked()
    || (units::parse(start_->text(), "Hz", &a)
        && units::parse(stop_->text(), "Hz", &b) && a > 0 && b > a);
  buttons_->button(QDialogButtonBox::Ok)->setEnabled(ok);
}


PlotSettings PlotDialog::settings() const
{
  PlotSettings s;
  s.kind = kind_->currentData().toInt();
  s.autoRange = autoRange_->isChecked();
  units::parse(start_->text(), "Hz", &s.fStart);
  units::parse(stop_->text(), "Hz", &s.fStop);
  s.pointsPerDecade = points_->value();
  s.angular = angular_->isChecked();
  s.xLog = xLog_->isChecked();
  s.magnitudeDb = db_->isChecked();
  s.yLog = yLog_->isChecked() && !s.magnitudeDb;
  s.phaseDegrees = degrees_->isChecked();
  return s;
}


}
