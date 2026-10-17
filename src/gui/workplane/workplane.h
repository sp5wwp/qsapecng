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


#ifndef WORKPLANE_H
#define WORKPLANE_H


#include "model/metacircuit.h"

#include <QWidget>
#include <QMenu>
#include <QContextMenuEvent>
#include <QPrinter>

#include <QObject>
#include <QVector>
#include <QPair>

#include <qwt_plot.h>
#include <qwt_plot_curve.h>

#include <string>
#include <map>


class QVBoxLayout;
class QHBoxLayout;
class QDoubleSpinBox;
class QTableWidget;
class QLineEdit;
class QLabel;

class QwtPlotGrid;
class QwtPickerMachine;
class QwtPlotPicker;
class QwtPlotMarker;
class QwtSymbol;


namespace qsapecng
{


class QwtPlot_ContextMenu: public QwtPlot
{

public:
  QwtPlot_ContextMenu(QWidget* parent = 0): QwtPlot(parent), contextMenu_(0) { }
  QwtPlot_ContextMenu(const QwtText& title, QWidget* parent = 0)
    : QwtPlot(title, parent), contextMenu_(0) { }

  inline void setContextMenu(QMenu* menu) { contextMenu_ = menu; }
  inline QMenu* contextMenu() const { return contextMenu_; }

  void contextMenuEvent(QContextMenuEvent* event)
    { if(contextMenu_) contextMenu_->exec(event->globalPos()); }

private:
  QMenu* contextMenu_;

};



typedef
  std::pair< std::vector<double>, std::vector<double> >
  (*functor)
    (
      const sapecng::metacircuit::expression& numerator,
      const sapecng::metacircuit::expression& denominator,
      std::map< std::string, double > values
    )
  ;

class MarkableCurve: public QObject, public QwtPlotCurve
{

  Q_OBJECT

public:
  MarkableCurve(): QwtPlotCurve() { initialize(); }
  ~MarkableCurve();

  void setVisible(bool on);

  // units for the marker labels ("Hz", "rad/s"; "dB", "rad")
  void setUnits(const QString& x, const QString& y)
    { xUnit_ = x; yUnit_ = y; }

  // placed markers: a click on the plot pins one at the nearest point of
  // the curve, a click on a pinned marker removes it

public slots:
  void selected(const QPointF& pos);
  void appended(const QPointF& pos);
  void moved(const QPointF& pos);

private:
  void initialize();
  int nearestSample(const QPointF& pos) const;
  QString labelFor(int idx) const;
  void placeLabel(QwtPlotMarker* marker, int idx) const;

private:
  QwtPlotMarker* marker_;
  QwtSymbol* symbol_;
  QString xUnit_;
  QString yUnit_;

};



class MarkedCurve: public QwtPlotCurve
{

public:
  MarkedCurve(): QwtPlotCurve() { }
  ~MarkedCurve();

  void setVisible(bool on);
  void replotMarker();

private:
  void resetMarker();

private:
  QVector<QwtPlotMarker*> markers_;

};



// Everything a plot is set up with (Run > New Plot, Plot > Settings).
struct PlotSettings
{
  int kind = 3;                 // WorkPlane::Kind, Bode by default
  bool magnitudeDb = true;
  bool phaseDegrees = true;
  bool angular = false;         // frequency in rad/s
  bool xLog = true;
  bool yLog = false;            // linear magnitude only
  bool autoRange = true;        // a decade beyond the poles and zeros
  double fStart = 1.;           // Hz
  double fStop = 1e5;           // Hz
  int pointsPerDecade = 100;

  // last used, as defaults for the next plot
  static PlotSettings load();
  void save() const;
};


class WorkPlane: public QWidget
{

  Q_OBJECT

public:
  enum Kind { MagnitudeKind = 0, PhaseKind = 1, PoleZeroKind = 2,
              BodeKind = 3 };

  enum F
  {
    MAGNITUDE,
    MAGNITUDE_RAD,
    PHASE,
    PHASE_RAD,
    GAIN,
    GAIN_RAD,
    LOSS,
    LOSS_RAD,
    ZEROS,
    POLES,
    NOOP
  };

public:
  WorkPlane(QWidget* parent = 0);
  ~WorkPlane();

  void setData(const std::map<std::string, double>& values,
      const sapecng::metacircuit::expression& numerator,
      const sapecng::metacircuit::expression& denominator
    );

  inline void setContextMenu(QMenu* menu) { plot_->setContextMenu(menu); }
  PlotSettings settings() const;
  inline int kind() const { return kind_; }
  inline bool hasPlot() const { return lastId_ != NOOP; }
  inline QMenu* contextMenu() const { return plot_->contextMenu(); }

  const QwtPlot& const_plot() const { return *plot_; }

signals:
  void markersPanelChanged(bool visible);

public slots:
  void setDirty();
  void xAxisLogScale(bool log = true);
  void yAxisLogScale(bool log = false);
  void plot(WorkPlane::F f);
  void plot(int f);
  void redraw();
  void clearMarkers();

  // what is plotted (WorkPlane::Kind), independent of its units
  void showKind(int kind);
  // all settings at once; replots when there is data
  void applySettings(const PlotSettings& settings);
  void setMagnitudeDb(bool db);
  void setPhaseDegrees(bool degrees);
  void setAngularFrequency(bool radPerSecond);
  // WorkPlane > Frequency Range...
  void editFrequencyRange();
  // WorkPlane > Vertical Range...
  void editVerticalRange();
  // WorkPlane > Reset View: undo zooming and panning
  void resetView();

public:
  // smooth curves and lines (Plot > Antialiasing, kept between sessions)
  static bool antialiased();
  static void setAntialiasedDefault(bool on);
  void applyAntialiasing();
  void equalizeAxes();
  // marker searches (Markers panel)
  double magnitudeAt(double hz) const;
  QVector<QPointF> magnitudeGrid() const;
  void markerToPeak(int direction);
  void markerMinus3dB();
  // markers (frequencies, Hz) and their panel
  void addMarker(double hz);
  void setMarkersPanelVisible(bool visible);
  bool markersPanelVisible() const { return markersPanelWanted_; }
  void print(QPrinter& printer);

private:
  std::map<std::string, double> actValues() const;
  void createMainLayout();
  void setupCurves();
  void setupCurve(
      std::pair< std::vector<double>, std::vector<double> > data,
      WorkPlane::F f
    );

private:
  sapecng::metacircuit::expression num_;
  sapecng::metacircuit::expression den_;

  QwtPlotGrid* grid_ = nullptr;
  QwtPlot_ContextMenu* plot_ = nullptr;
  QVector< QwtPlotCurve* > attached_;
  QVector< QPair< QwtPlotCurve*, bool > > curves_;
  QwtPickerMachine *clickPointMachine_;
  QwtPlotPicker* tracker_;
  F lastId_;
  bool xLog_ = true;   // user's x-axis log-scale choice
  bool yLog_ = false;  // user's y-axis log-scale choice (linear magnitude)
  int kind_ = MagnitudeKind;
  bool magnitudeDb_ = true;
  bool phaseDegrees_ = true;
  bool angular_ = false;   // frequency axis in rad/s instead of Hz

  F currentF() const;
  void relabel(F f);

  void applyAxisScales(bool forceLinear);

protected:
  void changeEvent(QEvent* event) override;

  // component values (from the schematic) and the frequency sweep
  std::map<std::string, double> values_;
  double fStart_ = 1.;          // Hz
  double fStop_ = 1e5;          // Hz
  int pointsPerDecade_ = 100;
  bool autoRange_ = true;       // a decade beyond the poles and zeros
  bool keepAttached_ = false;   // drawing the second curve of a Bode plot

  void autoFrequencyRange();
  void invalidateCurves();
  void plotBode();

  // vertical range per plotted quantity ("magDb", "mag", "phaseDeg",
  // "phaseRad"); automatic unless set in the dialog
  struct YRange { bool automatic = true; double min = 0.; double max = 1.; };
  YRange yRange(const QString& key) const;
  void setYRange(const QString& key, const YRange& range);
  QString magnitudeKey() const;
  QString phaseKey() const;
  void applyYRange(int axis, const QString& key);

  // markers: vertical lines with magnitude and phase, listed in a panel
  QList<double> markers_;                 // Hz
  QList<QwtPlotMarker*> markerLines_;
  QWidget* markerPanel_ = nullptr;
  QTableWidget* markerTable_ = nullptr;
  QLineEdit* stepEdit_ = nullptr;
  bool fillingTable_ = false;
  bool markersPanelWanted_ = false;   // opens with the first marker
  QLabel* markerNote_ = nullptr;      // what a marker search found (or not)
  double refHz_ = 0.;                     // reference marker (0: none)
  void setMarker(int row, double hz);
  void removeMarker(int row);
  void refreshMarkers();
  bool markerValues(double hz, double* magnitude, double* phase) const;
  QString magnitudeText(double v) const;
  QString phaseText(double v) const;
  void moveSelectedMarker(int direction);
  int selectedMarker() const;

private slots:
  void plotClicked(const QPointF& pos);
  void markerEdited(int row, int column);

private:
  // mouse wheel zoom around the pointer
  bool eventFilter(QObject* watched, QEvent* event) override;
  void zoomAt(const QPoint& pos, double factor, bool x, bool y);

  QVBoxLayout* centralLayout_;
  QHBoxLayout* mainLayout_;

};


}


#endif // WORKPLANE_H
