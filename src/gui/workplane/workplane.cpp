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
#include <algorithm>
#include <QSplitter>
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
#include <QFontMetrics>
#include <QScrollBar>
#include <qwt_scale_widget.h>
#include <qwt_scale_draw.h>
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
#include <qwt_scale_div.h>
#include <qwt_painter.h>
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
  marker_->detach();
  delete marker_;
  // delete symbol_;
}


void MarkableCurve::setVisible(bool on)
{
  marker_->setVisible(false);

  if(on) marker_->attach(plot());
  else marker_->detach();

  QwtPlotCurve::setVisible(on);
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


void MarkableCurve::selected(const QPointF&)
{
  // the live readout ends; WorkPlane::plotClicked places markers
  marker_->setVisible(false);
  if(plot())
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
    marker->setLabelAlignment(Qt::AlignBottom | Qt::AlignRight);
    marker->setValue(data()->sample(i).x(), data()->sample(i).y());

    // s = -1.2k or -500 + j3.1k (rad/s)
    const double re = data()->sample(i).x(), im = data()->sample(i).y();
    auto si = [](double v) {
        return units::format(QString::number(v, 'g', 4), QString()); };
    QString label = si(re);
    if(im != 0.)
      label += (im > 0 ? QStringLiteral(" + j") : QStringLiteral(" − j"))
        + si(std::fabs(im));
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
    curve->setRenderHint(QwtPlotItem::RenderAntialiased, WorkPlane::antialiased());

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


namespace {

// Qwt rounds the points of a curve to whole pixels on screen, so even an
// antialiased curve came out in steps; off, they keep their fractional
// positions (items drawn without antialiasing are rounded by QPainter
// anyway: grid, axes, marker lines look the same)
const bool noRounding = []() {
    QwtPainter::setRoundingAlignment(false);
    return true;
  }();

}


WorkPlane::~WorkPlane()
{
  // the Markers panel may be in the plot window's dock: it goes with the
  // plot (its buttons act on this plot)
  delete markerPanel_;

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
      // labels drawn in the old theme's colours (pole/zero values, the
      // live readout): recoloured; the frequency markers are redrawn
      const QPalette pal = QApplication::palette();
      foreach(QwtPlotItem* item, plot_->itemList(QwtPlotItem::Rtti_PlotMarker)) {
        QwtPlotMarker* m = static_cast<QwtPlotMarker*>(item);
        QwtText label = m->label();
        if(label.isEmpty())
          continue;
        label.setColor(pal.color(QPalette::Text));
        if(label.backgroundBrush().style() != Qt::NoBrush) {
          QColor bg = pal.color(QPalette::Base);
          bg.setAlpha(220);
          label.setBackgroundBrush(bg);
        }
        m->setLabel(label);
      }
      refreshMarkers();
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

  if(!keepAttached_)
    refreshMarkers();

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
      // the same scale on both axes (a circle stays a circle), once the
      // canvas has its size
      QTimer::singleShot(0, this, [this]() { equalizeAxes(); });
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
  refreshMarkers();
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
  if(r.automatic) {
    // the range of the curves rounded out to major ticks, plus one major
    // step above and below: a curve never runs along the frame
    double lo = 0., hi = 0.;
    bool any = false;
    for(QwtPlotItem* item : plot_->itemList(QwtPlotItem::Rtti_PlotCurve)) {
      if(!item->isVisible() || item->yAxis() != axis)
        continue;
      const QRectF b = static_cast<QwtPlotCurve*>(item)->boundingRect();
      if(!b.isValid() || !std::isfinite(b.top()) || !std::isfinite(b.bottom()))
        continue;
      lo = any ? qMin(lo, b.top()) : b.top();
      hi = any ? qMax(hi, b.bottom()) : b.bottom();
      any = true;
    }
    QwtScaleEngine* engine = plot_->axisScaleEngine(axis);
    const bool log = engine && dynamic_cast<QwtLogScaleEngine*>(engine);
    if(!any || !engine || (log && lo <= 0.)) {
      plot_->setAxisAutoScale(axis);
      return;
    }
    double step = 0.;
    engine->autoScale(plot_->axisMaxMajor(axis), lo, hi, step);
    const QwtScaleDiv div = engine->divideScale(lo, hi,
      plot_->axisMaxMajor(axis), plot_->axisMaxMinor(axis), step);
    const QList<double> majors = div.ticks(QwtScaleDiv::MajorTick);
    if(majors.size() >= 2)
      step = log ? std::log10(majors.at(1) / majors.at(0))
                 : majors.at(1) - majors.at(0);
    if(!(step > 0.)) {
      plot_->setAxisAutoScale(axis);
      return;
    }
    if(log) {
      lo /= std::pow(10., step);
      hi *= std::pow(10., step);
    } else {
      lo -= step;
      hi += step;
    }
    plot_->setAxisScale(axis, lo, hi, log ? 0. : step);
  } else
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


void WorkPlane::equalizeAxes()
{
  // pole-zero map: equal units per pixel on both axes, the larger of the
  // two, around the present centre
  if(lastId_ != ZEROS && lastId_ != POLES)
    return;
  const QWidget* canvas = plot_->canvas();
  const double w = canvas->width(), h = canvas->height();
  if(w < 10 || h < 10)
    return;
  const QwtInterval xi = plot_->axisInterval(QwtPlot::xBottom);
  const QwtInterval yi = plot_->axisInterval(QwtPlot::yLeft);
  const double u = qMax(xi.width() / w, yi.width() / h);
  if(!(u > 0) || !std::isfinite(u))
    return;
  const double cx = (xi.minValue() + xi.maxValue()) / 2;
  const double cy = (yi.minValue() + yi.maxValue()) / 2;
  plot_->setAxisScale(QwtPlot::xBottom, cx - u * w / 2, cx + u * w / 2);
  plot_->setAxisScale(QwtPlot::yLeft, cy - u * h / 2, cy + u * h / 2);
  plot_->replot();
}


bool WorkPlane::eventFilter(QObject* watched, QEvent* event)
{
  if(watched == plot_->canvas() && event->type() == QEvent::Resize
      && (lastId_ == ZEROS || lastId_ == POLES))
    QTimer::singleShot(0, this, [this]() { equalizeAxes(); });

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


PlotSettings PlotSettings::load()
{
  QSettings q(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION);
  PlotSettings p;
  p.kind = q.value("Plot/kind", p.kind).toInt();
  p.magnitudeDb = q.value("Workplane/magnitudeDb", p.magnitudeDb).toBool();
  p.phaseDegrees = q.value("Workplane/phaseDegrees", p.phaseDegrees).toBool();
  p.angular = q.value("Workplane/angularFrequency", p.angular).toBool();
  p.xLog = q.value("Workplane/xLog", p.xLog).toBool();
  p.yLog = q.value("Workplane/yLog", p.yLog).toBool();
  p.autoRange = q.value("Workplane/autoRange", p.autoRange).toBool();
  p.fStart = q.value("Workplane/fStart", p.fStart).toDouble();
  p.fStop = q.value("Workplane/fStop", p.fStop).toDouble();
  p.pointsPerDecade =
    q.value("Workplane/pointsPerDecade", p.pointsPerDecade).toInt();
  if(!(p.fStart > 0 && p.fStop > p.fStart)) { p.fStart = 1.; p.fStop = 1e5; }
  if(p.kind < 0 || p.kind > 3) p.kind = 3;
  return p;
}


void PlotSettings::save() const
{
  QSettings q(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION);
  q.setValue("Plot/kind", kind);
  q.setValue("Workplane/magnitudeDb", magnitudeDb);
  q.setValue("Workplane/phaseDegrees", phaseDegrees);
  q.setValue("Workplane/angularFrequency", angular);
  q.setValue("Workplane/xLog", xLog);
  q.setValue("Workplane/yLog", yLog);
  q.setValue("Workplane/autoRange", autoRange);
  q.setValue("Workplane/fStart", fStart);
  q.setValue("Workplane/fStop", fStop);
  q.setValue("Workplane/pointsPerDecade", pointsPerDecade);
}


PlotSettings WorkPlane::settings() const
{
  PlotSettings p;
  p.kind = kind_;
  p.magnitudeDb = magnitudeDb_;
  p.phaseDegrees = phaseDegrees_;
  p.angular = angular_;
  p.xLog = xLog_;
  p.yLog = yLog_;
  p.autoRange = autoRange_;
  p.fStart = fStart_;
  p.fStop = fStop_;
  p.pointsPerDecade = pointsPerDecade_;
  p.output = output_;
  return p;
}


void WorkPlane::applySettings(const PlotSettings& p)
{
  const bool sweepChanged = p.autoRange != autoRange_
    || p.fStart != fStart_ || p.fStop != fStop_
    || p.pointsPerDecade != pointsPerDecade_
    || p.phaseDegrees != phaseDegrees_;

  kind_ = p.kind;
  output_ = p.output;
  magnitudeDb_ = p.magnitudeDb;
  phaseDegrees_ = p.phaseDegrees;
  angular_ = p.angular;
  xLog_ = p.xLog;
  yLog_ = p.yLog;
  autoRange_ = p.autoRange;
  fStart_ = p.fStart;
  fStop_ = p.fStop;
  pointsPerDecade_ = p.pointsPerDecade;

  if(autoRange_ && !num_.empty())
    autoFrequencyRange();
  if(sweepChanged)
    invalidateCurves();
  if(!num_.empty())
    showKind(kind_);
}


bool WorkPlane::markerValues(double hz, double* magnitude, double* phase) const
{
  // evaluated from the transfer function itself (not the plotted samples)
  if(num_.empty() || !(hz > 0))
    return false;
  const std::pair<double, double> at(hz, hz * 1.000001);
  std::pair< std::vector<double>, std::vector<double> > m =
    magnitudeDb_ ? sapecng::gain(at, 1., true)(num_, den_, values_)
                 : sapecng::magnitude(at, 1., true)(num_, den_, values_);
  std::pair< std::vector<double>, std::vector<double> > p =
    sapecng::phase(at, 1., true)(num_, den_, values_);
  if(m.second.empty() || p.second.empty())
    return false;
  *magnitude = m.second.front();
  *phase = phaseDegrees_ ? p.second.front() * 180. / (4. * std::atan(1.))
                         : p.second.front();
  return true;
}


QString WorkPlane::magnitudeText(double v) const
{
  if(std::fabs(v) < 5e-3 && magnitudeDb_)
    v = 0.;     // no "-0.00 dB"
  return magnitudeDb_ ? QStringLiteral("%1 dB").arg(v, 0, 'f', 2)
                      : units::format(QString::number(v, 'g', 6), "");
}


QString WorkPlane::phaseText(double v) const
{
  if(std::fabs(v) < (phaseDegrees_ ? 0.05 : 5e-4))
    v = 0.;     // no "-0.0°"
  return phaseDegrees_ ? QStringLiteral("%1°").arg(v, 0, 'f', 1)
                       : QStringLiteral("%1 rad").arg(v, 0, 'f', 3);
}


void WorkPlane::refreshMarkers()
{
  if(!markerPanel_ || !markerTable_)
    return;
  foreach(QwtPlotMarker* m, markerLines_) {
    m->detach();
    delete m;
  }
  markerLines_.clear();

  // markers belong to frequency plots (not to the pole-zero map)
  markerPanel_->setEnabled(hasFrequencyAxis());

  fillingTable_ = true;
  markerTable_->setRowCount(markers_.size());
  const double twoPi = 8. * std::atan(1.);
  // labels of neighbouring markers go to different heights
  const QwtScaleMap xMap = plot_->canvasMap(QwtPlot::xBottom);
  const Qt::Alignment rows[] = { Qt::AlignTop, Qt::AlignBottom, Qt::AlignVCenter };
  double lastPx[3] = { -1e9, -1e9, -1e9 };

  // differential markers: values of the reference
  const bool hasRef = refHz_ > 0. && markers_.contains(refHz_);
  if(!hasRef)
    refHz_ = 0.;
  double refMag = 0., refPh = 0.;
  const bool refOk = hasRef && markerValues(refHz_, &refMag, &refPh);
  const int refIndex = hasRef ? markers_.indexOf(refHz_) : -1;
  markerTable_->setColumnHidden(3, !hasRef);
  markerTable_->setColumnHidden(4, !hasRef);

  for(int i = 0; i < markers_.size(); ++i) {
    const double hz = markers_.at(i);
    double mag = 0., ph = 0.;
    const bool ok = markerValues(hz, &mag, &ph);

    QTableWidgetItem* f = new QTableWidgetItem(units::format(
      QString::number(hz, 'g', 8), "Hz"));
    QTableWidgetItem* m = new QTableWidgetItem(ok ? magnitudeText(mag) : "-");
    QTableWidgetItem* p = new QTableWidgetItem(ok ? phaseText(ph) : "-");
    m->setFlags(m->flags() & ~Qt::ItemIsEditable);
    p->setFlags(p->flags() & ~Qt::ItemIsEditable);
    markerTable_->setItem(i, 0, f);
    markerTable_->setItem(i, 1, m);
    markerTable_->setItem(i, 2, p);

    const bool isRef = (i == refIndex);
    const bool showDelta = hasRef && !isRef && ok && refOk;
    QString dMag, dPh, dF;
    if(showDelta) {
      dMag = magnitudeDb_
        ? QStringLiteral("%1%2 dB").arg(mag - refMag >= 0 ? "+" : "")
            .arg(mag - refMag, 0, 'f', 2)
        : (mag - refMag >= 0 ? QStringLiteral("+") : QString())
            + units::format(QString::number(mag - refMag, 'g', 6), "");
      dPh = (ph - refPh >= 0 ? QStringLiteral("+") : QString())
        + phaseText(ph - refPh);
      dF = (hz - refHz_ >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
        + units::format(QString::number(std::fabs(hz - refHz_), 'g', 6), "Hz");
    }
    QTableWidgetItem* dm = new QTableWidgetItem(isRef ? tr("ref") : dMag);
    QTableWidgetItem* dp = new QTableWidgetItem(isRef ? tr("ref") : dPh);
    dm->setFlags(dm->flags() & ~Qt::ItemIsEditable);
    dp->setFlags(dp->flags() & ~Qt::ItemIsEditable);
    markerTable_->setItem(i, 3, dm);
    markerTable_->setItem(i, 4, dp);
    if(showDelta)
      f->setToolTip(QStringLiteral("Δf = ") + dF);
    if(isRef) {
      QFont bold = f->font();
      bold.setBold(true);
      for(QTableWidgetItem* it : { f, m, p, dm, dp })
        it->setFont(bold);
    }

    if(!hasFrequencyAxis())
      continue;

    // dashed vertical line with the values at the top
    QwtPlotMarker* line = new QwtPlotMarker;
    line->setLineStyle(QwtPlotMarker::VLine);
    QColor c = QApplication::palette().color(QPalette::Text);
    c.setAlpha(150);
    line->setLinePen(c, isRef ? 2 : 1, isRef ? Qt::DashDotLine : Qt::DashLine);
    line->setXValue(angular_ ? hz * twoPi : hz);
    QString text = QStringLiteral("M%1%2  %3").arg(i + 1)
      .arg(isRef ? tr(" (ref)") : QString())
      .arg(units::format(QString::number(hz, 'g', 6), "Hz"));
    if(ok)
      text += QLatin1Char('\n') + magnitudeText(mag)
        + QLatin1Char('\n') + phaseText(ph);
    if(showDelta)
      text += QStringLiteral("\nΔ ") + dF + QStringLiteral("\nΔ ") + dMag
        + QStringLiteral("\nΔ ") + dPh;
    QwtText label(text);
    label.setColor(QApplication::palette().color(QPalette::Text));
    QColor bg = QApplication::palette().color(QPalette::Base);
    bg.setAlpha(220);
    label.setBackgroundBrush(bg);
    label.setBorderRadius(3);
    label.setRenderFlags(Qt::AlignLeft | Qt::AlignTop);
    line->setLabel(label);
    const double px = xMap.transform(angular_ ? hz * twoPi : hz);
    const int labelWidth = label.textSize(plot_->canvas()->font()).width() + 16;
    int row = 0;
    while(row < 2 && px - lastPx[row] < labelWidth)
      ++row;
    lastPx[row] = px;
    line->setLabelAlignment(rows[row] | Qt::AlignRight);
    line->setZ(30);
    line->attach(plot_);
    markerLines_.append(line);
  }
  fillingTable_ = false;

  // the panel is as wide as the columns need
  int width = 2 * markerTable_->frameWidth() + 8;
  for(int c = 0; c < markerTable_->columnCount(); ++c)
    if(!markerTable_->isColumnHidden(c))
    {
      int w = markerTable_->horizontalHeader()->sectionSizeHint(c);
      const QFontMetrics fm(markerTable_->font());
      for(int r = 0; r < markerTable_->rowCount(); ++r)
        if(QTableWidgetItem* it = markerTable_->item(r, c))
          w = qMax(w, fm.horizontalAdvance(it->text()) + 12);
      width += w + 4;
    }
  if(markerTable_->verticalScrollBar()->isVisible())
    width += markerTable_->verticalScrollBar()->width();
  markerPanel_->setMinimumWidth(qBound(260, width + 8, 560));

  plot_->replot();
}


void WorkPlane::setMarker(int row, double hz)
{
  // the reference follows its marker
  if(markers_.at(row) == refHz_)
    refHz_ = hz;
  markers_[row] = hz;
  std::sort(markers_.begin(), markers_.end());
}


void WorkPlane::removeMarker(int row)
{
  if(markers_.at(row) == refHz_)
    refHz_ = 0.;
  markers_.removeAt(row);
}


double WorkPlane::magnitudeAt(double hz) const
{
  // |H(j 2 pi f)|, linear
  if(num_.empty() || !(hz > 0))
    return NAN;
  const std::pair<double, double> at(hz, hz * 1.000001);
  std::pair< std::vector<double>, std::vector<double> > m =
    sapecng::magnitude(at, 1., true)(num_, den_, values_);
  return m.second.empty() ? NAN : m.second.front();
}


QVector<QPointF> WorkPlane::magnitudeGrid() const
{
  // log(f), |H| over the plotted range, 400 points per decade
  QVector<QPointF> grid;
  const double a = std::log10(fStart_), b = std::log10(fStop_);
  const int n = qBound(50, int((b - a) * 400), 20000);
  grid.reserve(n + 1);
  for(int i = 0; i <= n; ++i) {
    const double lf = a + (b - a) * i / n;
    grid.append(QPointF(lf, magnitudeAt(std::pow(10., lf))));
  }
  return grid;
}


void WorkPlane::markerToPeak(int direction)
{
  const int row = selectedMarker();
  if(row < 0) {
    markerNote_->setText(tr("Select a marker first."));
    return;
  }
  const double f0 = markers_.at(row);
  const QVector<QPointF> g = magnitudeGrid();
  const double l0 = std::log10(f0);

  int found = -1;
  if(direction > 0) {
    for(int i = 1; i + 1 < g.size(); ++i)
      if(g[i].x() > l0 + 1e-6 && g[i].y() > g[i - 1].y() && g[i].y() >= g[i + 1].y()) {
        found = i;
        break;
      }
  } else {
    for(int i = g.size() - 2; i >= 1; --i)
      if(g[i].x() < l0 - 1e-6 && g[i].y() > g[i + 1].y() && g[i].y() >= g[i - 1].y()) {
        found = i;
        break;
      }
  }
  if(found < 0) {
    markerNote_->setText(direction > 0
      ? tr("No peak to the right in the plotted range.")
      : tr("No peak to the left in the plotted range."));
    return;
  }

  // refine between the neighbours (golden section on log f)
  double lo = g[found - 1].x(), hi = g[found + 1].x();
  const double phi = (std::sqrt(5.) - 1) / 2;
  for(int k = 0; k < 60; ++k) {
    const double c = hi - phi * (hi - lo), d = lo + phi * (hi - lo);
    if(magnitudeAt(std::pow(10., c)) > magnitudeAt(std::pow(10., d)))
      hi = d;
    else
      lo = c;
  }
  const double hz = QString::number(std::pow(10., (lo + hi) / 2), 'g', 6).toDouble();
  setMarker(row, hz);
  markerNote_->clear();
  refreshMarkers();
  markerTable_->selectRow(markers_.indexOf(hz));
}


void WorkPlane::markerMinus3dB()
{
  const int row = selectedMarker();
  if(row < 0) {
    markerNote_->setText(tr("Select a marker first."));
    return;
  }
  const double f0 = markers_.at(row);
  const double m0 = magnitudeAt(f0);
  if(!(m0 > 0)) {
    markerNote_->setText(tr("No magnitude at the selected marker."));
    return;
  }
  // 20 log10(m / m0) = -3.0103 dB  <=>  m = m0 / sqrt(2)
  const double target = m0 / std::sqrt(2.);
  const QVector<QPointF> g = magnitudeGrid();
  const double l0 = std::log10(f0);

  // nearest crossing of the target on each side
  auto crossing = [&](int dir, double* lf) {
      int start = 0;
      while(start < g.size() && g[start].x() < l0) ++start;
      if(dir < 0) --start;
      double prevL = l0, prevM = m0;
      for(int i = start; i >= 0 && i < g.size(); i += dir) {
        const double m = g[i].y();
        if(std::isfinite(m) && (prevM - target) * (m - target) <= 0 && prevM != m) {
          // bisection between prevL and g[i].x()
          double a = prevL, b = g[i].x();
          for(int k = 0; k < 60; ++k) {
            const double c = (a + b) / 2;
            if((magnitudeAt(std::pow(10., a)) - target)
                * (magnitudeAt(std::pow(10., c)) - target) <= 0)
              b = c;
            else
              a = c;
          }
          *lf = (a + b) / 2;
          return true;
        }
        prevL = g[i].x();
        prevM = m;
      }
      return false;
    };
  double up = 0., down = 0.;
  const bool hasUp = crossing(1, &up), hasDown = crossing(-1, &down);
  if(!hasUp && !hasDown) {
    markerNote_->setText(tr("The magnitude does not fall 3 dB below the "
                            "selected marker in the plotted range."));
    return;
  }
  const double lf = (hasUp && (!hasDown || up - l0 <= l0 - down)) ? up : down;
  const double hz = QString::number(std::pow(10., lf), 'g', 6).toDouble();
  refHz_ = f0;
  markerNote_->clear();
  addMarker(hz);
}


bool WorkPlane::antialiased()
{
  return QSettings().value(QStringLiteral("Plot/antialiased"), true).toBool();
}


void WorkPlane::setAntialiasedDefault(bool on)
{
  QSettings().setValue(QStringLiteral("Plot/antialiased"), on);
}


void WorkPlane::applyAntialiasing()
{
  // the curves only: the grid and the marker lines are straight lines,
  // which antialiasing only blurs (a line between two pixel rows is
  // smeared over both)
  const bool on = antialiased();
  foreach(QwtPlotItem* item, plot_->itemList(QwtPlotItem::Rtti_PlotCurve))
    item->setRenderHint(QwtPlotItem::RenderAntialiased, on);
  plot_->replot();
}


void WorkPlane::addMarker(double hz)
{
  if(!(hz > 0) || !std::isfinite(hz))
    return;
  // the panel (closed at first) opens with the first marker
  if(markers_.isEmpty())
    emit markersPanelChanged(true);
  markers_.append(hz);
  std::sort(markers_.begin(), markers_.end());
  refreshMarkers();
  markerTable_->selectRow(markers_.indexOf(hz));
}


int WorkPlane::selectedMarker() const
{
  const QList<QTableWidgetSelectionRange> r = markerTable_->selectedRanges();
  return r.isEmpty() ? -1 : r.first().topRow();
}


void WorkPlane::moveSelectedMarker(int direction)
{
  const int row = selectedMarker();
  double step = 0.;
  if(row < 0 || !units::parse(stepEdit_->text(), "Hz", &step) || !(step > 0))
    return;
  const double hz = markers_.at(row) + direction * step;
  if(!(hz > 0))
    return;
  setMarker(row, hz);
  refreshMarkers();
  markerTable_->selectRow(markers_.indexOf(hz));
}


void WorkPlane::markerEdited(int row, int column)
{
  if(fillingTable_ || column != 0 || row < 0 || row >= markers_.size())
    return;
  double hz = 0.;
  if(units::parse(markerTable_->item(row, 0)->text(), "Hz", &hz) && hz > 0)
    setMarker(row, hz);
  // re-show the list (also when the text was not a frequency)
  QTimer::singleShot(0, this, [this]() { refreshMarkers(); });
}


void WorkPlane::plotClicked(const QPointF& pos)
{
  if(lastId_ == NOOP || lastId_ == ZEROS || lastId_ == POLES)
    return;
  const double twoPi = 8. * std::atan(1.);
  const double hz = angular_ ? pos.x() / twoPi : pos.x();

  // a click on a marker line removes it
  const QwtScaleMap map = plot_->canvasMap(QwtPlot::xBottom);
  const double px = map.transform(pos.x());
  for(int i = 0; i < markers_.size(); ++i) {
    const double x = angular_ ? markers_.at(i) * twoPi : markers_.at(i);
    if(qAbs(map.transform(x) - px) < 5.) {
      removeMarker(i);
      refreshMarkers();
      return;
    }
  }
  // a clicked position: 3 significant digits (1.23 kHz) is plenty
  addMarker(QString::number(hz, 'g', 3).toDouble());
}


bool WorkPlane::hasFrequencyAxis() const
{
  return lastId_ != NOOP && lastId_ != ZEROS && lastId_ != POLES;
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
  points->setRange(10, 10000);
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


namespace {

// axes: 100, 1k, 10k, 1M instead of 100,000 and 1e+06
class SiScaleDraw: public QwtScaleDraw
{
public:
  QwtText label(double v) const override
  {
    // prefixes from 1000 up; below that the plain numbers read better
    if(std::fabs(v) < 999.5)
      return QwtScaleDraw::label(v);
    return QwtText(units::format(QString::number(v, 'g', 6), QString()));
  }
};

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

  connect(plot_->axisWidget(QwtPlot::xBottom), &QwtScaleWidget::scaleDivChanged,
    this, [this]() { if(!markers_.isEmpty()) QTimer::singleShot(0, this, [this]() { refreshMarkers(); }); });

  plot_->setAxisScaleDraw(QwtPlot::xBottom, new SiScaleDraw);
  plot_->setAxisScaleDraw(QwtPlot::yLeft, new SiScaleDraw);

  // click on the plot: a marker at that frequency (or remove one there)
  connect(tracker_, SIGNAL(selected(const QPointF&)),
    this, SLOT(plotClicked(const QPointF&)));

  // markers panel
  markerTable_ = new QTableWidget(0, 5);
  markerTable_->setHorizontalHeaderLabels(
    { tr("Frequency"), tr("Magnitude"), tr("Phase"),
      QStringLiteral("Δ ") + tr("Mag."), QStringLiteral("Δ ") + tr("Phase") });
  // the visible columns share the width (the last one may be hidden)
  markerTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  markerTable_->verticalHeader()->setVisible(false);
  markerTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  markerTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  markerTable_->setToolTip(tr("Click a curve to add a marker; edit a "
    "frequency here (10k, 2.5MEG)"));
  connect(markerTable_, &QTableWidget::cellChanged,
    this, &WorkPlane::markerEdited);

  QPushButton* add = new QPushButton(tr("Add"));
  add->setToolTip(tr("A marker in the middle of the visible range"));
  connect(add, &QPushButton::clicked, this, [this]() {
      const QwtScaleMap map = plot_->canvasMap(QwtPlot::xBottom);
      double x = map.invTransform((map.p1() + map.p2()) / 2);
      addMarker(angular_ ? x / (8. * std::atan(1.)) : x);
    });
  QPushButton* remove = new QPushButton(tr("Remove"));
  connect(remove, &QPushButton::clicked, this, [this]() {
      const int row = selectedMarker();
      if(row >= 0) {
        removeMarker(row);
        refreshMarkers();
      }
    });
  QPushButton* left = new QPushButton(QStringLiteral("◀"));
  left->setToolTip(tr("Move the selected marker down by the step"));
  connect(left, &QPushButton::clicked, this, [this]() { moveSelectedMarker(-1); });
  QPushButton* right = new QPushButton(QStringLiteral("▶"));
  right->setToolTip(tr("Move the selected marker up by the step"));
  connect(right, &QPushButton::clicked, this, [this]() { moveSelectedMarker(1); });
  for(QPushButton* b : { left, right })
    b->setMaximumWidth(32);
  stepEdit_ = new QLineEdit(QStringLiteral("100"));
  stepEdit_->setValidator(new units::ValueValidator(QStringLiteral("Hz"), stepEdit_));
  stepEdit_->setToolTip(tr("Step for moving a marker, in Hz (10, 1k, 1MEG)"));
  stepEdit_->setMaximumWidth(70);

  QPushButton* reference = new QPushButton(tr("Reference"));
  reference->setToolTip(tr("Differential markers: the selected marker becomes "
    "the reference and the others show their difference from it in "
    "magnitude and phase (and frequency). Again on the reference: off"));
  connect(reference, &QPushButton::clicked, this, [this]() {
      const int row = selectedMarker();
      if(row < 0)
        return;
      refHz_ = (markers_.at(row) == refHz_) ? 0. : markers_.at(row);
      refreshMarkers();
      markerTable_->selectRow(row);
    });

  QHBoxLayout* buttons = new QHBoxLayout;
  buttons->addWidget(add);
  buttons->addWidget(remove);
  buttons->addWidget(reference);
  QHBoxLayout* move = new QHBoxLayout;
  move->addWidget(left);
  move->addWidget(right);
  move->addWidget(new QLabel(tr("Step:")));
  move->addWidget(stepEdit_);
  move->addWidget(new QLabel(tr("Hz")));
  move->addStretch();

  // searches: peaks of the magnitude, the -3 dB point
  QPushButton* prevPeak = new QPushButton(tr("◀ Peak"));
  prevPeak->setToolTip(tr("Move the selected marker to the previous peak of "
                          "the magnitude"));
  connect(prevPeak, &QPushButton::clicked, this, [this]() { markerToPeak(-1); });
  QPushButton* nextPeak = new QPushButton(tr("Peak ▶"));
  nextPeak->setToolTip(tr("Move the selected marker to the next peak of the "
                          "magnitude"));
  connect(nextPeak, &QPushButton::clicked, this, [this]() { markerToPeak(1); });
  QPushButton* minus3 = new QPushButton(tr("−3 dB"));
  minus3->setToolTip(tr("A marker where the magnitude is 3 dB below the "
    "selected marker (the nearest, on either side); the selected marker "
    "becomes the reference, so the difference columns show the bandwidth"));
  connect(minus3, &QPushButton::clicked, this, &WorkPlane::markerMinus3dB);
  QHBoxLayout* search = new QHBoxLayout;
  search->addWidget(prevPeak);
  search->addWidget(nextPeak);
  search->addWidget(minus3);
  markerNote_ = new QLabel;
  markerNote_->setWordWrap(true);
  QFont small = markerNote_->font();
  small.setPointSizeF(small.pointSizeF() * 0.9);
  markerNote_->setFont(small);

  QVBoxLayout* panelLayout = new QVBoxLayout;
  panelLayout->setContentsMargins(4, 4, 4, 4);
  panelLayout->addWidget(markerTable_, 1);
  panelLayout->addLayout(buttons);
  panelLayout->addLayout(search);
  panelLayout->addLayout(move);
  panelLayout->addWidget(markerNote_);
  // shown by the plot window in its Markers dock (QSapecNGWindow)
  markerPanel_ = new QWidget;
  markerPanel_->setLayout(panelLayout);
  markerPanel_->setMinimumWidth(260);
  markerPanel_->setMaximumWidth(560);
  markerPanel_->hide();

  centralLayout_ = new QVBoxLayout;
  centralLayout_->addWidget(plot_);

  // right-click on the plot: a marker at that frequency
  plot_->extraActions = [this](const QPoint& canvasPos) {
      QList<QAction*> actions;
      if(!hasFrequencyAxis() || !plot_->canvas()->rect().contains(canvasPos))
        return actions;
      const double x = plot_->invTransform(QwtPlot::xBottom, canvasPos.x());
      const double twoPi = 8. * std::atan(1.);
      const double hz = QString::number(angular_ ? x / twoPi : x, 'g', 3)
        .toDouble();
      if(!(hz > 0))
        return actions;
      QAction* add = new QAction(tr("Add &Marker at %1")
        .arg(units::format(QString::number(hz, 'g', 3), QStringLiteral("Hz"))), nullptr);
      connect(add, &QAction::triggered, this, [this, hz]() { addMarker(hz); });
      actions << add;
      return actions;
    };

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
  markers_.clear();
  refHz_ = 0.;
  refreshMarkers();
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
