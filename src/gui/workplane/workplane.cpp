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


#include "gui/theme.h"
#include "model/metacircuit.h"
#include "functor/functor.hpp"
#include "gui/workplane/workplane.h"
#include "gui/units.h"
#include <QWheelEvent>
#include <QDoubleValidator>
#include <QGroupBox>
#include "config.h"
#include <cmath>
#include <QSpinBox>
#include <QSettings>
#include <QDialogButtonBox>
#include <QDialog>
#include <QCheckBox>
#include "gui/functor/functor_traits.hpp"
#include "gui/qlogger.h"
#include "gui/settings.h"

#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QFrame>

#include <QEvent>
#include <QApplication>
#include <QTimer>
#include <qwt_plot_grid.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_picker.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_panner.h>
#include <qwt_scale_engine.h>
#include <qwt_curve_fitter.h>
#include <qwt_weeding_curve_fitter.h>
#include <qwt_text.h>
#include <qwt_picker_machine.h>
#include <qwt_symbol.h>
#include <qwt_scale_map.h>
#include <qwt_text.h>

#include <map>
#include <limits>


namespace qsapecng
{


MarkableCurve::~MarkableCurve()
{
  clearPins();
  marker_->detach();
  delete marker_;
  // delete symbol_;
}


void MarkableCurve::setVisible(bool on)
{
  marker_->setVisible(false);

  if(on) marker_->attach(plot());
  else marker_->detach();

  // pinned markers stay with their curve
  foreach(QwtPlotMarker* pin, pins_) {
    if(on) pin->attach(plot());
    else pin->detach();
  }

  QwtPlotCurve::setVisible(on);
}


void MarkableCurve::clearPins()
{
  foreach(QwtPlotMarker* pin, pins_) {
    pin->detach();
    delete pin;
  }
  pins_.clear();
  if(plot())
    plot()->replot();
}


int MarkableCurve::nearestSample(const QPointF& pos) const
{
  if(!data() || data()->size() == 0)
    return -1;
  std::size_t idx;
  for(idx = 0; idx < (data()->size() - 1)
      && data()->sample(idx + 1).x() < pos.x(); ++idx);
  // the closer of the two samples around pos
  if(idx + 1 < data()->size()
      && qAbs(data()->sample(idx + 1).x() - pos.x())
         < qAbs(data()->sample(idx).x() - pos.x()))
    ++idx;
  return int(idx);
}


QString MarkableCurve::labelFor(int idx) const
{
  const QPointF p = data()->sample(idx);
  const QString x = units::format(QString::number(p.x(), 'g', 8), xUnit_);
  QString y = QString::number(p.y(), 'g', 4);
  if(!yUnit_.isEmpty())
    y += QLatin1Char(' ') + yUnit_;
  return x + QLatin1Char('\n') + y;
}


void MarkableCurve::placeLabel(QwtPlotMarker* marker, int idx) const
{
  const QPointF p = data()->sample(idx);
  marker->setValue(p);

  QwtText text(labelFor(idx));
  text.setColor(QApplication::palette().color(QPalette::Text));
  QColor bg = QApplication::palette().color(QPalette::Base);
  bg.setAlpha(220);
  text.setBackgroundBrush(bg);
  text.setBorderRadius(3);
  marker->setLabel(text);

  // keep the label inside the plot: towards the middle
  const Qt::Alignment h =
      (p.x() >= minXValue() + (maxXValue() - minXValue()) / 2)
    ? Qt::AlignLeft : Qt::AlignRight;
  const Qt::Alignment v =
      (p.y() >= minYValue() + (maxYValue() - minYValue()) / 2)
    ? Qt::AlignBottom : Qt::AlignTop;
  marker->setLabelAlignment(h | v);
}


void MarkableCurve::selected(const QPointF& pos)
{
  marker_->setVisible(false);
  if(!isVisible() || !plot())
    return;

  const int idx = nearestSample(pos);
  if(idx < 0)
    return;

  // a pinned marker near the release point (in pixels) is removed
  const QwtScaleMap xMap = plot()->canvasMap(xAxis());
  const QwtScaleMap yMap = plot()->canvasMap(yAxis());
  const QPointF at(xMap.transform(pos.x()), yMap.transform(pos.y()));
  for(int i = 0; i < pins_.size(); ++i) {
    const QPointF v = pins_.at(i)->value();
    const QPointF pin(xMap.transform(v.x()), yMap.transform(v.y()));
    if(QLineF(at, pin).length() < 10.
        || qAbs(v.x() - data()->sample(idx).x()) == 0.)
    {
      QwtPlotMarker* m = pins_.takeAt(i);
      m->detach();
      delete m;
      plot()->replot();
      return;
    }
  }

  QwtSymbol* symbol = new QwtSymbol(QwtSymbol::Diamond);
  symbol->setSize(QSize(9, 9));
  symbol->setPen(QPen(pen().color().darker(130), 1));
  symbol->setBrush(pen().color());

  QwtPlotMarker* pin = new QwtPlotMarker;
  pin->setLineStyle(QwtPlotMarker::NoLine);
  pin->setSymbol(symbol);
  pin->setZ(z() + 1);
  placeLabel(pin, idx);
  pin->attach(plot());
  pins_.append(pin);
  plot()->replot();
}


void MarkableCurve::appended(const QPointF& pos)
{
  if(isVisible()) {
    marker_->setVisible(true);
    moved(pos);
  }
}


void MarkableCurve::moved(const QPointF& pos)
{
  if(isVisible()) {
    const int idx = nearestSample(pos);
    if(idx < 0)
      return;
    placeLabel(marker_, idx);
    marker_->itemChanged();
  }
}


void MarkableCurve::initialize()
{
  symbol_ = new QwtSymbol;
  symbol_->setStyle(QwtSymbol::Ellipse);
  symbol_->setSize(QSize(5, 5));

  marker_ = new QwtPlotMarker;
  marker_->setLineStyle(QwtPlotMarker::NoLine);
  marker_->setSymbol(symbol_);
  marker_->setVisible(false);
}



MarkedCurve::~MarkedCurve()
{
  resetMarker();
}


void MarkedCurve::setVisible(bool on)
{
  if(!on) {
    resetMarker();
  } else {
    replotMarker();
    foreach(QwtPlotMarker* marker, markers_) {
      marker->setVisible(on);
      marker->attach(plot());
      marker->itemChanged();
    }
  }
}


void MarkedCurve::replotMarker()
{
  resetMarker();

  for(std::size_t i = 0; i < data()->size(); ++i) {
    QwtPlotMarker* marker = new QwtPlotMarker;
    marker->setLineStyle(QwtPlotMarker::NoLine);
    marker->setLabelAlignment(Qt::AlignBottom);
    marker->setValue(data()->sample(i).x(), data()->sample(i).y());

    QString label = "( " + QString::number(data()->sample(i).x()) + ", "
      + QString::number(data()->sample(i).y()) + " )";
    QwtText text(label);
    text.setColor(QApplication::palette().color(QPalette::Text));
    marker->setLabel(text);

    markers_.push_back(marker);
  }
}


void MarkedCurve::resetMarker()
{
  foreach(QwtPlotMarker* marker, markers_) {
    marker->detach();
    delete marker;
  }

  markers_.clear();
}



namespace
{


// Modern, palette-aware styling for the analysis plot: frameless canvas,
// low-contrast grid, antialiased curves in the accent colour and clearly
// distinguishable zero (o) / pole (x) markers.
void applyPlotStyle(
    const QPalette& pal,
    QwtPlot* plot,
    QwtPlotGrid* grid,
    QVector< QPair< QwtPlotCurve*, bool > >& curves,
    int zerosId,
    int polesId
  )
{
  const QColor base = pal.color(QPalette::Base);
  const QColor text = pal.color(QPalette::Text);
  const QColor accent = pal.color(QPalette::Highlight);
  const QColor poleColor(0xe5, 0x48, 0x4d);

  if(QwtPlotCanvas* canvas = qobject_cast<QwtPlotCanvas*>(plot->canvas())) {
    canvas->setFrameStyle(QFrame::NoFrame);
    QPalette cp = canvas->palette();
    cp.setColor(QPalette::Window, base);
    canvas->setPalette(cp);
    canvas->setAutoFillBackground(true);
  }

  QColor major = text;
  major.setAlpha(50);
  QColor minor = text;
  minor.setAlpha(20);
  grid->enableXMin(true);
  grid->setMajorPen(major, 0, Qt::SolidLine);
  grid->setMinorPen(minor, 0, Qt::SolidLine);

  for(int i = 0; i < curves.size(); ++i) {
    QwtPlotCurve* curve = curves[i].first;
    curve->setRenderHint(QwtPlotItem::RenderAntialiased, true);

    if(i == zerosId) {
      curve->setSymbol(new QwtSymbol(QwtSymbol::Ellipse,
        Qt::NoBrush, QPen(accent, 2), QSize(10, 10)));
    } else if(i == polesId) {
      curve->setSymbol(new QwtSymbol(QwtSymbol::XCross,
        Qt::NoBrush, QPen(poleColor, 2), QSize(11, 11)));
    } else {
      QPen pen = curve->pen();
      // phase in its own colour, so it reads apart in a Bode plot
      pen.setColor(i == WorkPlane::PHASE || i == WorkPlane::PHASE_RAD
        ? QColor(0xe0, 0x6c, 0x00) : accent);
      pen.setWidthF(2.);
      pen.setCosmetic(true);
      curve->setPen(pen);
    }
  }
}


}


WorkPlane::WorkPlane(QWidget* parent)
  : QWidget(parent)
{
  QSettings settings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION);
  autoRange_ = settings.value("Workplane/autoRange", true).toBool();
  fStart_ = settings.value("Workplane/fStart", 1.).toDouble();
  fStop_ = settings.value("Workplane/fStop", 1e5).toDouble();
  pointsPerDecade_ = settings.value("Workplane/pointsPerDecade", 100).toInt();
  if(!(fStart_ > 0 && fStop_ > fStart_)) { fStart_ = 1.; fStop_ = 1e5; }

  createMainLayout();
  setupCurves();
  setDirty();

  setLayout(mainLayout_);
  setWindowTitle(tr("WorkPlane"));
  setAttribute(Qt::WA_DeleteOnClose);

}


WorkPlane::~WorkPlane()
{
  // delete clickPointMachine_;

  for(int i = 0; i < curves_.size(); ++i) {
    // delete curves_[i].first->curveFitter();
    // delete curves_[i].first->symbol();
    delete curves_[i].first;
  }

  attached_.clear();
  curves_.clear();

  grid_->detach();
  delete grid_;
}


void WorkPlane::setData(
    const std::map<std::string, double>& values,
    const sapecng::metacircuit::expression& numerator,
    const sapecng::metacircuit::expression& denominator
  )
{
  setDirty();

  // the plots use the component values of the schematic
  values_ = values;
  num_ = numerator;
  den_ = denominator;

  if(autoRange_)
    autoFrequencyRange();
}


void WorkPlane::setDirty()
{
  attached_.clear();
  for(int i = 0; i < curves_.size(); ++i) {
    curves_[i].first->setVisible(false);
    curves_[i].first->detach();
    curves_[i].second = false;
  }

  plot_->setTitle(tr("none"));
  plot_->setAxisTitle(QwtPlot::yLeft, tr("none"));
  plot_->setAxisTitle(QwtPlot::xBottom, tr("none"));
  plot_->updateAxes();
  plot_->replot();
}


void WorkPlane::changeEvent(QEvent* event)
{
  const QEvent::Type t = event->type();
  if((t == QEvent::PaletteChange || t == QEvent::ApplicationPaletteChange
      || t == QEvent::StyleChange) && plot_ && grid_)
  {
    // defer until the application has finished re-polishing widgets
    QTimer::singleShot(0, this, [this]() {
      applyPlotStyle(QApplication::palette(), plot_, grid_, curves_, ZEROS, POLES);
      plot_->replot();
    });
  }
  QWidget::changeEvent(event);
}


void WorkPlane::applyAxisScales(bool forceLinear)
{
  // A pole/zero map has (mostly negative) real parts on the x axis, so it
  // is always drawn with linear axes; frequency plots use the user's choice.
  if(xLog_ && !forceLinear)
    plot_->setAxisScaleEngine(QwtPlot::xBottom, new QwtLogScaleEngine(10));
  else
    plot_->setAxisScaleEngine(QwtPlot::xBottom, new QwtLinearScaleEngine);

  // dB, phase and pole/zero values can be negative: log only for a
  // linear magnitude
  const bool linearMagnitude = !magnitudeDb_
    && (kind_ == MagnitudeKind || kind_ == BodeKind);
  if(yLog_ && !forceLinear && linearMagnitude)
    plot_->setAxisScaleEngine(QwtPlot::yLeft, new QwtLogScaleEngine(10));
  else
    plot_->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
}


void WorkPlane::xAxisLogScale(bool log)
{
  xLog_ = log;
  applyAxisScales(lastId_ == ZEROS || lastId_ == POLES);
  plot_->updateAxes();
  plot_->replot();
}


void WorkPlane::yAxisLogScale(bool log)
{
  yLog_ = log;
  applyAxisScales(lastId_ == ZEROS || lastId_ == POLES);
  plot_->updateAxes();
  plot_->replot();
}


void WorkPlane::plot(WorkPlane::F f)
{
  if(f == NOOP) {
    setDirty();
    return;
  }

  setEnabled(false);

  lastId_ = f;
  if(!keepAttached_) {
    if(!attached_.isEmpty()) {
      foreach(QwtPlotCurve* curve, attached_) {
        curve->setVisible(false);
        curve->detach();
      }
      attached_.clear();
    }
    // a single plot: everything on the left axis, autoscaled
    for(int i = 0; i < curves_.size(); ++i)
      curves_[i].first->setYAxis(QwtPlot::yLeft);
    plot_->enableAxis(QwtPlot::yRight, false);
    // drop the Bode plot's coloured axis titles (setAxisTitle(QString)
    // keeps the previous text colour)
    plot_->setAxisTitle(QwtPlot::yLeft, QwtText());
    plot_->setAxisTitle(QwtPlot::yRight, QwtText());
    plot_->setAxisAutoScale(QwtPlot::xBottom);
    plot_->setAxisAutoScale(QwtPlot::yLeft);
  }
  applyAxisScales(f == ZEROS || f == POLES);

  switch(f)
  {
  case MAGNITUDE:
  case MAGNITUDE_RAD:
    {
      std::pair< std::vector<double>, std::vector<double> > res;
      if(!curves_[f].second) {
        QLogger::info(QObject::tr("Calculate magnitude."));
        std::map<std::string, double> values = actValues();
        res = sapecng::magnitude(
            std::make_pair(fStart_, fStop_), pointsPerDecade_, true
          )(num_, den_, values);
      }

      plot_->setTitle(functor_traits<sapecng::magnitude>::title);
      plot_->setAxisTitle(
        QwtPlot::yLeft, functor_traits<sapecng::magnitude>::yLabel);

      if(f == MAGNITUDE)
        plot_->setAxisTitle(
          QwtPlot::xBottom, functor_traits<sapecng::magnitude>::xLabel);
      else if(f == MAGNITUDE_RAD)
        plot_->setAxisTitle(QwtPlot::xBottom,
          functor_traits<sapecng::magnitude>::xLabel + tr(" (rad/s)"));

      setupCurve(res, MAGNITUDE);
      std::vector<double> rad = res.first;
      for(std::vector<double>::iterator i = rad.begin(); i < rad.end(); ++i)
        *i = 2. * 4. * std::atan(1.) * *i;
      res.first = rad;
      setupCurve(res, MAGNITUDE_RAD);

      curves_[f].first->attach(plot_);
      curves_[f].first->setVisible(true);
      attached_.push_back(curves_[f].first);

      break;
    }
  case PHASE:
  case PHASE_RAD:
    {
      std::pair< std::vector<double>, std::vector<double> > res;
      if(!curves_[f].second) {
        QLogger::info(QObject::tr("Calculate phase."));
        std::map<std::string, double> values = actValues();
        res = sapecng::phase(
            std::make_pair(fStart_, fStop_), pointsPerDecade_, true
          )(num_, den_, values);
      }

      plot_->setTitle(functor_traits<sapecng::phase>::title);
      plot_->setAxisTitle(
        QwtPlot::yLeft, functor_traits<sapecng::phase>::yLabel);

      if(f == PHASE)
        plot_->setAxisTitle(
          QwtPlot::xBottom, functor_traits<sapecng::phase>::xLabel);
      else if(f == PHASE_RAD)
        plot_->setAxisTitle(QwtPlot::xBottom,
          functor_traits<sapecng::phase>::xLabel + tr(" (rad/s)"));

      if(phaseDegrees_)
        for(double& v : res.second)
          v *= 180. / (4. * std::atan(1.));
      setupCurve(res, PHASE);
      std::vector<double> rad = res.first;
      for(std::vector<double>::iterator i = rad.begin(); i < rad.end(); ++i)
        *i = 2. * 4. * std::atan(1.) * *i;
      res.first = rad;
      setupCurve(res, PHASE_RAD);

      curves_[f].first->attach(plot_);
      curves_[f].first->setVisible(true);
      attached_.push_back(curves_[f].first);

      break;
    }
  case GAIN:
  case GAIN_RAD:
    {
      std::pair< std::vector<double>, std::vector<double> > res;
      if(!curves_[f].second) {
        QLogger::info(QObject::tr("Calculate gain."));
        std::map<std::string, double> values = actValues();
        res = sapecng::gain(
            std::make_pair(fStart_, fStop_), pointsPerDecade_, true
          )(num_, den_, values);
      }

      plot_->setTitle(functor_traits<sapecng::gain>::title);
      plot_->setAxisTitle(
        QwtPlot::yLeft, functor_traits<sapecng::gain>::yLabel);

      if(f == GAIN)
        plot_->setAxisTitle(
          QwtPlot::xBottom, functor_traits<sapecng::gain>::xLabel);
      else if(f == GAIN_RAD)
        plot_->setAxisTitle(QwtPlot::xBottom,
          functor_traits<sapecng::gain>::xLabel + tr(" (rad/s)"));

      setupCurve(res, GAIN);
      std::vector<double> rad = res.first;
      for(std::vector<double>::iterator i = rad.begin(); i < rad.end(); ++i)
        *i = 2. * 4. * std::atan(1.) * *i;
      res.first = rad;
      setupCurve(res, GAIN_RAD);

      curves_[f].first->attach(plot_);
      curves_[f].first->setVisible(true);
      attached_.push_back(curves_[f].first);

      break;
    }
  case LOSS:
  case LOSS_RAD:
    {
      std::pair< std::vector<double>, std::vector<double> > res;
      if(!curves_[f].second) {
        QLogger::info(QObject::tr("Calculate loss."));
        std::map<std::string, double> values = actValues();
        res = sapecng::loss(
            std::make_pair(fStart_, fStop_), pointsPerDecade_, true
          )(num_, den_, values);
      }

      plot_->setTitle(functor_traits<sapecng::loss>::title);
      plot_->setAxisTitle(
        QwtPlot::yLeft, functor_traits<sapecng::loss>::yLabel);

      if(f == LOSS)
        plot_->setAxisTitle(
          QwtPlot::xBottom, functor_traits<sapecng::gain>::xLabel);
      else if(f == LOSS_RAD)
        plot_->setAxisTitle(QwtPlot::xBottom,
          functor_traits<sapecng::gain>::xLabel + tr(" (rad/s)"));

      setupCurve(res, LOSS);
      std::vector<double> rad = res.first;
      for(std::vector<double>::iterator i = rad.begin(); i < rad.end(); ++i)
        *i = 2. * 4. * std::atan(1.) * *i;
      res.first = rad;
      setupCurve(res, LOSS_RAD);

      curves_[f].first->attach(plot_);
      curves_[f].first->setVisible(true);
      attached_.push_back(curves_[f].first);

      break;
    }
  case ZEROS:
  case POLES:
    {
      std::pair< std::vector<double>, std::vector<double> > resZ;
      if(!curves_[ZEROS].second) {
        QLogger::info(QObject::tr("Looking for zeros."));
        std::map<std::string, double> values = actValues();
        resZ = sapecng::zeros()(num_, den_, values);

        unsigned int size = resZ.first.size() > resZ.second.size() ?
          resZ.second.size() : resZ.first.size();

        for(unsigned int i = 0; i < size; ++i)
          QLogger::info(QObject::tr("Zero found near to ")
            + QString("(%1, %2)")
              .arg(QString::number(resZ.first.at(i)))
              .arg(QString::number(resZ.second.at(i))));
      }

      std::pair< std::vector<double>, std::vector<double> > resP;
      if(!curves_[POLES].second) {
        QLogger::info(QObject::tr("Looking for poles."));
        std::map<std::string, double> values = actValues();
        resP = sapecng::poles()(num_, den_, values);

        unsigned int size = resP.first.size() > resP.second.size() ?
          resP.second.size() : resP.first.size();

        for(unsigned int i = 0; i < size; ++i)
          QLogger::info(QObject::tr("Pole found near to ")
            + QString("(%1, %2)")
              .arg(QString::number(resP.first.at(i)))
              .arg(QString::number(resP.second.at(i))));
      }

      plot_->setTitle(
            functor_traits<sapecng::zeros>::title
          + "/" + functor_traits<sapecng::poles>::title
        );
      plot_->setAxisTitle(
        QwtPlot::yLeft, functor_traits<sapecng::zeros>::yLabel);
      plot_->setAxisTitle(
        QwtPlot::xBottom, functor_traits<sapecng::zeros>::xLabel);

      setupCurve(resZ, ZEROS);
      setupCurve(resP, POLES);

      curves_[ZEROS].first->attach(plot_);
      curves_[POLES].first->attach(plot_);
      curves_[ZEROS].first->setVisible(true);
      curves_[POLES].first->setVisible(true);
      attached_.push_back(curves_[ZEROS].first);
      attached_.push_back(curves_[POLES].first);

      break;
    }
  default:
    break;
  }

  relabel(f);
  if(!keepAttached_) {
    if(f == PHASE || f == PHASE_RAD)
      applyYRange(QwtPlot::yLeft, phaseKey());
    else if(f != ZEROS && f != POLES)
      applyYRange(QwtPlot::yLeft, magnitudeKey());
  }

  plot_->setFocus(Qt::OtherFocusReason);
  plot_->updateAxes();
  plot_->replot();

  setEnabled(true);
}


WorkPlane::F WorkPlane::currentF() const
{
  switch(kind_) {
  case PhaseKind:
    return angular_ ? PHASE_RAD : PHASE;
  case PoleZeroKind:
    return ZEROS;
  default:
    if(magnitudeDb_)
      return angular_ ? GAIN_RAD : GAIN;
    return angular_ ? MAGNITUDE_RAD : MAGNITUDE;
  }
}


void WorkPlane::relabel(F f)
{
  // titles and axes with units
  const QString freq = (f == MAGNITUDE_RAD || f == PHASE_RAD
      || f == GAIN_RAD || f == LOSS_RAD)
    ? tr("Angular frequency (rad/s)") : tr("Frequency (Hz)");
  const QString xUnit = (f == MAGNITUDE_RAD || f == PHASE_RAD
      || f == GAIN_RAD || f == LOSS_RAD) ? QStringLiteral("rad/s")
                                         : QStringLiteral("Hz");
  QString title, yTitle, yUnit;

  switch(f) {
  case MAGNITUDE:
  case MAGNITUDE_RAD:
    title = tr("Frequency response");
    yTitle = tr("Magnitude");
    break;
  case GAIN:
  case GAIN_RAD:
    title = tr("Frequency response");
    yTitle = tr("Magnitude (dB)");
    yUnit = QStringLiteral("dB");
    break;
  case LOSS:
  case LOSS_RAD:
    title = tr("Loss");
    yTitle = tr("Loss (dB)");
    yUnit = QStringLiteral("dB");
    break;
  case PHASE:
  case PHASE_RAD:
    title = tr("Phase response");
    yTitle = phaseDegrees_ ? tr("Phase (°)") : tr("Phase (rad)");
    yUnit = phaseDegrees_ ? QStringLiteral("°") : QStringLiteral("rad");
    break;
  case ZEROS:
  case POLES:
    {
      const bool none = curves_[ZEROS].first->dataSize() == 0
        && curves_[POLES].first->dataSize() == 0;
      plot_->setTitle(none
        ? tr("No poles or zeros: the transfer function is a constant")
        : tr("Poles (×) and zeros (o)"));

      // centred on the origin, the same range on both axes
      double r = 0.;
      for(F c : { ZEROS, POLES }) {
        const QwtSeriesData<QPointF>* d = curves_[c].first->data();
        for(size_t i = 0; d && i < d->size(); ++i)
          r = qMax(r, qMax(qAbs(d->sample(i).x()), qAbs(d->sample(i).y())));
      }
      r = r > 0 ? r * 1.25 : 1.;
      plot_->setAxisScale(QwtPlot::xBottom, -r, r);
      plot_->setAxisScale(QwtPlot::yLeft, -r, r);
    }
    plot_->setAxisTitle(QwtPlot::xBottom, tr("Real part (rad/s)"));
    plot_->setAxisTitle(QwtPlot::yLeft, tr("Imaginary part (rad/s)"));
    return;
  default:
    return;
  }

  plot_->setTitle(title);
  plot_->setAxisTitle(QwtPlot::xBottom, freq);
  plot_->setAxisTitle(QwtPlot::yLeft, yTitle);
  if(MarkableCurve* mc = dynamic_cast<MarkableCurve*>(curves_[f].first))
    mc->setUnits(xUnit, yUnit);
}


void WorkPlane::showKind(int kind)
{
  kind_ = kind;
  if(kind_ == BodeKind)
    plotBode();
  else
    plot(currentF());
}


void WorkPlane::plotBode()
{
  // magnitude on the left axis, phase on the right one
  const F phaseF = angular_ ? PHASE_RAD : PHASE;
  plot(currentF());
  const F magF = lastId_;

  keepAttached_ = true;
  plot(phaseF);
  keepAttached_ = false;
  lastId_ = magF;

  curves_[phaseF].first->setYAxis(QwtPlot::yRight);
  plot_->enableAxis(QwtPlot::yRight, true);
  plot_->setAxisAutoScale(QwtPlot::yRight);
  plot_->setAxisScaleEngine(QwtPlot::yRight, new QwtLinearScaleEngine);
  applyAxisScales(false);
  applyYRange(QwtPlot::yLeft, magnitudeKey());
  applyYRange(QwtPlot::yRight, phaseKey());

  relabel(magF);
  plot_->setTitle(tr("Bode plot"));

  // axis titles in the colour of their curve
  QwtText left = plot_->axisTitle(QwtPlot::yLeft);
  left.setColor(curves_[magF].first->pen().color());
  plot_->setAxisTitle(QwtPlot::yLeft, left);
  QwtText right(phaseDegrees_ ? tr("Phase (°)") : tr("Phase (rad)"));
  right.setColor(curves_[phaseF].first->pen().color());
  right.setFont(left.font());
  plot_->setAxisTitle(QwtPlot::yRight, right);

  plot_->updateAxes();
  plot_->replot();
}


QString WorkPlane::magnitudeKey() const
{
  return magnitudeDb_ ? QStringLiteral("magDb") : QStringLiteral("mag");
}


QString WorkPlane::phaseKey() const
{
  return phaseDegrees_ ? QStringLiteral("phaseDeg") : QStringLiteral("phaseRad");
}


WorkPlane::YRange WorkPlane::yRange(const QString& key) const
{
  QSettings settings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION);
  const QString base = QStringLiteral("Workplane/yRange/%1/").arg(key);
  YRange r;
  r.automatic = settings.value(base + "auto", true).toBool();
  r.min = settings.value(base + "min", 0.).toDouble();
  r.max = settings.value(base + "max", 1.).toDouble();
  if(!(r.max > r.min))
    r.automatic = true;
  return r;
}


void WorkPlane::setYRange(const QString& key, const YRange& r)
{
  QSettings settings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION);
  const QString base = QStringLiteral("Workplane/yRange/%1/").arg(key);
  settings.setValue(base + "auto", r.automatic);
  settings.setValue(base + "min", r.min);
  settings.setValue(base + "max", r.max);
}


void WorkPlane::applyYRange(int axis, const QString& key)
{
  const YRange r = yRange(key);
  if(r.automatic)
    plot_->setAxisAutoScale(axis);
  else
    plot_->setAxisScale(axis, r.min, r.max);
}


void WorkPlane::resetView()
{
  if(lastId_ != NOOP)
    showKind(kind_);   // curves are cached: this only rescales
}


void WorkPlane::editVerticalRange()
{
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Vertical Range"));

  struct Axis {
    QString key, title;
    QCheckBox* automatic; QLineEdit* min; QLineEdit* max;
  };
  QList<Axis> axes;
  if(kind_ == PhaseKind)
    axes << Axis{ phaseKey(), phaseDegrees_ ? tr("Phase (°)") : tr("Phase (rad)") };
  else if(kind_ == BodeKind)
    axes << Axis{ magnitudeKey(),
                  magnitudeDb_ ? tr("Magnitude (dB)") : tr("Magnitude") }
         << Axis{ phaseKey(), phaseDegrees_ ? tr("Phase (°)") : tr("Phase (rad)") };
  else
    axes << Axis{ magnitudeKey(),
                  magnitudeDb_ ? tr("Magnitude (dB)") : tr("Magnitude") };

  QVBoxLayout* layout = new QVBoxLayout(&dialog);
  QDialogButtonBox* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  auto validate = [&]() {
    bool ok = true;
    for(const Axis& a : axes) {
      a.min->setEnabled(!a.automatic->isChecked());
      a.max->setEnabled(!a.automatic->isChecked());
      if(!a.automatic->isChecked()) {
        bool okMin = false, okMax = false;
        const double lo = a.min->text().toDouble(&okMin);
        const double hi = a.max->text().toDouble(&okMax);
        ok = ok && okMin && okMax && hi > lo;
      }
    }
    buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
  };

  for(Axis& a : axes) {
    const YRange r = yRange(a.key);
    // automatic: show the range on screen as a starting point
    const int axisId = (&a == &axes.last() && kind_ == BodeKind)
      ? QwtPlot::yRight : QwtPlot::yLeft;
    const QwtInterval shown = plot_->axisInterval(axisId);
    a.automatic = new QCheckBox(tr("&Automatic"));
    a.automatic->setChecked(r.automatic);
    a.min = new QLineEdit(QString::number(r.automatic ? shown.minValue() : r.min, 'g', 6));
    a.max = new QLineEdit(QString::number(r.automatic ? shown.maxValue() : r.max, 'g', 6));
    a.min->setValidator(new QDoubleValidator(a.min));
    a.max->setValidator(new QDoubleValidator(a.max));

    QFormLayout* form = new QFormLayout;
    form->addRow(a.automatic);
    form->addRow(tr("Minimum:"), a.min);
    form->addRow(tr("Maximum:"), a.max);
    QGroupBox* box = new QGroupBox(a.title);
    box->setLayout(form);
    layout->addWidget(box);

    connect(a.automatic, &QCheckBox::toggled, &dialog, validate);
    connect(a.min, &QLineEdit::textChanged, &dialog, validate);
    connect(a.max, &QLineEdit::textChanged, &dialog, validate);
  }

  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  validate();

  if(dialog.exec() != QDialog::Accepted)
    return;

  for(const Axis& a : axes) {
    YRange r;
    r.automatic = a.automatic->isChecked();
    r.min = a.min->text().toDouble();
    r.max = a.max->text().toDouble();
    setYRange(a.key, r);
  }
  resetView();
}


bool WorkPlane::eventFilter(QObject* watched, QEvent* event)
{
  if(watched == plot_->canvas() && event->type() == QEvent::Wheel) {
    QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
    const int delta = wheel->angleDelta().y();
    if(delta != 0 && lastId_ != NOOP) {
      // Shift: frequency axis only, Ctrl: vertical axis only
      const bool onlyX = wheel->modifiers() & Qt::ShiftModifier;
      const bool onlyY = wheel->modifiers() & Qt::ControlModifier;
      zoomAt(wheel->position().toPoint(), std::pow(1.2, delta / 120.),
        !onlyY, !onlyX);
    }
    return true;
  }
  return QWidget::eventFilter(watched, event);
}


void WorkPlane::zoomAt(const QPoint& pos, double factor, bool x, bool y)
{
  // zoom in pixel space around the pointer; invTransform takes care of
  // logarithmic axes
  auto zoomAxis = [&](int axis, double c) {
    const QwtScaleMap map = plot_->canvasMap(axis);
    const double p1 = c + (map.p1() - c) / factor;
    const double p2 = c + (map.p2() - c) / factor;
    double a = map.invTransform(p1), b = map.invTransform(p2);
    if(a > b) std::swap(a, b);
    if(std::isfinite(a) && std::isfinite(b) && b > a)
      plot_->setAxisScale(axis, a, b);
  };

  if(x)
    zoomAxis(QwtPlot::xBottom, pos.x());
  if(y) {
    zoomAxis(QwtPlot::yLeft, pos.y());
    if(plot_->isAxisVisible(QwtPlot::yRight))
      zoomAxis(QwtPlot::yRight, pos.y());
  }
  plot_->replot();
}


void WorkPlane::invalidateCurves()
{
  for(int i = 0; i < curves_.size(); ++i)
    curves_[i].second = false;
}


void WorkPlane::autoFrequencyRange()
{
  // a decade below the lowest and above the highest pole or zero
  const double twoPi = 8. * std::atan(1.);
  double lo = 0., hi = 0.;
  for(int which = 0; which < 2; ++which) {
    const std::pair< std::vector<double>, std::vector<double> > roots =
      which == 0 ? sapecng::zeros()(num_, den_, values_)
                 : sapecng::poles()(num_, den_, values_);
    const size_t n = qMin(roots.first.size(), roots.second.size());
    for(size_t i = 0; i < n; ++i) {
      const double w = std::hypot(roots.first[i], roots.second[i]);
      if(w <= 0. || !std::isfinite(w))
        continue;
      lo = lo > 0. ? qMin(lo, w) : w;
      hi = qMax(hi, w);
    }
  }

  if(hi > 0.) {
    fStart_ = std::pow(10., std::floor(std::log10(lo / twoPi / 10.)));
    fStop_ = std::pow(10., std::ceil(std::log10(hi / twoPi * 10.)));
  } else {
    fStart_ = 1.;     // no poles or zeros: any range shows the same
    fStop_ = 1e6;
  }
  if(fStop_ <= fStart_)
    fStop_ = fStart_ * 1000.;
}


void WorkPlane::editFrequencyRange()
{
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Frequency Range"));

  QCheckBox* automatic = new QCheckBox(
    tr("&Automatic: a decade beyond the poles and zeros"));
  automatic->setChecked(autoRange_);

  auto field = [](double value) {
    QLineEdit* edit = new QLineEdit(
      units::formatForEdit(QString::number(value, 'g', 8)));
    edit->setValidator(new units::ValueValidator(QStringLiteral("Hz"), edit));
    edit->setToolTip(tr("Frequency in Hz; prefixes allowed: 10, 1k, 2.5MEG"));
    return edit;
  };
  QLineEdit* start = field(fStart_);
  QLineEdit* stop = field(fStop_);

  QSpinBox* points = new QSpinBox;
  points->setRange(10, 2000);
  points->setValue(pointsPerDecade_);
  points->setToolTip(tr("Points per decade, spaced logarithmically"));

  QFormLayout* form = new QFormLayout;
  form->addRow(automatic);
  form->addRow(tr("&Start (Hz):"), start);
  form->addRow(tr("St&op (Hz):"), stop);
  form->addRow(tr("&Points per decade:"), points);

  QDialogButtonBox* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto validate = [&]() {
    double a = 0., b = 0.;
    const bool ok = automatic->isChecked()
      || (units::parse(start->text(), "Hz", &a)
          && units::parse(stop->text(), "Hz", &b) && a > 0 && b > a);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
    start->setEnabled(!automatic->isChecked());
    stop->setEnabled(!automatic->isChecked());
  };
  connect(automatic, &QCheckBox::toggled, &dialog, validate);
  connect(start, &QLineEdit::textChanged, &dialog, validate);
  connect(stop, &QLineEdit::textChanged, &dialog, validate);
  validate();

  QVBoxLayout* layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);

  if(dialog.exec() != QDialog::Accepted)
    return;

  autoRange_ = automatic->isChecked();
  pointsPerDecade_ = points->value();
  if(autoRange_) {
    autoFrequencyRange();
  } else {
    units::parse(start->text(), "Hz", &fStart_);
    units::parse(stop->text(), "Hz", &fStop_);
  }

  QSettings settings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION);
  settings.setValue("Workplane/autoRange", autoRange_);
  settings.setValue("Workplane/fStart", fStart_);
  settings.setValue("Workplane/fStop", fStop_);
  settings.setValue("Workplane/pointsPerDecade", pointsPerDecade_);

  invalidateCurves();
  if(lastId_ != NOOP)
    showKind(kind_);
}


void WorkPlane::setMagnitudeDb(bool db)
{
  magnitudeDb_ = db;
  if(lastId_ != NOOP && (kind_ == MagnitudeKind || kind_ == BodeKind))
    showKind(kind_);
}


void WorkPlane::setPhaseDegrees(bool degrees)
{
  if(phaseDegrees_ == degrees)
    return;
  phaseDegrees_ = degrees;
  // phase curves hold converted values: compute them again
  curves_[PHASE].second = false;
  curves_[PHASE_RAD].second = false;
  if(lastId_ != NOOP && (kind_ == PhaseKind || kind_ == BodeKind))
    showKind(kind_);
}


void WorkPlane::setAngularFrequency(bool radPerSecond)
{
  angular_ = radPerSecond;
  if(lastId_ != NOOP && kind_ != PoleZeroKind)
    showKind(kind_);
}


void WorkPlane::plot(int f)
{
  plot((F) f);
}


void WorkPlane::redraw()
{
  const bool shown = lastId_ != NOOP;
  setDirty();
  if(shown)
    showKind(kind_);
}


void WorkPlane::print(QPrinter& printer) {
  // print on white, also when the dark theme is active
  const QPalette paper = theme::paperPalette();
  plot_->setPalette(paper);
  applyPlotStyle(paper, plot_, grid_, curves_, ZEROS, POLES);

  QwtPlotRenderer().renderTo(plot_, printer);

  plot_->setPalette(QPalette());   // back to the inherited palette
  applyPlotStyle(QApplication::palette(), plot_, grid_, curves_, ZEROS, POLES);
  plot_->replot();
}


std::map<std::string, double> WorkPlane::actValues() const
{
  return values_;
}


void WorkPlane::createMainLayout()
{
  plot_ = new QwtPlot_ContextMenu(this);
  plot_->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
  plot_->setAxisScaleEngine(QwtPlot::xBottom, new QwtLogScaleEngine(10));
  plot_->setAutoReplot(true);

  tracker_ = new QwtPlotPicker(plot_->canvas());
  clickPointMachine_ = new QwtPickerDragPointMachine;
  tracker_->setStateMachine(clickPointMachine_);
  tracker_->setRubberBand(QwtPicker::VLineRubberBand);
  tracker_->setTrackerMode(QwtPicker::AlwaysOff);

  // middle button pans, the wheel zooms (see eventFilter)
  QwtPlotPanner* panner = new QwtPlotPanner(plot_->canvas());
  panner->setMouseButton(Qt::MiddleButton);
  plot_->canvas()->installEventFilter(this);

  grid_ = new QwtPlotGrid;
  grid_->setPen(QPen(Qt::gray, 0., Qt::DotLine));
  grid_->attach(plot_);
  plot_->replot();

  centralLayout_ = new QVBoxLayout;
  centralLayout_->addWidget(plot_);

  mainLayout_ = new QHBoxLayout;
  mainLayout_->setContentsMargins(0, 0, 0, 0);
  mainLayout_->addLayout(centralLayout_);
}


void WorkPlane::setupCurves()
{
  curves_ = QVector< QPair< QwtPlotCurve*, bool > >(NOOP);
  QwtSymbol* symbol;

  symbol = new QwtSymbol;
  symbol->setStyle(functor_traits<sapecng::magnitude>::symbol);
  symbol->setSize(functor_traits<sapecng::magnitude>::ssize);
  curves_[MAGNITUDE].first = new MarkableCurve;
  curves_[MAGNITUDE].first->setPen(functor_traits<sapecng::magnitude>::pen);
  curves_[MAGNITUDE].first->setStyle(functor_traits<sapecng::magnitude>::style);
  curves_[MAGNITUDE].first->setSymbol(symbol);
  curves_[MAGNITUDE].first->setCurveAttribute(
    functor_traits<sapecng::magnitude>::attribute);

  connect(tracker_, SIGNAL(selected(const QPointF&)),
    (MarkableCurve*) curves_[MAGNITUDE].first, SLOT(selected(const QPointF&)));
  connect(tracker_, SIGNAL(appended(const QPointF&)),
    (MarkableCurve*) curves_[MAGNITUDE].first,
    SLOT(appended(const QPointF&)));
  connect(tracker_, SIGNAL(moved(const QPointF&)),
    (MarkableCurve*) curves_[MAGNITUDE].first,
    SLOT(moved(const QPointF&)));

  symbol = new QwtSymbol;
  symbol->setStyle(functor_traits<sapecng::magnitude>::symbol);
  symbol->setSize(functor_traits<sapecng::magnitude>::ssize);
  curves_[MAGNITUDE_RAD].first = new MarkableCurve;
  curves_[MAGNITUDE_RAD].first->setPen(functor_traits<sapecng::magnitude>::pen);
  curves_[MAGNITUDE_RAD].first->setStyle(functor_traits<sapecng::magnitude>::style);
  curves_[MAGNITUDE_RAD].first->setSymbol(symbol);
  curves_[MAGNITUDE_RAD].first->setCurveAttribute(
    functor_traits<sapecng::magnitude>::attribute);

  connect(tracker_, SIGNAL(selected(const QPointF&)),
    (MarkableCurve*) curves_[MAGNITUDE_RAD].first, SLOT(selected(const QPointF&)));
  connect(tracker_, SIGNAL(appended(const QPointF&)),
    (MarkableCurve*) curves_[MAGNITUDE_RAD].first,
    SLOT(appended(const QPointF&)));
  connect(tracker_, SIGNAL(moved(const QPointF&)),
    (MarkableCurve*) curves_[MAGNITUDE_RAD].first,
    SLOT(moved(const QPointF&)));

  symbol = new QwtSymbol;
  symbol->setStyle(functor_traits<sapecng::phase>::symbol);
  symbol->setSize(functor_traits<sapecng::phase>::ssize);
  curves_[PHASE].first = new MarkableCurve;
  curves_[PHASE].first->setPen(functor_traits<sapecng::phase>::pen);
  curves_[PHASE].first->setStyle(functor_traits<sapecng::phase>::style);
  curves_[PHASE].first->setSymbol(symbol);
  curves_[PHASE].first->setCurveAttribute(
    functor_traits<sapecng::phase>::attribute);

  connect(tracker_, SIGNAL(selected(const QPointF&)),
    (MarkableCurve*) curves_[PHASE].first, SLOT(selected(const QPointF&)));
  connect(tracker_, SIGNAL(appended(const QPointF&)),
    (MarkableCurve*) curves_[PHASE].first,
    SLOT(appended(const QPointF&)));
  connect(tracker_, SIGNAL(moved(const QPointF&)),
    (MarkableCurve*) curves_[PHASE].first, SLOT(moved(const QPointF&)));

  symbol = new QwtSymbol;
  symbol->setStyle(functor_traits<sapecng::phase>::symbol);
  symbol->setSize(functor_traits<sapecng::phase>::ssize);
  curves_[PHASE_RAD].first = new MarkableCurve;
  curves_[PHASE_RAD].first->setPen(functor_traits<sapecng::phase>::pen);
  curves_[PHASE_RAD].first->setStyle(functor_traits<sapecng::phase>::style);
  curves_[PHASE_RAD].first->setSymbol(symbol);
  curves_[PHASE_RAD].first->setCurveAttribute(
    functor_traits<sapecng::phase>::attribute);

  connect(tracker_, SIGNAL(selected(const QPointF&)),
    (MarkableCurve*) curves_[PHASE_RAD].first, SLOT(selected(const QPointF&)));
  connect(tracker_, SIGNAL(appended(const QPointF&)),
    (MarkableCurve*) curves_[PHASE_RAD].first,
    SLOT(appended(const QPointF&)));
  connect(tracker_, SIGNAL(moved(const QPointF&)),
    (MarkableCurve*) curves_[PHASE_RAD].first,
    SLOT(moved(const QPointF&)));

  symbol = new QwtSymbol;
  symbol->setStyle(functor_traits<sapecng::gain>::symbol);
  symbol->setSize(functor_traits<sapecng::gain>::ssize);
  curves_[GAIN].first = new MarkableCurve;
  curves_[GAIN].first->setPen(functor_traits<sapecng::gain>::pen);
  curves_[GAIN].first->setStyle(functor_traits<sapecng::gain>::style);
  curves_[GAIN].first->setSymbol(symbol);
  curves_[GAIN].first->setCurveAttribute(
    functor_traits<sapecng::gain>::attribute);

  connect(tracker_, SIGNAL(selected(const QPointF&)),
    (MarkableCurve*) curves_[GAIN].first, SLOT(selected(const QPointF&)));
  connect(tracker_, SIGNAL(appended(const QPointF&)),
    (MarkableCurve*) curves_[GAIN].first,
    SLOT(appended(const QPointF&)));
  connect(tracker_, SIGNAL(moved(const QPointF&)),
    (MarkableCurve*) curves_[GAIN].first, SLOT(moved(const QPointF&)));

  symbol = new QwtSymbol;
  symbol->setStyle(functor_traits<sapecng::gain>::symbol);
  symbol->setSize(functor_traits<sapecng::gain>::ssize);
  curves_[GAIN_RAD].first = new MarkableCurve;
  curves_[GAIN_RAD].first->setPen(functor_traits<sapecng::gain>::pen);
  curves_[GAIN_RAD].first->setStyle(functor_traits<sapecng::gain>::style);
  curves_[GAIN_RAD].first->setSymbol(symbol);
  curves_[GAIN_RAD].first->setCurveAttribute(
    functor_traits<sapecng::gain>::attribute);

  connect(tracker_, SIGNAL(selected(const QPointF&)),
    (MarkableCurve*) curves_[GAIN_RAD].first, SLOT(selected(const QPointF&)));
  connect(tracker_, SIGNAL(appended(const QPointF&)),
    (MarkableCurve*) curves_[GAIN_RAD].first,
    SLOT(appended(const QPointF&)));
  connect(tracker_, SIGNAL(moved(const QPointF&)),
    (MarkableCurve*) curves_[GAIN_RAD].first,
    SLOT(moved(const QPointF&)));

  symbol = new QwtSymbol;
  symbol->setStyle(functor_traits<sapecng::loss>::symbol);
  symbol->setSize(functor_traits<sapecng::loss>::ssize);
  curves_[LOSS].first = new MarkableCurve;
  curves_[LOSS].first->setPen(functor_traits<sapecng::loss>::pen);
  curves_[LOSS].first->setStyle(functor_traits<sapecng::loss>::style);
  curves_[LOSS].first->setSymbol(symbol);
  curves_[LOSS].first->setCurveAttribute(
    functor_traits<sapecng::loss>::attribute);

  connect(tracker_, SIGNAL(selected(const QPointF&)),
    (MarkableCurve*) curves_[LOSS].first, SLOT(selected(const QPointF&)));
  connect(tracker_, SIGNAL(appended(const QPointF&)),
    (MarkableCurve*) curves_[LOSS].first, SLOT(appended(const QPointF&)));
  connect(tracker_, SIGNAL(moved(const QPointF&)),
    (MarkableCurve*) curves_[LOSS].first, SLOT(moved(const QPointF&)));

  symbol = new QwtSymbol;
  symbol->setStyle(functor_traits<sapecng::loss>::symbol);
  symbol->setSize(functor_traits<sapecng::loss>::ssize);
  curves_[LOSS_RAD].first = new MarkableCurve;
  curves_[LOSS_RAD].first->setPen(functor_traits<sapecng::loss>::pen);
  curves_[LOSS_RAD].first->setStyle(functor_traits<sapecng::loss>::style);
  curves_[LOSS_RAD].first->setSymbol(symbol);
  curves_[LOSS_RAD].first->setCurveAttribute(
    functor_traits<sapecng::loss>::attribute);

  connect(tracker_, SIGNAL(selected(const QPointF&)),
    (MarkableCurve*) curves_[LOSS_RAD].first, SLOT(selected(const QPointF&)));
  connect(tracker_, SIGNAL(appended(const QPointF&)),
    (MarkableCurve*) curves_[LOSS_RAD].first,
    SLOT(appended(const QPointF&)));
  connect(tracker_, SIGNAL(moved(const QPointF&)),
    (MarkableCurve*) curves_[LOSS_RAD].first,
    SLOT(moved(const QPointF&)));

  symbol = new QwtSymbol;
  symbol->setStyle(functor_traits<sapecng::zeros>::symbol);
  symbol->setSize(functor_traits<sapecng::zeros>::ssize);
  curves_[ZEROS].first = new MarkedCurve;
  curves_[ZEROS].first->setPen(functor_traits<sapecng::zeros>::pen);
  curves_[ZEROS].first->setStyle(functor_traits<sapecng::zeros>::style);
  curves_[ZEROS].first->setSymbol(symbol);
  curves_[ZEROS].first->setCurveAttribute(
    functor_traits<sapecng::zeros>::attribute);

  symbol = new QwtSymbol;
  symbol->setStyle(functor_traits<sapecng::poles>::symbol);
  symbol->setSize(functor_traits<sapecng::poles>::ssize);
  curves_[POLES].first = new MarkedCurve;
  curves_[POLES].first->setPen(functor_traits<sapecng::poles>::pen);
  curves_[POLES].first->setStyle(functor_traits<sapecng::poles>::style);
  curves_[POLES].first->setSymbol(symbol);
  curves_[POLES].first->setCurveAttribute(
    functor_traits<sapecng::poles>::attribute);

  for(int i = 0; i < curves_.size(); ++i) {
    // plain polyline through the samples: the sweep is dense enough
    // (log-spaced), and point weeding left visible kinks
    curves_[i].first->setCurveAttribute(QwtPlotCurve::Fitted, false);
    curves_[i].first->setVisible(false);
    curves_[i].second = false;
  }

  applyPlotStyle(QApplication::palette(), plot_, grid_, curves_, ZEROS, POLES);

  lastId_ = NOOP;

  // units in the marker labels
  const struct { F f; const char* x; const char* y; } units[] = {
    { MAGNITUDE, "Hz", "" }, { MAGNITUDE_RAD, "rad/s", "" },
    { PHASE, "Hz", "rad" }, { PHASE_RAD, "rad/s", "rad" },
    { GAIN, "Hz", "dB" }, { GAIN_RAD, "rad/s", "dB" },
    { LOSS, "Hz", "dB" }, { LOSS_RAD, "rad/s", "dB" }
  };
  for(const auto& u : units)
    if(MarkableCurve* mc = dynamic_cast<MarkableCurve*>(curves_[u.f].first))
      mc->setUnits(QString::fromLatin1(u.x), QString::fromLatin1(u.y));
}


void WorkPlane::clearMarkers()
{
  for(int i = 0; i < curves_.size(); ++i)
    if(MarkableCurve* mc = dynamic_cast<MarkableCurve*>(curves_[i].first))
      mc->clearPins();
}


void WorkPlane::setupCurve(
    std::pair< std::vector<double>, std::vector<double> > data,
    WorkPlane::F f
  )
{
  if(!curves_[f].second) {
    curves_[f].second = true;

    double* x = new double [data.first.size()];
    std::copy(data.first.begin(), data.first.end(), x);
    double* y = new double [data.second.size()];
    std::copy(data.second.begin(), data.second.end(), y);

    if(MarkableCurve* mc = dynamic_cast<MarkableCurve*>(curves_[f].first))
      mc->clearPins();   // new data: old markers no longer apply

    curves_[f].first->setSamples(x, y,
        data.first.size() < data.second.size() ?
            data.first.size()
          : data.second.size()
      );

    delete [] y;
    delete [] x;
  }
}


}
