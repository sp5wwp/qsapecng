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


#ifndef SCHEMATICVIEW_H
#define SCHEMATICVIEW_H


#include "gui/theme.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QGraphicsView>
#include <QResizeEvent>


namespace qsapecng
{


class SchematicScene;


class SchematicView: public QGraphicsView
{

  Q_OBJECT

public:
  SchematicView(QWidget* parent = 0): QGraphicsView(parent)
  {
    setCacheMode(QGraphicsView::CacheBackground);
    // Repaint the whole viewport on changes: antialiased (subpixel) text
    // can draw slightly outside an item's bounding rectangle, and minimal
    // updates then leave trails when items are dragged.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::RubberBandDrag);
//     setDragMode(QGraphicsView::ScrollHandDrag);
//     setAlignment(Qt::AlignCenter);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setAcceptDrops(true);
    setMouseTracking(true);
    setAttribute(Qt::WA_DeleteOnClose);
    adjustSize();
  }

protected:
  // follow light/dark theme changes: recolour text on the sheet and redraw
  // the cached background (sheet colour and grid)
  void changeEvent(QEvent* event) override
  {
    QGraphicsView::changeEvent(event);
    if(event->type() == QEvent::PaletteChange) {
      theme::refreshSheetText(scene());
      resetCachedContent();
      viewport()->update();
    }
  }

public slots:
  void zoomIn()
    { scale(1.2, 1.2); }

  void zoomOut()
    { scale(1/1.2, 1/1.2); }

  void normalSize()
    { resetTransform(); }

  void fitToView()
  {
    fitInView(sceneRect(), Qt::KeepAspectRatio);
    ensureVisible(sceneRect());
    centerOn(sceneRect().center());
  }

protected:
  // CAD-style box selection: dragging to the right selects what lies
  // entirely inside the box (blue), dragging to the left also what it
  // touches (green)
  void mousePressEvent(QMouseEvent* event) override
  {
    boxStart_ = event->position().toPoint();
    boxDirection_ = 0;
    QGraphicsView::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override
  {
    if(event->buttons() & Qt::LeftButton) {
      const int dir = event->position().x() >= boxStart_.x() ? 1 : -1;
      if(dir != boxDirection_) {
        boxDirection_ = dir;
        setRubberBandSelectionMode(dir > 0
          ? Qt::ContainsItemShape : Qt::IntersectsItemShape);
        QPalette pal = viewport()->palette();
        pal.setColor(QPalette::Highlight, dir > 0
          ? QColor(0x25, 0x63, 0xeb) : QColor(0x16, 0xa3, 0x4a));
        viewport()->setPalette(pal);
      }
    }
    QGraphicsView::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override
  {
    QGraphicsView::mouseReleaseEvent(event);
    if(boxDirection_ != 0) {
      boxDirection_ = 0;
      viewport()->setPalette(QPalette());   // follow the theme again
    }
  }

private:
  QPoint boxStart_;
  int boxDirection_ = 0;

protected:
//   void resizeEvent(QResizeEvent* event)
//   {
//     setSceneRect(0, 0, event->size().width(), event->size().height());
//     QGraphicsView::resizeEvent(event);
//   }

};


}


#endif // SCHEMATICVIEW_H
