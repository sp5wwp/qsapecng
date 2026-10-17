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


#ifndef COMPONENT_H
#define COMPONENT_H


#include "gui/editor/item.h"
#include "gui/editor/vpiface.h"
#include "gui/editor/propertytextitem.hpp"

#include <QList>
#include <QPointF>
#include <QVariant>
#include <QPainterPath>


class QPainter;
class QStyleOptionGraphicsItem;

class QFocusEvent;
class QGraphicsSceneMouseEvent;

class QGraphicsTextItem;
class PropertyTextItem;


namespace qsapecng
{


class GraphicsNode;
class SchematicScene;


class Component: public Item, public ValuePropagationInterface
{

public:
  Component(
      QGraphicsItem* parent = 0,
      SchematicScene* scene = 0
    );
  Component(
      QPainterPath path,
      QGraphicsItem* parent = 0,
      SchematicScene* scene = 0
    );
  Component(
      QPainterPath path,
      QList<QPointF> nodes,
      QGraphicsItem* parent = 0,
      SchematicScene* scene = 0
    );
  Component(
      QPainterPath path,
      QPointF node,
      QGraphicsItem* parent = 0,
      SchematicScene* scene = 0
    );
  ~Component();

  QRectF boundingRect() const;
  void paint(
    QPainter* painter,
    const QStyleOptionGraphicsItem* option,
    QWidget* widget
  );

  virtual void mirror();
  virtual void rotate();

  inline PropertyTextItem* label() const { return label_; }

  // The label is anchored at its centre: by default beside the body,
  // perpendicular to the terminals; a dragged label keeps its anchor.
  // Coordinates are the component's own (they rotate with it).
  void updateLabelPosition();
  inline bool hasCustomLabelPosition() const { return labelMoved_; }
  inline QPointF labelAnchor() const { return labelAnchor_; }
  void setLabelAnchor(const QPointF& anchor);

  void addNodes(QList<QPointF> nodes);
  void addNodes(QPointF node);
  QVector<int> nodes() const;

  // tag symbols (the Out marker): filled shape with a caption
  void setTag(const QString& text, const QRectF& pivot);
  QRectF pivotRect() const override;

  inline void setPath(const QPainterPath& path)
    { prepareGeometryChange(); path_ = path; update(); updateLabelPosition(); }
  inline QPainterPath path() const { return path_; }

public:
  int propagate(int value);
  void invalidate();

protected:
  void focusInEvent(QFocusEvent* event);
  void focusOutEvent(QFocusEvent* event);
  QVariant itemChange(GraphicsItemChange change, const QVariant& value);

private:
  void createNode(const QPointF& point);
  void setupLabel();
  QPointF defaultLabelAnchor(const QSizeF& textSize,
      const QTransform& orientation) const;

private:
  QList<GraphicsNode*> nodeList_;
  QPainterPath path_;

  PropertyTextItem* label_;
  QString tag_;
  QRectF pivot_;
  QPointF labelAnchor_;
  bool labelMoved_ = false;
  bool positioningLabel_ = false;

};


}


#endif // COMPONENT_H
