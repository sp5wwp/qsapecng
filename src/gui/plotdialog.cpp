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
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>


namespace qsapecng
{


PlotSettingsWidget::PlotSettingsWidget(const PlotSettings& s, QWidget* parent)
  : QWidget(parent)
{
  kind_ = new QComboBox;
  kind_->addItem(tr("Frequency response (magnitude)"), WorkPlane::MagnitudeKind);
  kind_->addItem(tr("Phase response"), WorkPlane::PhaseKind);
  kind_->addItem(tr("Bode plot (magnitude and phase)"), WorkPlane::BodeKind);
  kind_->addItem(tr("Poles and zeros"), WorkPlane::PoleZeroKind);

  // frequency sweep
  autoRange_ = new QCheckBox(tr("&Automatic range"));
  autoRange_->setToolTip(tr("From a decade below the lowest to a decade "
                            "above the highest pole or zero"));
  auto field = []() {
    QLineEdit* edit = new QLineEdit;
    edit->setValidator(new units::ValueValidator(QStringLiteral("Hz"), edit));
    edit->setToolTip(tr("Frequency in Hz; prefixes allowed: 10, 1k, 2.5MEG"));
    return edit;
  };
  start_ = field();
  stop_ = field();
  points_ = new QSpinBox;
  points_->setRange(10, 10000);
  points_->setToolTip(tr("Points per decade, spaced logarithmically"));
  angular_ = new QCheckBox(tr("Frequency in &rad/s"));
  xLog_ = new QCheckBox(tr("Logarithmic &frequency axis"));

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
  yLog_ = new QCheckBox(tr("&Logarithmic magnitude axis"));
  degrees_ = new QCheckBox(tr("Phase in de&grees"));
  QVBoxLayout* axes = new QVBoxLayout;
  axes->addWidget(db_);
  axes->addWidget(yLog_);
  axes->addWidget(degrees_);
  QGroupBox* axesBox = new QGroupBox(tr("Vertical axes"));
  axesBox->setLayout(axes);

  output_ = new QComboBox;
  output_->setToolTip(tr("Which output of the circuit (Out marker, "
                         "voltmeter or ammeter) to plot"));
  outputLabel_ = new QLabel(tr("O&utput:"));
  outputLabel_->setBuddy(output_);

  QFormLayout* top = new QFormLayout;
  top->addRow(tr("P&lot:"), kind_);
  top->addRow(outputLabel_, output_);
  outputLabel_->hide();
  output_->hide();

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addLayout(top);
  layout->addWidget(sweepBox);
  layout->addWidget(axesBox);
  layout->addStretch();

  // spin steps settle before they count (each one recomputes the plot)
  settle_ = new QTimer(this);
  settle_->setSingleShot(true);
  settle_->setInterval(350);
  connect(settle_, &QTimer::timeout, this, [this]() { emit changed(); });

  auto edited = [this]() {
      update();
      if(!filling_ && isValid())
        emit changed();
    };
  for(QCheckBox* c : { autoRange_, angular_, xLog_, db_, yLog_, degrees_ })
    connect(c, &QCheckBox::toggled, this, edited);
  connect(kind_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, edited);
  connect(output_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, edited);
  connect(start_, &QLineEdit::editingFinished, this, edited);
  connect(stop_, &QLineEdit::editingFinished, this, edited);
  connect(start_, &QLineEdit::textChanged, this, &PlotSettingsWidget::update);
  connect(stop_, &QLineEdit::textChanged, this, &PlotSettingsWidget::update);
  connect(points_, QOverload<int>::of(&QSpinBox::valueChanged), this,
    [this]() { if(!filling_) settle_->start(); });

  setSettings(s);
}


void PlotSettingsWidget::setSettings(const PlotSettings& s)
{
  filling_ = true;
  kind_->setCurrentIndex(qMax(0, kind_->findData(s.kind)));
  output_->setCurrentIndex(qMax(0, output_->findText(s.output)));
  autoRange_->setChecked(s.autoRange);
  start_->setText(units::formatForEdit(QString::number(s.fStart, 'g', 8)));
  stop_->setText(units::formatForEdit(QString::number(s.fStop, 'g', 8)));
  points_->setValue(s.pointsPerDecade);
  angular_->setChecked(s.angular);
  xLog_->setChecked(s.xLog);
  db_->setChecked(s.magnitudeDb);
  yLog_->setChecked(s.yLog);
  degrees_->setChecked(s.phaseDegrees);
  settle_->stop();
  filling_ = false;
  update();
}


void PlotSettingsWidget::setOutputs(const QStringList& names)
{
  const bool filling = filling_;
  filling_ = true;
  const QString current = output_->currentText();
  output_->clear();
  output_->addItems(names);
  output_->setCurrentIndex(qMax(0, output_->findText(current)));
  const bool choice = names.size() > 1;
  outputLabel_->setVisible(choice);
  output_->setVisible(choice);
  filling_ = filling;
}


void PlotSettingsWidget::update()
{
  start_->setEnabled(!autoRange_->isChecked());
  stop_->setEnabled(!autoRange_->isChecked());
  yLog_->setEnabled(!db_->isChecked());   // dB values can be negative
}


bool PlotSettingsWidget::isValid() const
{
  double a = 0., b = 0.;
  return autoRange_->isChecked()
    || (units::parse(start_->text(), "Hz", &a)
        && units::parse(stop_->text(), "Hz", &b) && a > 0 && b > a);
}


PlotSettings PlotSettingsWidget::settings() const
{
  PlotSettings s;
  s.kind = kind_->currentData().toInt();
  s.output = output_->currentText();
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


void PlotDialog::setOutputs(const QStringList& names, const QString& current)
{
  form_->setOutputs(names);
  PlotSettings s = form_->settings();
  s.output = current;
  form_->setSettings(s);
}


PlotDialog::PlotDialog(const PlotSettings& s, bool newPlot, QWidget* parent)
  : QDialog(parent)
{
  setWindowTitle(newPlot ? tr("New Plot") : tr("Plot Settings"));

  form_ = new PlotSettingsWidget(s);
  buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  if(newPlot)
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("&Plot"));
  connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->addWidget(form_);
  layout->addWidget(buttons_);

  auto validate = [this]() {
      buttons_->button(QDialogButtonBox::Ok)->setEnabled(form_->isValid());
    };
  for(QLineEdit* e : form_->findChildren<QLineEdit*>())
    connect(e, &QLineEdit::textChanged, this, validate);
  for(QCheckBox* c : form_->findChildren<QCheckBox*>())
    connect(c, &QCheckBox::toggled, this, validate);
  validate();
}


}
