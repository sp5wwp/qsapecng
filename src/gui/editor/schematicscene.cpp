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
#include "gui/editor/componentdialog.h"
#include "parser/parser_factory.h"

#include "gui/editor/schematicscene.h"
#include <QGraphicsLineItem>
#include "gui/editor/schematicsceneparser.h"
#include "gui/editor/undoredocommand.h"
#include "gui/editor/component.h"
#include "gui/editor/propertytextitem.hpp"
#include "gui/editor/label.h"
#include "gui/editor/wire.h"
#include "gui/editor/graphicsnode.h"
#include "gui/editor/item.h"

#include "gui/settings.h"
#include "gui/qlogger.h"

#include "gui/qtsolutions/qtpropertyeditor/QtBoolPropertyManager"
#include "gui/qtsolutions/qtpropertyeditor/QtGroupPropertyManager"
#include "gui/qtsolutions/qtpropertyeditor/QtStringPropertyManager"


#include "gui/qtsolutions/qtpropertyeditor/QtAbstractPropertyBrowser"
#include "gui/qtsolutions/qtpropertyeditor/QtProperty"

#include <QEvent>
#include <QRegularExpression>
#include <QMimeData>
#include <QTimer>
#include <QCryptographicHash>
#include <QPointer>
#include <QSet>

#include <QKeyEvent>
#include <QMenu>
#include <QPainter>

#include <cmath>
#include <QFileDialog>
#include <QUndoCommand>
#include <QGraphicsView>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QApplication>
#include <QClipboard>

#include <sstream>
#include <memory>


namespace qsapecng
{


QPainterPath SchematicScene::nilPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(0, 0);
  path.lineTo(0, step);
  path.lineTo(step, step);
  path.lineTo(step, 0);
  path.lineTo(0, 0);

  return path;
}


QPainterPath SchematicScene::groundPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(step, 0);
  path.lineTo(step, step);
  path.moveTo(step - step / 2, step);
  path.lineTo(step + step / 2, step);
  path.moveTo(step - step / 4, step + step / 3);
  path.lineTo(step + step / 4, step + step / 3);
  path.moveTo(step - step / 6, step + step * 2/3);
  path.lineTo(step + step / 6, step + step * 2/3);
  path.addRect(step, 2 * step, 1E-99, 1E-99);

  return path;
}


QPainterPath SchematicScene::portPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.addRect(0, 0, 1E-99, 1E-99);
  path.addRect(2 * step, 2 * step, 1E-99, 1E-99);
  path.moveTo(step / 3, step);
  path.arcTo(
      QRectF(step / 3, step / 3,
        2 * step - step * 2 / 3,
        2 * step - step * 2 / 3),
      180, 360
    );
  path.moveTo(step / 2, step);
  path.arcTo(
      QRectF(step / 2, step / 2, step, step),
      180, 360
    );

  return path;
}


QPainterPath SchematicScene::outPath()
{
  // tag pointing at its terminal (0, step): <| Out |
  QPainterPath path;
  const qreal step = GridStep;

  // 1.44 steps high, 3.4 long, centred on the terminal
  const qreal top = 0.28 * step, bottom = 1.72 * step;
  path.moveTo(0, step);
  path.lineTo(0.7 * step, top);
  path.lineTo(3.4 * step, top);
  path.lineTo(3.4 * step, bottom);
  path.lineTo(0.7 * step, bottom);
  path.closeSubpath();

  return path;
}


QPainterPath SchematicScene::singleVoltageSourcePath()
{
  // lead from the pin (bottom) ending in a small open circle (as in the
  // Falstad simulator's rail symbol)
  QPainterPath path;
  const qreal step = GridStep;

  // 2 steps from the pin to the top of the circle
  path.moveTo(step, 2 * step);
  path.lineTo(step, 0.6 * step);
  path.addEllipse(QPointF(step, 0.3 * step), 0.3 * step, 0.3 * step);
  // invisible marks: a 2 x 2 step box, so rotation keeps the pin on grid
  path.addRect(0, 0, 1E-99, 1E-99);
  path.addRect(2 * step, 2 * step, 1E-99, 1E-99);

  return path;
}


QRectF SchematicScene::outPivotRect()
{
  // the box of the old round Out marker: rotation turns around it, so
  // rotated markers in older files stay on their wires
  const qreal step = GridStep;
  return QRectF(0, step / 2, 2 * step, step);
}


QPainterPath SchematicScene::voltmeterPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(step, 0);
  path.lineTo(step, step + step / 2);
  path.moveTo(step, 3 * step + step / 2);
  path.arcTo(
    QRectF(
      QPointF(0, step + step / 2),
      QPointF(2 * step, 3 * step + step / 2)
    ), -90, 360
  );
  path.lineTo(step, 5 * step);

  path.moveTo(step - step / 2, 2 * step);
  path.lineTo(step, 3 * step);
  path.lineTo(step + step / 2, 2 * step);

  path.moveTo(step + step / 2, step / 2 - step / 4);
  path.lineTo(step + step / 2, step / 2 + step / 4);

  path.moveTo(step + step / 2 - step / 4, step / 2);
  path.lineTo(step + step / 2 + step / 4, step / 2);

  path.moveTo(step + step / 2 - step / 4, 4 * step + step / 2);
  path.lineTo(step + step / 2 + step / 4, 4 * step + step / 2);

  return path;
}


QPainterPath SchematicScene::ammeterPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(step, 0);
  path.lineTo(step, step + step / 2);
  path.moveTo(step, 3 * step + step / 2);
  path.arcTo(
    QRectF(
      QPointF(0, step + step / 2),
      QPointF(2 * step, 3 * step + step / 2)
    ), -90, 360
  );
  path.lineTo(step, 5 * step);

  path.moveTo(step + step / 2, 3 * step);
  path.lineTo(step, 2 * step);
  path.lineTo(step - step / 2, 3 * step);

  path.moveTo(step + step / 4, 2 * step + step / 2);
  path.lineTo(step - step / 4, 2 * step + step / 2);

  path.moveTo(step / 2, 5 * step / 4);
  path.lineTo(step / 2, step / 4);
  path.moveTo(step / 2 - step / 4, 2 * step / 4);
  path.lineTo(step / 2, step / 4);
  path.moveTo(step / 2 + step / 4, 2 * step / 4);
  path.lineTo(step / 2, step / 4);

  return path;
}


QPainterPath SchematicScene::capacitorPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(0, step);
  path.lineTo(2 * step, step);
  path.moveTo(2 * step, 0);
  path.lineTo(2 * step, 2 * step);
  path.moveTo(3 * step, 0);
  path.lineTo(3 * step, 2 * step);
  path.moveTo(3 * step, step);
  path.lineTo(5 * step, step);

  return path;
}


QPainterPath SchematicScene::cccsPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(0, 0);
  path.lineTo(2 * step, 0);
  path.lineTo(2 * step, step + step / 2);

  path.lineTo(step, 2 * step + step / 2);
  path.lineTo(2 * step, 3 * step + step / 2);
  path.lineTo(3 * step, 2 * step + step / 2);
  path.lineTo(2 * step, step + step / 2);

  path.moveTo(2 * step, 3 * step + step / 2);
  path.lineTo(2 * step, 5 * step);
  path.lineTo(0, 5 * step);

  path.moveTo(2 * step, 3 * step + step / 4);
  path.lineTo(2 * step, 2 * step - step / 4);

  path.moveTo(2 * step - step / 4, 2 * step + step / 4);
  path.lineTo(2 * step, 2 * step - step / 4);

  path.moveTo(2 * step + step / 4, 2 * step + step / 4);
  path.lineTo(2 * step, 2 * step - step / 4);

  path.moveTo(5 * step, 0);
  path.lineTo(4 * step, 0);
  path.lineTo(4 * step, 5 * step);
  path.lineTo(5 * step, 5 * step);

  path.moveTo(4 * step - step / 3, 2 * step + step / 4);
  path.lineTo(4 * step, step - step / 4);

  path.moveTo(4 * step + step / 3, 2 * step + step / 4);
  path.lineTo(4 * step, step - step / 4);

  return path;
}


QPainterPath SchematicScene::ccvsPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(0, 0);
  path.lineTo(2 * step, 0);
  path.lineTo(2 * step, step + step / 2);

  path.lineTo(step, 2 * step + step / 2);
  path.lineTo(2 * step, 3 * step + step / 2);
  path.lineTo(3 * step, 2 * step + step / 2);
  path.lineTo(2 * step, step + step / 2);

  path.moveTo(2 * step, 3 * step + step / 2);
  path.lineTo(2 * step, 5 * step);
  path.lineTo(0, 5 * step);

  path.moveTo(2 * step, 2 * step - step / 4);
  path.lineTo(2 * step, 2 * step + step / 4);

  path.moveTo(2 * step - step / 4, 2 * step);
  path.lineTo(2 * step + step / 4, 2 * step);

  path.moveTo(2 * step - step / 4, 3 * step);
  path.lineTo(2 * step + step / 4, 3 * step);

  path.moveTo(5 * step, 0);
  path.lineTo(4 * step, 0);
  path.lineTo(4 * step, 5 * step);
  path.lineTo(5 * step, 5 * step);

  // path.moveTo(4 * step - step / 3, 2 * step + step / 4);
  // path.lineTo(4 * step, step - step / 4);

  // path.moveTo(4 * step + step / 3, 2 * step + step / 4);
  // path.lineTo(4 * step, step - step / 4);
  
  path.moveTo(4 * step - step / 3, 3 * step - step / 4);
  path.lineTo(4 * step, 4 * step + step / 4);

  path.moveTo(4 * step + step / 3, 3 * step - step / 4);
  path.lineTo(4 * step, 4 * step + step / 4);

  return path;
}


QPainterPath SchematicScene::conductancePath()
{
  // same symbol as a resistor (ANSI zigzag or IEC/DIN rectangle)
  return resistorPath();
}


QPainterPath SchematicScene::currentSourcePath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(step, 0);
  path.lineTo(step, step + step / 2);
  path.moveTo(step, 3 * step + step / 2);
  path.arcTo(
    QRectF(
      QPointF(0, step + step / 2),
      QPointF(2 * step, 3 * step + step / 2)
    ), -90, 360
  );
  path.lineTo(step, 5 * step);

  path.moveTo(step, 3 * step + step / 4);
  path.lineTo(step, 2 * step - step / 4);

  path.moveTo(step - step / 4, 2 * step + step / 4);
  path.lineTo(step, 2 * step - step / 4);

  path.moveTo(step + step / 4, 2 * step + step / 4);
  path.lineTo(step, 2 * step - step / 4);

  return path;
}


QPainterPath SchematicScene::inductorPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.addRect(0, 2 * step, 1E-99, 1E-99);
  path.moveTo(5 * step, step);
  path.lineTo(4 * step, step);
  path.arcTo(
    QRectF(
      QPointF(3 * step, 0),
      QPointF(4 * step, 2 * step)
    ), 0, 180
  );
  path.arcTo(
    QRectF(
      QPointF(2 * step, 0),
      QPointF(3 * step, 2 * step)
    ), 0, 180
  );
  path.arcTo(
    QRectF(
      QPointF(step, 0),
      QPointF(2 * step, 2 * step)
    ), 0, 180
  );
  path.lineTo(0, step);

  return path;
}


QPainterPath SchematicScene::opAmplPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(step, 0);
  path.lineTo(step, 4 * step);
  path.lineTo(4 * step, 2 * step);
  path.lineTo(step, 0);

  path.moveTo(0, step);
  path.lineTo(step, step);
  path.moveTo(0, 3 * step);
  path.lineTo(step, 3 * step);
  path.moveTo(4 * step, 2 * step);
  path.lineTo(5 * step, 2 * step);

  path.moveTo(step + step / 2 - step / 4, step);
  path.lineTo(step + step / 2 + step / 4, step);

  path.moveTo(step + step / 2 - step / 4, 3 * step);
  path.lineTo(step + step / 2 + step / 4, 3 * step);

  path.moveTo(step + step / 2, 3 * step - step / 4);
  path.lineTo(step + step / 2, 3 * step + step / 4);

  return path;
}


QPainterPath SchematicScene::resistorPath()
{
  QPainterPath path;
  uint step = GridStep;

  if(theme::resistorStyle() == theme::ResistorStyle::Din) {
    // IEC 60617 / DIN: rectangle, 1:3, between the same terminals
    path.moveTo(0, step);
    path.lineTo(step, step);
    path.addRect(QRectF(step, step / 2., 3 * step, step));
    path.moveTo(4 * step, step);
    path.lineTo(5 * step, step);
    // invisible marks: same bounding box as the zigzag (0..2 steps high),
    // which rotation and saved files depend on
    path.addRect(2.5 * step, 0, 1E-99, 1E-99);
    path.addRect(2.5 * step, 2 * step, 1E-99, 1E-99);
    return path;
  }

  // ANSI zigzag
  path.moveTo(0, step);
  path.lineTo(step, step);
  path.lineTo(step + step / 2, 0);
  path.lineTo(2 * step, 2 * step);
  path.lineTo(2 * step + step / 2, 0);
  path.lineTo(3 * step, 2 * step);
  path.lineTo(3 * step + step / 2, 0);
  path.lineTo(4 * step, step);
  path.lineTo(5 * step, step);

  return path;
}


QPainterPath SchematicScene::vccsPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(0, 0);
  path.lineTo(2 * step, 0);
  path.lineTo(2 * step, step + step / 2);

  path.lineTo(step, 2 * step + step / 2);
  path.lineTo(2 * step, 3 * step + step / 2);
  path.lineTo(3 * step, 2 * step + step / 2);
  path.lineTo(2 * step, step + step / 2);

  path.moveTo(2 * step, 3 * step + step / 2);
  path.lineTo(2 * step, 5 * step);
  path.lineTo(0, 5 * step);

  path.moveTo(2 * step, 3 * step + step / 4);
  path.lineTo(2 * step, 2 * step - step / 4);

  path.moveTo(2 * step - step / 4, 2 * step + step / 4);
  path.lineTo(2 * step, 2 * step - step / 4);

  path.moveTo(2 * step + step / 4, 2 * step + step / 4);
  path.lineTo(2 * step, 2 * step - step / 4);

  path.moveTo(5 * step, 0);
  path.lineTo(4 * step, 0);
  path.moveTo(4 * step, 5 * step);
  path.lineTo(5 * step, 5 * step);

  path.moveTo(4 * step - step / 4, step);
  path.lineTo(4 * step + step / 4, step);

  path.moveTo(4 * step, 4 * step - step / 4);
  path.lineTo(4 * step, 4 * step + step / 4);

  path.moveTo(4 * step - step / 4, 4 * step);
  path.lineTo(4 * step + step / 4, 4 * step);

  return path;
}


QPainterPath SchematicScene::vcvsPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(0, 0);
  path.lineTo(2 * step, 0);
  path.lineTo(2 * step, step + step / 2);

  path.lineTo(step, 2 * step + step / 2);
  path.lineTo(2 * step, 3 * step + step / 2);
  path.lineTo(3 * step, 2 * step + step / 2);
  path.lineTo(2 * step, step + step / 2);

  path.moveTo(2 * step, 3 * step + step / 2);
  path.lineTo(2 * step, 5 * step);
  path.lineTo(0, 5 * step);

  path.moveTo(2 * step, 2 * step - step / 4);
  path.lineTo(2 * step, 2 * step + step / 4);

  path.moveTo(2 * step - step / 4, 2 * step);
  path.lineTo(2 * step + step / 4, 2 * step);

  path.moveTo(2 * step - step / 4, 3 * step);
  path.lineTo(2 * step + step / 4, 3 * step);

  path.moveTo(5 * step, 0);
  path.lineTo(4 * step, 0);
  path.moveTo(4 * step, 5 * step);
  path.lineTo(5 * step, 5 * step);

  path.moveTo(4 * step, step - step / 4);
  path.lineTo(4 * step, step + step / 4);

  path.moveTo(4 * step - step / 4, step);
  path.lineTo(4 * step + step / 4, step);

  path.moveTo(4 * step - step / 4, 4 * step);
  path.lineTo(4 * step + step / 4, 4 * step);

  return path;
}


QPainterPath SchematicScene::voltageSourcePath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(step, 0);
  path.lineTo(step, step + step / 2);
  path.moveTo(step, 3 * step + step / 2);
  path.arcTo(
    QRectF(
      QPointF(0, step + step / 2),
      QPointF(2 * step, 3 * step + step / 2)
    ), -90, 360
  );
  path.lineTo(step, 5 * step);

  path.moveTo(step - step / 4, 3 * step);
  path.lineTo(step + step / 4, 3 * step);

  path.moveTo(step, 2 * step - step / 4);
  path.lineTo(step, 2 * step + step / 4);

  path.moveTo(step - step / 4, 2 * step);
  path.lineTo(step + step / 4, 2 * step);

  return path;
}


QPainterPath SchematicScene::transformerPath()
{
  QPainterPath path;
  uint step = GridStep;

  path.moveTo(0, 0);
  path.lineTo(step, 0);

  path.moveTo(step, 5 * step);
  path.lineTo(step, 5 * step - step / 2);
  path.arcTo(
    QRectF(
      QPointF(0, 4 * step - step / 2),
      QPointF(2 * step, 5 * step - step / 2)
    ), -90, 180
  );
  path.arcTo(
    QRectF(
      QPointF(0, 3 * step - step / 2),
      QPointF(2 * step, 4 * step - step / 2)
    ), -90, 180
  );
  path.arcTo(
    QRectF(
      QPointF(0, 2 * step - step / 2),
      QPointF(2 * step, 3 * step - step / 2)
    ), -90, 180
  );
  path.arcTo(
    QRectF(
      QPointF(0, step - step / 2),
      QPointF(2 * step, 2 * step - step / 2)
    ), -90, 180
  );
  path.lineTo(step, 0);

  path.moveTo(step, 5 * step);
  path.lineTo(0, 5 * step);

  path.moveTo(2 * step + step / 3, step);
  path.lineTo(2 * step + step / 3, 4 * step);
  path.moveTo(3 * step - step / 3, step);
  path.lineTo(3 * step - step / 3, 4 * step);

  path.moveTo(5 * step, 0);
  path.lineTo(4 * step, 0);

  path.moveTo(4 * step, 5 * step);
  path.lineTo(4 * step, 5 * step - step / 2);
  path.arcTo(
    QRectF(
      QPointF(3 * step, 4 * step - step / 2),
      QPointF(5 * step, 5 * step - step / 2)
    ), -90, -180
  );
  path.arcTo(
    QRectF(
      QPointF(3 * step, 3 * step - step / 2),
      QPointF(5 * step, 4 * step - step / 2)
    ), -90, -180
  );
  path.arcTo(
    QRectF(
      QPointF(3 * step, 2 * step - step / 2),
      QPointF(5 * step, 3 * step - step / 2)
    ), -90, -180
  );
  path.arcTo(
    QRectF(
      QPointF(3 * step, step - step / 2),
      QPointF(5 * step, 2 * step - step / 2)
    ), -90, -180
  );
  path.lineTo(4 * step, 0);

  path.moveTo(4 * step, 5 * step);
  path.lineTo(5 * step, 5 * step);

  return path;
}


QPainterPath SchematicScene::mutualInductancePath()
{
  QPainterPath path;
  uint step = GridStep;

  path = transformerPath();
  path.addText(0, 3 * step, QFont(), "Lp");
  path.addText(4 * step, 3 * step, QFont(), "Ls");

  return path;
}


QtProperty* SchematicScene::itemProperties(QGraphicsItem* item)
{
  QVariant props = item->data(1);

  if(props == QVariant())
    return 0;

  return props.value<QtProperty*>();
}


SchematicScene::SupportedItemType SchematicScene::itemType(QGraphicsItem* item)
{
  QVariant type = item->data(0);

  if(type == QVariant())
    return NilItemType;

  bool ok;
  int intType = type.toInt(&ok);

  if(!ok)
    return NilItemType;

  return static_cast<SupportedItemType>(intType);
}


Item* SchematicScene::itemByType(SchematicScene::SupportedItemType type)
{
  Item* item;
  switch(type)
  {
  case CapacitorItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, GridStep));
      nodes.push_back(QPointF(5 * GridStep, GridStep));
      item = new Component(capacitorPath(), nodes);
      break;
    }
  case CCCSItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, 5 * GridStep));
      nodes.push_back(QPointF(0, 0));
      nodes.push_back(QPointF(5 * GridStep, 0));
      nodes.push_back(QPointF(5 * GridStep, 5 * GridStep));
      item = new Component(cccsPath(), nodes);
      break;
    }
  case CCVSItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, 0));
      nodes.push_back(QPointF(0, 5 * GridStep));
      nodes.push_back(QPointF(5 * GridStep, 5 * GridStep));
      nodes.push_back(QPointF(5 * GridStep, 0));
      item = new Component(ccvsPath(), nodes);
      break;
    }
  case ConductanceItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, GridStep));
      nodes.push_back(QPointF(5 * GridStep, GridStep));
      item = new Component(conductancePath(), nodes);
      break;
    }
  case CurrentSourceItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(GridStep, 5 * GridStep));
      nodes.push_back(QPointF(GridStep, 0));
      item = new Component(currentSourcePath(), nodes);
      break;
    }
  case InductorItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, GridStep));
      nodes.push_back(QPointF(5 * GridStep, GridStep));
      item = new Component(inductorPath(), nodes);
      break;
    }
  case OpAmplItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, 3* GridStep));
      nodes.push_back(QPointF(0, GridStep));
      nodes.push_back(QPointF(5 * GridStep, 2 * GridStep));
      item = new Component(opAmplPath(), nodes);
      break;
    }
  case ResistorItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, GridStep));
      nodes.push_back(QPointF(5 * GridStep, GridStep));
      item = new Component(resistorPath(), nodes);
      break;
    }
  case VCCSItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, 5 * GridStep));
      nodes.push_back(QPointF(0, 0));
      nodes.push_back(QPointF(5 * GridStep, 5 * GridStep));
      nodes.push_back(QPointF(5 * GridStep, 0));
      item = new Component(vccsPath(), nodes);
      break;
    }
  case VCVSItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, 0));
      nodes.push_back(QPointF(0, 5 * GridStep));
      nodes.push_back(QPointF(5 * GridStep, 0));
      nodes.push_back(QPointF(5 * GridStep, 5 * GridStep));
      item = new Component(vcvsPath(), nodes);
      break;
    }
  case SingleVoltageSourceItemType:
    {
      // one pin at the bottom; the other end is the ground
      item = new Component(singleVoltageSourcePath(),
        QPointF(GridStep, 2 * GridStep));
      break;
    }
  case VoltageSourceItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(GridStep, 0));
      nodes.push_back(QPointF(GridStep, 5 * GridStep));
      item = new Component(voltageSourcePath(), nodes);
      break;
    }
  case TransformerItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, 0));
      nodes.push_back(QPointF(0, 5 * GridStep));
      nodes.push_back(QPointF(5 * GridStep, 0));
      nodes.push_back(QPointF(5 * GridStep, 5 * GridStep));
      item = new Component(transformerPath(), nodes);
      break;
    }
  case MutualInductanceItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(0, 0));
      nodes.push_back(QPointF(0, 5 * GridStep));
      nodes.push_back(QPointF(5 * GridStep, 0));
      nodes.push_back(QPointF(5 * GridStep, 5 * GridStep));
      item = new Component(mutualInductancePath(), nodes);
      break;
    }
  case GroundItemType:
    {
      item = new Component(groundPath(), QPointF(GridStep, 0));
      break;
    }
  case PortItemType:
    {
      item = new Component(portPath(), QPointF(GridStep, GridStep));
      break;
    }
  case OutItemType:
    {
      Component* out = new Component(outPath(), QPointF(0, GridStep));
      out->setTag(QObject::tr("Out"), outPivotRect());
      item = out;
      break;
    }
  case VoltmeterItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(GridStep, 0));
      nodes.push_back(QPointF(GridStep, 5 * GridStep));
      item = new Component(voltmeterPath(), nodes);
      break;
    }
  case AmmeterItemType:
    {
      QList<QPointF> nodes;
      nodes.push_back(QPointF(GridStep, 0));
      nodes.push_back(QPointF(GridStep, 5 * GridStep));
      item = new Component(ammeterPath(), nodes);
      break;
    }
  case WireItemType:
    {
      item = new Wire;
      break;
    }
  case LabelItemType:
    {
      item = new Label;
      break;
    }
  case UserDefItemType:
    {
      QList<QPointF> nodes;
      item = new Component(nilPath(), nodes);
      break;
    }
  default:
    item = 0;
  }

  if(item)
    item->setData(0, type);

  return item;
}


QString SchematicScene::itemNameByType(SchematicScene::SupportedItemType type)
{
  switch(type)
  {
  case GroundItemType:
    return QString(QObject::tr("Ground"));
  case PortItemType:
    return QString(QObject::tr("Port"));
  case OutItemType:
    return QString(QObject::tr("Out"));
  case VoltmeterItemType:
    return QString(QObject::tr("Voltmeter"));
  case AmmeterItemType:
    return QString(QObject::tr("Ammeter"));
  case WireItemType:
    return QString(QObject::tr("Wire"));
  case ResistorItemType:
    return QString(QObject::tr("Resistor"));
  case ConductanceItemType:
    return QString(QObject::tr("Conductance"));
  case InductorItemType:
    return QString(QObject::tr("Inductor"));
  case CapacitorItemType:
    return QString(QObject::tr("Capacitor"));
  case VoltageSourceItemType:
    return QString(QObject::tr("Voltage Source"));
  case SingleVoltageSourceItemType:
    return QString(QObject::tr("Voltage Source (1-pin)"));
  case TransformerItemType:
    return QString(QObject::tr("Ideal Transformer"));
  case MutualInductanceItemType:
    return QString(QObject::tr("Mutual Inductance"));
  case CurrentSourceItemType:
    return QString(QObject::tr("Current Source"));
  case VCVSItemType:
    return QString(QObject::tr("VCVS"));
  case CCVSItemType:
    return QString(QObject::tr("CCVS"));
  case VCCSItemType:
    return QString(QObject::tr("VCCS"));
  case CCCSItemType:
    return QString(QObject::tr("CCCS"));
  case OpAmplItemType:
    return QString(QObject::tr("Operational Amplifier"));
  case UserDefItemType:
    return QString(QObject::tr("User Defined"));
  default:
    return QString(QObject::tr("Unknow"));
  }

  return QString();
}


QChar SchematicScene::itemIdByType(SchematicScene::SupportedItemType type)
{
  switch(type)
  {
  case ResistorItemType:
    return QChar('R');
  case ConductanceItemType:
    return QChar('G');
  case InductorItemType:
    return QChar('L');
  case CapacitorItemType:
    return QChar('C');
  case VoltageSourceItemType:
  case SingleVoltageSourceItemType:
    return QChar('V');
  case TransformerItemType:
    return QChar('n');
  case MutualInductanceItemType:
    return QChar('K');
  case CurrentSourceItemType:
    return QChar('I');
  case VCVSItemType:
    return QChar('E');
  case CCVSItemType:
    return QChar('Y');
  case VCCSItemType:
    return QChar('H');
  case CCCSItemType:
    return QChar('F');
  case OpAmplItemType:
    return QChar('A');
  case UserDefItemType:
    return QChar('X');
  default:
    break;
  }

  return QChar();
}


QPainterPath SchematicScene::userDefPath(uint ports)
{
  QPainterPath path;
  uint step = GridStep;
  uint side = (ports + (ports % 2)) / 2;

  if(side) {
    path.moveTo(step, 0);
    path.lineTo(step, step * 2 * side);
    path.lineTo(5 * step, step * 2 * side);
    path.lineTo(5 * step, 0);
    path.lineTo(step, 0);
  } else {
    path.moveTo(0, 0);
    path.lineTo(0, 2 * step);
    path.lineTo(2 * step, 2 * step);
    path.lineTo(2 * step, 0);
    path.lineTo(0, 0);
    path.moveTo(0, 0);
    path.lineTo(2 * step, 2 * step);
    path.moveTo(2 * step, 0);
    path.lineTo(0, 2 * step);
  }

  for(uint i = 0; i < side; ++i) {
    path.moveTo(0, step * (2 * i + 1) + step / 3);
    path.lineTo(0, step * (2 * i + 1));
    path.lineTo(step, step * (2 * i + 1));
    path.addText(step * 4 / 3, step * (2 * i + 1) + step / 2,
        theme::schematicFont(8),
        QString::number(i + 1));
  }

  side -= ports % 2;
  for(uint i = 0; i < side; ++i) {
    path.moveTo(6 * step, step * (2 * i + 1) + step / 3);
    path.lineTo(6 * step, step * (2 * i + 1));
    path.lineTo(5 * step, step * (2 * i + 1));
    path.addText(4 * step - step / 3, step * (2 * i + 1) + step / 2,
        theme::schematicFont(8),
        QString::number(i + side + ports % 2 + 1));
  }

  return path;
}


QList<QPointF> SchematicScene::userDefNodes(uint ports)
{
  QList<QPointF> nodes;

  uint side = (ports + (ports % 2)) / 2;
  for(uint i = 0; i < side; ++i)
    nodes.push_back(QPointF(0, GridStep * (2 * i + 1)));

  side -= ports % 2;
  for(uint i = 0; i < side; ++i)
    nodes.push_back(QPointF(6 * GridStep, GridStep * (2 * i + 1)));

  return nodes;
}


SchematicScene::SchematicScene(QObject* parent): QGraphicsScene(parent) {
  setupProperties();
  init();

  // keep T-junction connections (and their dots) in sync with the drawing:
  // recompute once per batch of changes
  connect(this, &QGraphicsScene::changed, this, [this]() {
    if(wireTapsPending_)
      return;
    wireTapsPending_ = true;
    QTimer::singleShot(0, this, [this]() {
      wireTapsPending_ = false;
      updateWireTaps();
    });
  });
}


void SchematicScene::updateWireTaps()
{
  foreach(Item* item, wireList_)
    static_cast<Wire*>(item)->updateTaps();
}


SchematicScene::~SchematicScene() {
  // delete itemized sub-circuits
  foreach(Item* item, userDefList_)
    delete item->data(101).value< QPointer<qsapecng::SchematicScene> >();

  delete undoRedoStack_;
}


QPointF SchematicScene::placementPos(const QPointF& pointer) const
{
  // the part being placed is centred on the pointer; the offset is a whole
  // number of grid steps, so its pins stay on the grid
  if(!item_)
    return closestGridPoint(pointer);
  const QRectF box = item_->transform().mapRect(item_->boundingRect());
  return closestGridPoint(pointer) - closestGridPoint(box.center());
}


QPointF SchematicScene::closestGridPoint(const QPointF& pos) const
{
  QPoint point = pos.toPoint();
  int x = point.x();
  int y = point.y();

  if(x < 0) x -= (GridStep >> 1) - 1;
  else x += (GridStep >> 1);
  x -= x % GridStep;

  if(y < 0) y -= (GridStep >> 1) - 1;
  else y += (GridStep >> 1);
  y -= y % GridStep;

  return QPointF(x,y);
}


void SchematicScene::setActiveItem(SchematicScene::SupportedItemType item)
{
  resetStatus();
  activeItem_ = item;
  if(hasActiveItem())
    foreach(QGraphicsView* view, views())
      view->setCursor(Qt::CrossCursor);
  emit placementChanged();
}


QList<Item*> SchematicScene::activeItems() const
{
  QList<Item*> items;

  items.append(portList_);
  items.append(groundList_);
  items.append(standardList_);
  items.append(userDefList_);
  items.append(wireList_);
  items.append(labelList_);
  if(out_)
    items.append(out_);

  return items;
}


QByteArray SchematicScene::registerUserDef(const SchematicScene& scene)
{
    std::ostringstream stream;
    sapecng::abstract_parser* parser = new SchematicSceneParser(scene);
    sapecng::abstract_builder* builder =
      sapecng::builder_factory::builder(
        sapecng::builder_factory::INFO, stream);
    
    if(builder)
      parser->parse(*builder);
    
    QByteArray str(stream.str().c_str());
    QByteArray md5 = QCryptographicHash::hash(str, QCryptographicHash::Md5);
    
    delete builder;
    delete parser;
    
    if(!userDefMap_.contains(md5))
      userDefMap_.insert(md5, stream.str());
    
    return md5;
}


std::string SchematicScene::queryUserDef(QByteArray md5)
{
  std::string rep;

  if(!userDefMap_.contains(md5))
    userDefMap_.value(md5);
  
  return rep;
}


void SchematicScene::setUserDefRequest()
{
  if(0 == views().size())
    return;
  
  QString fileName = QFileDialog::getOpenFileName(views().at(0),
      tr("Read file"), Settings().workspace(),
      QString("%1;;%2")
        .arg(tr("Info files (*.info)"))
        .arg(tr("XML files (*.xml)"))
    );

  if(!fileName.isEmpty()) {
    SchematicScene* scene = new SchematicScene(this);
    sapecng::abstract_builder* builder = new SchematicSceneBuilder(*scene);
    
    QFileInfo fileInfo(fileName);
    std::ifstream in_file(QFile::encodeName(fileName));
    sapecng::abstract_parser* parser =
      sapecng::parser_factory::parser(
        fileInfo.suffix().toStdString(), in_file);

    try {
      if(parser)
        parser->parse(*builder);
    } catch (...) {
      delete parser;
      delete builder;
      delete scene;
      throw;
    }

    delete parser;
    delete builder;
    
    QByteArray md5 = registerUserDef(*scene);
    int size = scene->size();

    delete scene;
    
    setActiveItem(SchematicScene::UserDefItemType);
    userDefSessionRequested_ = true;
    emit placementChanged();
    userDefSize_ = size;
    userDefMD5_ = md5;
  }
}


void SchematicScene::addItems(QList<QGraphicsItem*> items)
{
  AddItems* command = new AddItems(this, items);
  undoRedoStack_->push(command);
}


void SchematicScene::addItems(QGraphicsItem* item)
{
  AddItems* command = new AddItems(this, item);
  undoRedoStack_->push(command);
}


void SchematicScene::addSupportedItem(QGraphicsItem* gItem, bool init)
{
  if(!gItem)
    return;

  Item* item = qgraphicsitem_cast<Item*>(gItem);
  if(!item) {
    addItem(gItem);
    return;
  }

  bool ok;
  QVariant data = item->data(0);
  if(!data.isNull()) {
    SupportedItemType type = (SupportedItemType) data.toInt(&ok);
    if(ok && type != NilItemType) {
      addItem(item);
      switch(type)
      {
      case GroundItemType:
        {
          if(!groundList_.contains(item))
            groundList_.push_back(item);
          break;
        }
      case PortItemType:
        {
          if(!portList_.contains(item)) {
            portList_.push_back(item);

            static_cast<Component*>(item)->label()->setPlainText(
              QString::number(portList_.size()));
          }
          break;
        }
      case OutItemType:
      case VoltmeterItemType:
      case AmmeterItemType:
        {
          if(!out_) {
            out_ = item;
          } else {
            out_->setOpacity(0.5);
            outList_.push_back(out_);
            out_ = item;
          }
          break;
        }
      case WireItemType:
        {
          if(!wireList_.contains(item))
            wireList_.push_back(item);
          break;
        }
      case LabelItemType:
        {
          if(!labelList_.contains(item))
            labelList_.push_back(item);
          break;
        }
      case CapacitorItemType:
      case ConductanceItemType:
      case InductorItemType:
      case ResistorItemType:
      case CCCSItemType:
      case CCVSItemType:
      case VCCSItemType:
      case VCVSItemType:
      case OpAmplItemType:
      case CurrentSourceItemType:
      case VoltageSourceItemType:
      case SingleVoltageSourceItemType:
      case TransformerItemType:
      case MutualInductanceItemType:
        {
          if(!standardList_.contains(item)) {
            standardList_.push_back(item);

            QtProperty* name =
              stringManager_->addProperty(itemNameByType(type));

            if(init) {
              stringManager_->setValue(name,
                  nextFreeName(type));
            } else {
              stringManager_->setValue(name, tr("noname"));
            }

            item->setData(1, QVariant::fromValue<QtProperty*>(name));
            static_cast<Component*>(item)->label()->setProperty(name);
            connect(
              stringManager_,
              SIGNAL(valueChanged(QtProperty*, const QString&)),
              static_cast<Component*>(item)->label(),
              SLOT(valueChanged(QtProperty*, const QString&))
            );
            // e.g. an op-amp switched between ideal and real
            PropertyTextItem* label = static_cast<Component*>(item)->label();
            connect(boolManager_, &QtBoolPropertyManager::valueChanged,
              label, [label, name](QtProperty* p, bool) {
                if(name->subProperties().contains(p))
                  label->refreshText();
              });

            if(type != OpAmplItemType) {
              QtProperty* pS = boolManager_->addProperty(tr("Symbolic"));
              boolManager_->setValue(pS, true);

              QtProperty* pV =
                stringManager_->addProperty(
                    type == MutualInductanceItemType
                  ? tr("M")
                  : tr("Value")
                );

              stringManager_->setValue(pV, "1.0");
              if(type == CapacitorItemType
                  || type == ConductanceItemType
                  || type == InductorItemType
                  || type == ResistorItemType
                )
              {
                QRegularExpression sn = Settings().notation();
                stringManager_->setRegExp(pV, sn);
              } else {
                QRegularExpression sn = Settings().notation();
                stringManager_->setRegExp(pV, sn);
              }

              name->addSubProperty(pS);
              name->addSubProperty(pV);

              // R, L, C: ideal, or with parasitics
              const QList<Parasitic> extra = parasitics(type);
              if(!extra.isEmpty()) {
                QtProperty* pI = boolManager_->addProperty(tr("Ideal"));
                boolManager_->setValue(pI, true);
                name->addSubProperty(pI);
                foreach(const Parasitic& par, extra) {
                  QtProperty* pp = stringManager_->addProperty(par.property);
                  stringManager_->setRegExp(pp, Settings().notation());
                  stringManager_->setValue(pp, par.defaultValue);
                  pp->setToolTip(par.description);
                  name->addSubProperty(pp);
                }
              }
            } else {
              // op-amp: ideal (nullor) or open-loop gain A0 with an
              // optional gain-bandwidth product (single-pole model)
              QtProperty* pI = boolManager_->addProperty(tr("Ideal"));
              boolManager_->setValue(pI, true);
              QtProperty* pS = boolManager_->addProperty(tr("Symbolic"));
              boolManager_->setValue(pS, false);
              QtProperty* pV = stringManager_->addProperty(tr("Value"));
              stringManager_->setValue(pV, "100000");
              QtProperty* pB = stringManager_->addProperty(tr("GBW"));
              stringManager_->setValue(pB, "1000000");

              name->addSubProperty(pI);
              name->addSubProperty(pS);
              name->addSubProperty(pV);
              name->addSubProperty(pB);
            }

            if(type == MutualInductanceItemType) {
              QtProperty* lpn = stringManager_->addProperty(tr("lp:name"));
              QtProperty* lpv = stringManager_->addProperty(tr("lp:value"));
              QtProperty* lsn = stringManager_->addProperty(tr("ls:name"));
              QtProperty* lsv = stringManager_->addProperty(tr("ls:value"));

              stringManager_->setValue(lpn, name->valueText() + "_Lp");
              stringManager_->setValue(lsn, name->valueText() + "_Ls");

              QRegularExpression sn = Settings().notation();
              stringManager_->setRegExp(lpv, sn);
              stringManager_->setValue(lpv, "1.0");
              stringManager_->setRegExp(lsv, sn);
              stringManager_->setValue(lsv, "1.0");

              name->addSubProperty(lpn);
              name->addSubProperty(lpv);
              name->addSubProperty(lsn);
              name->addSubProperty(lsv);
            }

            static_cast<Component*>(item)->label()->setValueUnit(
              valueUnit(type));

            typeRootMap_[type]->addSubProperty(name);
            properties_->addSubProperty(typeRootMap_[type]);
          }
          break;
        }
      case UserDefItemType:
        {
          if(!userDefList_.contains(item)) {
            userDefList_.push_back(item);

            QtProperty* name =
              stringManager_->addProperty(itemNameByType(type));

            stringManager_->setValue(name,
                nextFreeName(type));

            item->setData(1, QVariant::fromValue<QtProperty*>(name));
            static_cast<Component*>(item)->label()->setProperty(name);
            connect(
              stringManager_,
              SIGNAL(valueChanged(QtProperty*, const QString&)),
              static_cast<Component*>(item)->label(),
              SLOT(valueChanged(QtProperty*, const QString&))
            );

            QPointer<qsapecng::SchematicScene> rep =
              item->data(101)
                .value< QPointer<qsapecng::SchematicScene> >();

            name->addSubProperty(rep->properties());

            static_cast<Component*>(item)->label()->refreshText();

            typeRootMap_[type]->addSubProperty(name);
            properties_->addSubProperty(typeRootMap_[type]);
          }
          break;
        }
      default:
        break;
      }
    }
  }
}


void SchematicScene::removeSupportedItem(QGraphicsItem* gItem)
{
  if(!gItem)
    return;

  Item* item = qgraphicsitem_cast<Item*>(gItem);
  if(!item) {
    if(gItem->scene() == this)
      removeItem(gItem);
    return;
  }

  bool ok;
  QVariant data = item->data(0);
  if(!data.isNull()) {
    SupportedItemType type = (SupportedItemType) data.toInt(&ok);
    if(ok && type != NilItemType) {

      if(item->scene() == this)
        removeItem(item);

      switch(type)
      {
      case GroundItemType:
        {
          groundList_.removeAll(item);
          break;
        }
      case PortItemType:
        {
          portList_.removeAll(item);
          for(QList<Item*>::size_type i = 0; i < portList_.size(); ++i)
            static_cast<Component*>(portList_.at(i))->label()->setPlainText(
              QString::number(i + 1));
          break;
        }
      case OutItemType:
      case VoltmeterItemType:
      case AmmeterItemType:
        {
          if(out_ == item) {
            if(outList_.isEmpty()) {
              out_ = 0;
            } else {
              out_ = outList_.back();
              outList_.pop_back();
              out_->setOpacity(1.0);
            }
          } else {
            if(outList_.contains(item)) {
              item->setOpacity(1.0);
              outList_.removeAll(item);
            }
          }
          break;
        }
      case WireItemType:
        {
          wireList_.removeAll(item);
          break;
        }
      case LabelItemType:
        {
          labelList_.removeAll(item);
          break;
        }
      case CapacitorItemType:
      case ConductanceItemType:
      case InductorItemType:
      case ResistorItemType:
      case CCCSItemType:
      case CCVSItemType:
      case VCCSItemType:
      case VCVSItemType:
      case CurrentSourceItemType:
      case VoltageSourceItemType:
      case SingleVoltageSourceItemType:
      case TransformerItemType:
      case MutualInductanceItemType:
      case OpAmplItemType:
        {
          if(standardList_.contains(item)) {
            if(itemProperties(item)) {
              typeRootMap_[type]->removeSubProperty(itemProperties(item));
              if(typeRootMap_[type]->subProperties().size() == 0)
                properties_->removeSubProperty(typeRootMap_[type]);
            }

            standardList_.removeAll(item);
          }
          break;
        }
      case UserDefItemType:
        {
          if(userDefList_.contains(item)) {
            if(itemProperties(item)) {
              typeRootMap_[type]->removeSubProperty(itemProperties(item));
              if(typeRootMap_[type]->subProperties().size() == 0)
                properties_->removeSubProperty(typeRootMap_[type]);
            }

            userDefList_.removeAll(item);
          }
          break;
        }
      default:
        break;
      }

    }
  }
}


void SchematicScene::clearSupportedItem(QGraphicsItem* gItem)
{
  if(!gItem)
    return;

  Item* item = qgraphicsitem_cast<Item*>(gItem);
  if(!item) {
    if(gItem->scene() == this)
      removeItem(gItem);
    return;
  }

  bool ok;
  QVariant data = item->data(0);
  if(!data.isNull()) {
    SupportedItemType type = (SupportedItemType) data.toInt(&ok);
    if(ok && type != NilItemType) {

      if(item->scene() == this)
        removeItem(item);

      switch(type)
      {
      case GroundItemType:
        {
          groundList_.removeAll(item);
          break;
        }
      case PortItemType:
        {
          portList_.removeAll(item);
          for(QList<Item*>::size_type i = 0; i < portList_.size(); ++i)
            static_cast<Component*>(portList_.at(i))->label()->setPlainText(
              QString::number(i + 1));
          break;
        }
      case OutItemType:
      case VoltmeterItemType:
      case AmmeterItemType:
        {
          if(out_ == item) {
            if(outList_.isEmpty()) {
              out_ = 0;
            } else {
              out_ = outList_.back();
              outList_.pop_back();
              out_->setOpacity(1.0);
            }
          } else {
            if(outList_.contains(item)) {
              item->setOpacity(1.0);
              outList_.removeAll(item);
            }
          }
          break;
        }
      case WireItemType:
        {
          wireList_.removeAll(item);
          break;
        }
      case LabelItemType:
        {
          labelList_.removeAll(item);
          break;
        }
      case CapacitorItemType:
      case ConductanceItemType:
      case InductorItemType:
      case ResistorItemType:
      case CCCSItemType:
      case CCVSItemType:
      case VCCSItemType:
      case VCVSItemType:
      case CurrentSourceItemType:
      case VoltageSourceItemType:
      case SingleVoltageSourceItemType:
      case TransformerItemType:
      case MutualInductanceItemType:
      case OpAmplItemType:
        {
          if(standardList_.contains(item)) {
            QtProperty* props = itemProperties(item);
            if(props) {
              typeRootMap_[type]->removeSubProperty(itemProperties(item));
              if(typeRootMap_[type]->subProperties().size() == 0)
                properties_->removeSubProperty(typeRootMap_[type]);

              foreach(QtProperty* prop, props->subProperties())
                delete prop;

              delete props;
            }

            standardList_.removeAll(item);
          }
          break;
        }
      case UserDefItemType:
        {
          if(userDefList_.contains(item)) {
            if(itemProperties(item)) {
              typeRootMap_[type]->removeSubProperty(itemProperties(item));
              if(typeRootMap_[type]->subProperties().size() == 0)
                properties_->removeSubProperty(typeRootMap_[type]);
            }

            userDefList_.removeAll(item);
          }
          break;
        }
      default:
        break;
      }

    }
  }
}


void SchematicScene::setWireSessionRequest(bool connectedWire)
{
  resetStatus();

  wireSessionRequested_ = true;
  horizontalFirst_ = true;
  bendDecided_ = false;
  hasValidFromPoint_ = false;
  connectedWire_ = connectedWire;
  wirePen_.setColor(theme::sheetForeground());
  preWireItem_ = addLine(QLineF(fromPoint_, fromPoint_), wirePen_);
  postWireItem_ = addLine(QLineF(fromPoint_, fromPoint_), wirePen_);
  preWireItem_->setVisible(false);
  postWireItem_->setVisible(false);

  foreach(QGraphicsView* view, views())
    view->setCursor(Qt::CrossCursor);
}


void SchematicScene::joinWires(Wire* w1, Wire* w2)
{
  if(!w1 || !w2)
    return;

  JoinWires* command = new JoinWires(w1, w2);
  undoRedoStack_->push(command);
}


void SchematicScene::modifyWire(Wire* wire)
{
  if(!hasActiveItem() && !hasUserDefReq() && !hasPendingWire() && wire) {
    resetStatus();

    wireSessionRequested_ = true;
    horizontalFirst_ = (wire->orientation() == Qt::Horizontal);
    bendDecided_ = true;
    fromPoint_ = wire->mapToScene(wire->fromPoint());
    QPointF toPoint = wire->mapToScene(wire->toPoint());
    hasValidFromPoint_ = true;
    connectedWire_ = wire->isJunctionsConnected();
    wirePen_.setColor(theme::sheetForeground());
    preWireItem_ = addLine(QLineF(fromPoint_, toPoint), wirePen_);
    postWireItem_ = addLine(QLineF(fromPoint_, fromPoint_), wirePen_);

    wireInProgress_ = wire;
    wire->setVisible(false);
    preWireItem_->setVisible(true);
    postWireItem_->setVisible(false);

    foreach(QGraphicsView* view, views())
      view->setCursor(Qt::CrossCursor);
  }
}


void SchematicScene::createLabel()
{
  resetStatus();

  AddItems* command = new AddItems(this,
    itemByType(LabelItemType), QPointF(3 * GridStep, 3 * GridStep));
  undoRedoStack_->push(command);
}


void SchematicScene::moveSelectedItems(QPointF offset)
{
  // arrow keys: one step, wires attached to the moved pins follow
  if(drag_.active)
    endDrag(true);
  if(selectedItems().isEmpty())
    return;
  beginDrag();
  drag_.total = offset;
  endDrag(true);
}


void SchematicScene::dragSelectedBy(const QPointF& offset)
{
  if(!drag_.active) {
    if(selectedItems().isEmpty())
      return;
    beginDrag();
  }
  drag_.total += offset;
  foreach(QGraphicsItem* item, drag_.items)
    item->moveBy(offset.x(), offset.y());
  updateDragPreview();
}


QList<QLineF> SchematicScene::rubberSegments(const QPointF& fixed,
    const QPointF& moving, Qt::Orientation orientation)
{
  // straight when still in line; otherwise an L whose leg at the moved
  // pin keeps the wire's direction (the pin is entered as before)
  if(fixed == moving)
    return {};
  if(fixed.x() == moving.x() || fixed.y() == moving.y())
    return { QLineF(fixed, moving) };
  const QPointF corner = (orientation == Qt::Horizontal)
    ? QPointF(fixed.x(), moving.y()) : QPointF(moving.x(), fixed.y());
  return { QLineF(fixed, corner), QLineF(corner, moving) };
}


void SchematicScene::beginDrag()
{
  drag_ = DragSession();
  drag_.active = true;

  // the moved items (top level only: labels move with their part)
  foreach(QGraphicsItem* item, selectedItems())
    if(!item->parentItem() && dynamic_cast<Item*>(item))
      drag_.items.append(item);

  // their connection points: pins, and the points of moved wires
  QList<QPointF> points;
  foreach(QGraphicsItem* item, drag_.items)
    foreach(QGraphicsItem* child, item->childItems())
      if(dynamic_cast<GraphicsNode*>(child))
        points.append(child->scenePos());
  auto attached = [&points](const QPointF& p) {
      foreach(const QPointF& q, points)
        if(qAbs(p.x() - q.x()) < 0.5 && qAbs(p.y() - q.y()) < 0.5)
          return true;
      return false;
    };

  // wires that are not moved but end on a moved point
  foreach(Item* item, wireList_) {
    Wire* wire = static_cast<Wire*>(item);
    if(drag_.items.contains(wire) || !wire->isVisible() || !wire->scene())
      continue;
    const QPointF a = wire->mapToScene(wire->fromPoint());
    const QPointF b = wire->mapToScene(wire->toPoint());
    const bool onA = attached(a), onB = attached(b);
    if(onA && onB) {
      drag_.items.append(wire);           // between two moved pins
    } else if(onA || onB) {
      RubberWire r;
      r.wire = wire;
      r.fixed = onA ? b : a;
      r.moving = onA ? a : b;
      r.orientation = wire->orientation();
      for(int i = 0; i < 2; ++i) {
        r.preview[i] = new QGraphicsLineItem;
        r.preview[i]->setPen(wirePen_);
        r.preview[i]->setZValue(wire->zValue());
        r.preview[i]->setVisible(false);
        addItem(r.preview[i]);
      }
      wire->setVisible(false);
      drag_.rubber.append(r);
    }
  }

  // pins of parts that stay, touching a moved pin directly (e.g. a ground
  // on a capacitor): a new wire joins them
  foreach(QGraphicsItem* item, items()) {
    GraphicsNode* node = dynamic_cast<GraphicsNode*>(item);
    if(!node || !node->parentItem() || !node->isVisible())
      continue;
    QGraphicsItem* owner = node->parentItem();
    if(drag_.items.contains(owner) || dynamic_cast<Wire*>(owner))
      continue;
    const QPointF p = node->scenePos();
    if(!attached(p))
      continue;
    RubberWire r;
    r.wire = 0;
    r.fixed = p;
    r.moving = p;
    // leave the pin the way it points out of its part
    const QPointF out = p - owner->sceneBoundingRect().center();
    r.orientation = qAbs(out.x()) > qAbs(out.y()) ? Qt::Horizontal : Qt::Vertical;
    for(int i = 0; i < 2; ++i) {
      r.preview[i] = new QGraphicsLineItem;
      r.preview[i]->setPen(wirePen_);
      r.preview[i]->setVisible(false);
      addItem(r.preview[i]);
    }
    drag_.rubber.append(r);
  }

  foreach(QGraphicsItem* item, drag_.items)
    drag_.origin.append(item->pos());
  updateDragPreview();
}


void SchematicScene::updateDragPreview()
{
  for(RubberWire& r : drag_.rubber) {
    const QList<QLineF> segments = r.wire
      ? rubberSegments(r.fixed, r.moving + drag_.total, r.orientation)
      : rubberSegments(r.moving + drag_.total, r.fixed, r.orientation);
    for(int i = 0; i < 2; ++i) {
      r.preview[i]->setVisible(i < segments.size());
      if(i < segments.size())
        r.preview[i]->setLine(segments.at(i));
    }
  }
}


void SchematicScene::endDrag(bool commit)
{
  if(!drag_.active)
    return;
  DragSession drag = drag_;
  drag_ = DragSession();

  // back to the start; the commands below make the move (undoable)
  for(int i = 0; i < drag.items.size(); ++i)
    drag.items.at(i)->setPos(drag.origin.at(i));
  for(RubberWire& r : drag.rubber) {
    for(int i = 0; i < 2; ++i) {
      removeItem(r.preview[i]);
      delete r.preview[i];
    }
    if(r.wire)
      r.wire->setVisible(true);
  }

  if(!commit || drag.total.isNull() || drag.items.isEmpty())
    return;

  undoRedoStack_->beginMacro(tr("Move item(s)"));
  undoRedoStack_->push(new MoveItems(this, drag.items, drag.total));
  for(const RubberWire& r : drag.rubber) {
    const bool junctions = r.wire ? r.wire->isJunctionsConnected() : false;
    if(r.wire)
      undoRedoStack_->push(new RemoveItems(this, r.wire));
    // a new wire leaves the fixed pin straight (the leg at the moved pin
    // keeps the old wire's direction)
    const QList<QLineF> lines = r.wire
      ? rubberSegments(r.fixed, r.moving + drag.total, r.orientation)
      : rubberSegments(r.moving + drag.total, r.fixed, r.orientation);
    foreach(const QLineF& line, lines)
    {
      const QPointF topLeft(qMin(line.x1(), line.x2()),
                            qMin(line.y1(), line.y2()));
      undoRedoStack_->push(new AddWire(this, topLeft, line, junctions));
    }
  }
  undoRedoStack_->endMacro();
}


void SchematicScene::cutSelectedItems()
{
  undoRedoStack_->beginMacro(tr("Cut item(s)"));
  copySelectedItems();
  binSelectedItems();
  undoRedoStack_->endMacro();
}


void SchematicScene::copySelectedItems()
{
  std::ostringstream stream;
  sapecng::abstract_builder* out =
    sapecng::builder_factory::builder(sapecng::builder_factory::INFO, stream);

  SchematicSceneParser* parser =
    new SchematicSceneParser(*this, selectedItems());

  parser->parse(*out);
  
  delete parser;
  delete out;

  QApplication::clipboard()->setText(
    QString::fromStdString(stream.str()));
}


void SchematicScene::pasteItems()
{
  SchematicSceneBuilder* out = 
    new SchematicSceneBuilder(*this, lastMousePressPos_);

  std::istringstream stream;
  stream.str(QApplication::clipboard()->text().toStdString());
  sapecng::abstract_parser* parser =
    sapecng::parser_factory::parser(sapecng::parser_factory::INFO, stream);

  undoRedoStack_->beginMacro(QObject::tr("Add item(s)"));
  parser->parse(*out);
  undoRedoStack_->endMacro();
  delete parser;
  delete out;
}


void SchematicScene::rotateSelectedItems()
{
  QList<QGraphicsItem*> items = selectedItems();
  if(items.isEmpty())
    return;

  RotateItems* command = new RotateItems(items);
  undoRedoStack_->push(command);
}


void SchematicScene::editItemProperties(QGraphicsItem* item)
{
  QtProperty* properties = itemProperties(item);
  if(!properties)
    return;

  QWidget* parent = views().isEmpty() ? 0 : views().first()->window();
  ComponentDialog dialog(itemType(item), properties, parent);
  if(dialog.exec() != QDialog::Accepted)
    return;

  const QList<PropertyChange> changes = dialog.changes();
  if(!changes.isEmpty())
    undoRedoStack_->push(new ChangeProperties(changes));
}


void SchematicScene::updateResistorSymbols()
{
  QList<Item*> items = standardList_;
  if(item_)
    items.append(item_);   // item being placed

  foreach(Item* item, items) {
    SupportedItemType type = itemType(item);
    if(type == ResistorItemType)
      static_cast<Component*>(item)->setPath(resistorPath());
    else if(type == ConductanceItemType)
      static_cast<Component*>(item)->setPath(conductancePath());
  }

  // sub-circuits
  foreach(Item* item, userDefList_) {
    QPointer<qsapecng::SchematicScene> rep =
      item->data(101).value< QPointer<qsapecng::SchematicScene> >();
    if(rep)
      rep->updateResistorSymbols();
  }
}


QString SchematicScene::nextFreeName(SupportedItemType type)
{
  // names already in use in this schematic (e.g. loaded from a file, whose
  // parts did not advance the counter)
  QSet<QString> used;
  foreach(Item* item, standardList_ + userDefList_) {
    QtProperty* prop = item->data(1).value<QtProperty*>();
    if(prop)
      used.insert(prop->valueText());
  }

  QString name;
  do {
    name = itemIdByType(type) + QString::number(++itemCntMap_[type]);
  } while(used.contains(name));
  return name;
}


QString SchematicScene::toolHint() const
{
  if(hasPendingWire()) {
    if(wireInProgress_)
      return tr("Editing a wire: click where it should end - Esc cancels");
    if(hasValidFromPoint_)
      return tr("Wire: click to add a corner; ending on a pin or a wire "
                "(or a double-click) finishes it - / flips the bend, "
                "Esc stops");
    return tr("Wire: click where the wire starts - Esc leaves the tool");
  }
  if(hasUserDefReq())
    return tr("Placing a sub-circuit: click to place it - R rotates, "
              "Esc cancels");
  if(hasActiveItem())
    return tr("Placing %1: click to place it - R rotates, Esc cancels")
      .arg(itemNameByType(activeItem_).toLower());
  return QString();
}


int SchematicScene::partCount() const
{
  return standardList_.size() + userDefList_.size();
}


int SchematicScene::wireCount() const
{
  int n = 0;
  foreach(QGraphicsItem* item, items())
    if(!item->parentItem() && dynamic_cast<Wire*>(item))
      ++n;
  return n;
}


QStringList SchematicScene::sourceNames() const
{
  QStringList names;
  foreach(Item* item, standardList_) {
    const SupportedItemType type = itemType(item);
    if(type == VoltageSourceItemType || type == SingleVoltageSourceItemType
        || type == CurrentSourceItemType)
      if(QtProperty* prop = item->data(1).value<QtProperty*>())
        names << prop->valueText();
  }
  return names;
}


void SchematicScene::refreshComponentLabels()
{
  QList<Item*> items = standardList_ + userDefList_;
  if(item_)
    items.append(item_);

  foreach(Item* item, items)
    if(Component* component = dynamic_cast<Component*>(item))
      component->label()->refreshText();

  foreach(Item* item, userDefList_) {
    QPointer<qsapecng::SchematicScene> rep =
      item->data(101).value< QPointer<qsapecng::SchematicScene> >();
    if(rep)
      rep->refreshComponentLabels();
  }
}


QList<SchematicScene::Parasitic> SchematicScene::parasitics(
    SchematicScene::SupportedItemType type)
{
  switch(type)
  {
  case ResistorItemType:
    return {
      { QStringLiteral("Ls"), "ls", QStringLiteral("H"), QStringLiteral("1e-09"),
        tr("Series inductance (leads, film spiral)") },
      { QStringLiteral("Cp"), "cp", QStringLiteral("F"), QStringLiteral("2e-13"),
        tr("Parallel capacitance across the terminals") } };
  case InductorItemType:
    return {
      { QStringLiteral("Rs"), "rs", QStringLiteral("Ω"), QStringLiteral("0.1"),
        tr("Winding (DC) resistance") },
      { QStringLiteral("Cp"), "cp", QStringLiteral("F"), QStringLiteral("1e-12"),
        tr("Winding capacitance (sets the self-resonant frequency)") } };
  case CapacitorItemType:
    return {
      { QStringLiteral("ESR"), "esr", QStringLiteral("Ω"), QStringLiteral("0.01"),
        tr("Equivalent series resistance") },
      { QStringLiteral("ESL"), "esl", QStringLiteral("H"), QStringLiteral("1e-09"),
        tr("Equivalent series inductance") },
      { QStringLiteral("Rp"), "rp", QStringLiteral("Ω"), QStringLiteral("0"),
        tr("Leakage (insulation) resistance; 0 = none") } };
  default:
    return {};
  }
}


QString SchematicScene::valueUnit(SchematicScene::SupportedItemType type)
{
  switch(type)
  {
  case ResistorItemType:
  case CCVSItemType:
    return QStringLiteral("Ω");
  case ConductanceItemType:
  case VCCSItemType:
    return QStringLiteral("S");
  case InductorItemType:
  case MutualInductanceItemType:
    return QStringLiteral("H");
  case CapacitorItemType:
    return QStringLiteral("F");
  case VoltageSourceItemType:
  case SingleVoltageSourceItemType:
    return QStringLiteral("V");
  case CurrentSourceItemType:
    return QStringLiteral("A");
  default:
    return QString();
  }
}


void SchematicScene::rotatePlacedOrSelected()
{
  if(isPlacingItem()) {
    if(item_)
      item_->rotate();
  } else {
    rotateSelectedItems();
  }
}


void SchematicScene::mirrorPlacedOrSelected(bool vertical)
{
  // a vertical flip is a horizontal one turned by 180 degrees
  if(isPlacingItem()) {
    if(item_) {
      item_->mirror();
      if(vertical) {
        item_->rotate();
        item_->rotate();
      }
    }
    return;
  }

  QList<QGraphicsItem*> items = selectedItems();
  if(items.isEmpty())
    return;
  undoRedoStack_->beginMacro(vertical ? tr("Flip vertically")
                                      : tr("Flip horizontally"));
  undoRedoStack_->push(new MirrorItems(items));
  if(vertical) {
    undoRedoStack_->push(new RotateItems(items));
    undoRedoStack_->push(new RotateItems(items));
  }
  undoRedoStack_->endMacro();
}


void SchematicScene::mirrorSelectedItems()
{
  QList<QGraphicsItem*> items = selectedItems();
  if(items.isEmpty())
    return;

  MirrorItems* command = new MirrorItems(items);
  undoRedoStack_->push(command);
}


void SchematicScene::binSelectedItems()
{
  QList<QGraphicsItem*> items = selectedItems();
  if(items.isEmpty())
    return;

  RemoveItems* command = new RemoveItems(this, items);
  undoRedoStack_->push(command);
}


void SchematicScene::bringToFrontSelectedItem()
{
  QGraphicsItem* item = itemAt(lastMousePressPos_, QTransform());
  if(!item)
    return;

  clearSelection();
  item->setSelected(true);
  BringToFrontItem* command = new BringToFrontItem(item);
  undoRedoStack_->push(command);
}


void SchematicScene::sendToBackSelectedItem()
{
  QGraphicsItem* item = itemAt(lastMousePressPos_, QTransform());
  if(!item)
    return;

  clearSelection();
  item->setSelected(true);
  SendToBackItem* command = new SendToBackItem(item);
  undoRedoStack_->push(command);
}


void SchematicScene::clearSchematicScene()
{
  QList<QGraphicsItem*> itemList = items();
  if(itemList.isEmpty())
    return;

  undoRedoStack_->beginMacro("Clear schematic scene");
  RemoveItems* command = new RemoveItems(this, itemList);
  undoRedoStack_->push(command);
  undoRedoStack_->endMacro();
}


QVector<int> SchematicScene::ports() {
  QVector<int> vector;
  foreach(Item* item, portList_)
    vector += static_cast<Component*>(item)->nodes();
  
  return vector;
}


void SchematicScene::assignNodes(int seed)
{
  resetStatus();
  updateWireTaps();

  foreach(Item* item, groundList_)
    static_cast<Component*>(item)->propagate(SchematicScene::Ground);
  foreach(Item* item, wireList_)
    seed = static_cast<Wire*>(item)->propagate(seed);
  foreach(Item* item, standardList_)
    seed = static_cast<Component*>(item)->propagate(seed);
  foreach(Item* item, portList_)
    seed = static_cast<Component*>(item)->propagate(seed);
  foreach(Item* item, userDefList_)
    seed = static_cast<Component*>(item)->propagate(seed);
  if(out_)
    seed = static_cast<Component*>(out_)->propagate(seed);

  numberNetLabels();

  // sub-circuits propagation
  foreach(Item* item, userDefList_) {
    QPointer<qsapecng::SchematicScene> rep =
      item->data(101).value< QPointer<qsapecng::SchematicScene> >();

    rep->assignNodes(seed);
  }
}


void SchematicScene::numberNetLabels()
{
  // only nets that join two or more pins (the Out tag counts) get a
  // number, 1, 2, 3 ... in the order of the parts; a lone wire or a pin
  // left open is not worth a label. Display only: the solver keeps its
  // own numbering
  QHash<int, int> pins;
  QList<int> order;
  QList<Item*> parts;
  parts << standardList_ << portList_ << userDefList_;
  if(out_)
    parts << out_;
  foreach(Item* item, parts)
    foreach(int net, static_cast<Component*>(item)->nodes()) {
      if(!pins.contains(net))
        order.append(net);
      ++pins[net];
    }

  QHash<int, int> shown;
  foreach(int net, order)
    if(net != Ground && pins.value(net) >= 2)
      shown.insert(net, shown.size() + 1);

  // one label per net: on a wire end if the net has wires (a pin's
  // label can sit on the part's lead), the top-left one
  // preference: a wire corner or junction (2), a wire end on a pin (1),
  // a pin (0); then the top-left one
  QHash<int, GraphicsNode*> carrier;
  QHash<int, int> rank;
  foreach(QGraphicsItem* item, items()) {
    GraphicsNode* node = dynamic_cast<GraphicsNode*>(item);
    if(!node || !node->parentItem() || !node->isVisible())
      continue;
    node->hideNodeValue();
    const int net = node->constNode()->value();
    if(!shown.contains(net))
      continue;
    int r = 0;
    if(dynamic_cast<Wire*>(node->parentItem())) {
      r = 2;
      foreach(GraphicsNode* other, node->itemSet())
        if(other->parentItem() && !dynamic_cast<Wire*>(other->parentItem()))
          r = 1;
    }
    GraphicsNode* best = carrier.value(net);
    bool better = !best || r > rank.value(net);
    if(best && r == rank.value(net)) {
      const QPointF a = node->scenePos(), b = best->scenePos();
      better = a.y() < b.y() || (a.y() == b.y() && a.x() < b.x());
    }
    if(better) {
      carrier.insert(net, node);
      rank.insert(net, r);
    }
  }
  for(auto i = carrier.constBegin(); i != carrier.constEnd(); ++i) {
    i.value()->showNodeValue();
    i.value()->setDisplayedValue(shown.value(i.key()));
  }
}


void SchematicScene::resetNodes()
{
  foreach(Item* item, groundList_)
    static_cast<Component*>(item)->invalidate();
  foreach(Item* item, wireList_)
    static_cast<Wire*>(item)->invalidate();
  foreach(Item* item, standardList_)
    static_cast<Component*>(item)->invalidate();
  foreach(Item* item, portList_)
    static_cast<Component*>(item)->invalidate();
  foreach(Item* item, userDefList_)
    static_cast<Component*>(item)->invalidate();
  if(out_)
    static_cast<Component*>(out_)->invalidate();

  // sub-circuits reset
  foreach(Item* item, userDefList_)
    item->data(101)
      .value< QPointer<qsapecng::SchematicScene> >()->resetNodes();
}


void SchematicScene::setGridVisible(bool visible)
{
  gridVisible_ = visible;
  update();

  foreach(QGraphicsView* view, views())
    view->resetCachedContent();
}


void SchematicScene::resetStatus()
{
  resetNodes();
  clearSelection();
  clearFocus();

  activeItem_ = NilItemType;
  delete item_;
  item_ = 0;

  foreach(QGraphicsView* view, views())
    view->unsetCursor();

  userDefSessionRequested_ = false;
  userDefMD5_ = QByteArray();
  userDefSize_ = 0;

  endWireChainMacro();
  wireSessionRequested_ = false;
  delete preWireItem_;
  delete postWireItem_;
  preWireItem_ = 0;
  postWireItem_ = 0;
  if(wireInProgress_) {
    wireInProgress_->setVisible(true);
    wireInProgress_ = 0;
  }

  emit placementChanged();
}


void SchematicScene::drawBackground(QPainter* painter, const QRectF& rect)
{
  // The schematic is always drawn on a white "sheet" (components are black),
  // independently of the light/dark application palette.
  painter->fillRect(rect, theme::sheetBackground());

  if(gridVisible_) {
    painter->save();

//     painter->setRenderHints(
//       QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter->setRenderHints(QPainter::SmoothPixmapTransform);
    painter->setPen(QPen(theme::sheetGrid(), 0));
    painter->setBrush(Qt::NoBrush);

    // zoomed out, show every 5th / 25th... dot so the grid stays readable
    const qreal scale = qMax(qreal(1e-6), painter->transform().m11() != 0.
      ? qAbs(painter->transform().m11()) : qAbs(painter->transform().m12()));
    int step = GridStep;
    while(step * scale < 6.)
      step *= 5;

    const int left = int(std::floor(rect.left() / step)) * step;
    const int top = int(std::floor(rect.top() / step)) * step;
    const qreal right = rect.right();
    const qreal bottom = rect.bottom();

    for(int x = left; x <= right; x += step)
      for(int y = top; y <= bottom; y += step)
        painter->drawPoint(x, y);

    painter->restore();
  }
}


void SchematicScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
  lastMousePressPos_ = event->scenePos();
  resetNodes();

  if(hasActiveItem() || hasUserDefReq()) {
    switch(event->button())
    {
    case Qt::RightButton:
      item_->rotate();
      event->accept();
      return;
    case Qt::LeftButton:
    {
      QUndoCommand* addCommand = 0;
      
      if(hasUserDefReq()) {
        addCommand = new AddUserDefItem(this,
          userDefMD5_,
          userDefSize_,
          userDefMap_.value(userDefMD5_),
          item_->pos());
      } else {
          addCommand = new AddSupportedItem(this, activeItem_, item_->pos());
      }

      undoRedoStack_->beginMacro(addCommand->text());
      undoRedoStack_->push(addCommand);

      uint rotFact = item_->angle() / 90;
      for(uint i = 0; i < rotFact; ++i) {
        RotateItems* rotateCommand = 0;

        if(hasUserDefReq()) {
          rotateCommand = new RotateItems(
            static_cast<AddUserDefItem*>(addCommand)->item());
        } else {
          rotateCommand = new RotateItems(
            static_cast<AddSupportedItem*>(addCommand)->item());
        }

        undoRedoStack_->push(rotateCommand);
      }
      if(item_->mirrored()) {
        QGraphicsItem* placed = hasUserDefReq()
          ? static_cast<QGraphicsItem*>(
              static_cast<AddUserDefItem*>(addCommand)->item())
          : static_cast<QGraphicsItem*>(
              static_cast<AddSupportedItem*>(addCommand)->item());
        undoRedoStack_->push(new MirrorItems(placed));
      }
      undoRedoStack_->endMacro();

      event->accept();
      return;
    }
    default:
      break;
    }
  }

  if(hasPendingWire()) {
    switch(event->button())
    {
    case Qt::RightButton:
    {
      // flip the bend (horizontal or vertical first)
      if(hasValidFromPoint_)
        flipWireBend();
      event->accept();
      return;
    }
    case Qt::LeftButton:
    {
      const QPointF point = closestGridPoint(event->scenePos());

      if(!hasValidFromPoint_) {
        // first click: start a chain of segments
        fromPoint_ = point;
        bendDecided_ = false;
        preWireItem_->setVisible(true);
        hasValidFromPoint_ = true;
      } else if(point == fromPoint_) {
        // clicking the last corner again ends the chain
        finishWireChain();
      } else {
        // a pin or a wire under the click ends the chain, otherwise the
        // click is a corner and drawing goes on from there (KiCad, Eagle)
        const bool ends = wireInProgress_ || isConnectionPoint(point);
        const bool modifying = (wireInProgress_ != 0);
        setPrePostWireLine(event->scenePos());
        if(!modifying && !chainMacroOpen_) {
          // all segments of the wire are one undo step
          undoRedoStack_->beginMacro(tr("Add wire"));
          chainMacroOpen_ = true;
        }
        addWires();
        if(ends || modifying) {
          finishWireChain();
          if(modifying)
            resetStatus();
        } else {
          fromPoint_ = point;
          bendDecided_ = false;
        }
      }
      if(preWireItem_) {
        preWireItem_->setLine(QLineF(fromPoint_, fromPoint_));
        postWireItem_->setLine(QLineF(fromPoint_, fromPoint_));
        postWireItem_->setVisible(false);
      }
      event->accept();
      return;
    }
    default:
      break;
    }
  }

  if(!hasActiveItem() && !hasUserDefReq() && !hasPendingWire()
      && selectedItems().isEmpty() && itemAt(event->scenePos(), QTransform())) {
    clearSelection();
    itemAt(event->scenePos(), QTransform())->setSelected(true);
  }

  QGraphicsScene::mousePressEvent(event);

  selectWireRunAt(event);
}


void SchematicScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
  QGraphicsScene::mouseReleaseEvent(event);
  if(drag_.active && event->button() == Qt::LeftButton)
    endDrag(true);
  // a click without a move ends with only the clicked item selected
  selectWireRunAt(event);
}


void SchematicScene::selectWireRunAt(QGraphicsSceneMouseEvent* event)
{
  // a bent wire is one object: clicking a segment (de)selects them all
  if(hasActiveItem() || hasUserDefReq() || hasPendingWire()
      || event->button() != Qt::LeftButton)
    return;

  QGraphicsItem* hit = itemAt(event->scenePos(), QTransform());
  while(hit && !dynamic_cast<Wire*>(hit))
    hit = hit->parentItem();
  if(Wire* wire = dynamic_cast<Wire*>(hit)) {
    const bool selected = wire->isSelected();
    foreach(Wire* segment, wireRun(wire))
      segment->setSelected(selected);
  }
}


void SchematicScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
  lastMousePos_ = event->scenePos();

  if(hasActiveItem() || hasUserDefReq()) {
    item_->setPos(placementPos(event->scenePos()));
    event->accept();
    return;
  }

  if(hasPendingWire()) {
    if(hasValidFromPoint_)
      setPrePostWireLine(event->scenePos());
    event->accept();
    return;
  }

  QGraphicsScene::mouseMoveEvent(event);
}


void SchematicScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
  if(hasPendingWire()) {
    // the first click of the pair already placed the segments
    if(event->button() == Qt::LeftButton)
      finishWireChain();
    event->accept();
    return;
  }

  if(!hasActiveItem() && !hasUserDefReq() && !hasPendingWire()
      && itemAt(event->scenePos(), QTransform())) {
    if(SchematicScene::itemType(itemAt(event->scenePos(), QTransform()))
        == SchematicScene::UserDefItemType)
    {
      QGraphicsItem* item = itemAt(event->scenePos(), QTransform());
      QPointer<qsapecng::SchematicScene> rep =
        item->data(101).value< QPointer<qsapecng::SchematicScene> >();

      emit(showUserDef(*rep));
      event->accept();
    }
  }

  if(SchematicScene::itemType(itemAt(event->scenePos(), QTransform()))
      != SchematicScene::UserDefItemType)
    QGraphicsScene::mousePressEvent(event);
}


void SchematicScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
  if(!hasActiveItem() && !hasUserDefReq() && !hasPendingWire() && contextMenu_)
    contextMenu_->exec(event->screenPos());
}


void SchematicScene::keyPressEvent(QKeyEvent* event)
{
  if(event->modifiers().testFlag(Qt::NoModifier)) {
    int key = event->key();

    switch(key)
    {
    case Qt::Key_Left:
      {
        moveSelectedItems(QPointF(-GridStep, 0));
        event->accept();
        return;
      }
    case Qt::Key_Up:
      {
        moveSelectedItems(QPointF(0, -GridStep));
        event->accept();
        return;
      }
    case Qt::Key_Right:
      {
        moveSelectedItems(QPointF(GridStep, 0));
        event->accept();
        return;
      }
    case Qt::Key_Down:
      {
        moveSelectedItems(QPointF(0, GridStep));
        event->accept();
        return;
      }
    case Qt::Key_Escape:
      {
        // Esc while drawing drops the unfinished segment and keeps the
        // wire tool; a second Esc leaves the tool
        if(hasPendingWire() && hasValidFromPoint_ && !wireInProgress_)
          finishWireChain();
        else
          resetStatus();
        event->accept();
        return;
      }
    case Qt::Key_Slash:
    case Qt::Key_Space:
      {
        if(hasPendingWire() && hasValidFromPoint_) {
          flipWireBend();
          event->accept();
          return;
        }
        break;
      }
    default:
      break;
    }

  }

  return QGraphicsScene::keyPressEvent(event);
}


bool SchematicScene::event(QEvent* event)
{
  // enter event
  if(event->type() == QEvent::Enter)
  {
    if(hasActiveItem() || hasUserDefReq()) {
      if(item_)
        delete item_;

      item_ = SchematicScene::itemByType(activeItem_);
      
      if(hasUserDefReq()) {
        static_cast<Component*>(item_)->setPath(userDefPath(userDefSize_));
        static_cast<Component*>(item_)->addNodes(userDefNodes(userDefSize_));
      }
      
      addItem(item_);
      event->accept();
      return true;
    }
  }

  // leave event
  if(event->type() == QEvent::Leave)
  {
    if(hasActiveItem() || hasUserDefReq()) {
      removeItem(item_);
      delete item_;
      item_ = 0;
      event->accept();
      return true;
    }
  }

  return QGraphicsScene::event(event);
}


void SchematicScene::dragEnterEvent(QGraphicsSceneDragDropEvent* event)
{
  if(hasActiveItem() || hasUserDefReq() || hasPendingWire()) {
    event->ignore();
    return;
  }

  if(event->mimeData()->hasFormat("application/qsapecng-itemtype")) {
    SupportedItemType type = (SupportedItemType) event->mimeData()->data(
      "application/qsapecng-itemtype").toInt();

    item_ = itemByType(type);
    if(item_) {
      resetNodes();

      QPointF pos = placementPos(event->scenePos());

      addItem(item_);
      item_->setPos(pos);
      clearSelection();
      item_->setSelected(true);
      event->acceptProposedAction();
    }
  } else {
    event->ignore();
  }
}


void SchematicScene::dragMoveEvent(QGraphicsSceneDragDropEvent* event)
{
  if(item_) {
    QPointF pos = placementPos(event->scenePos());
    item_->setPos(pos);
    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}


void SchematicScene::dragLeaveEvent(QGraphicsSceneDragDropEvent* event)
{
  if(item_) {
    removeItem(item_);
    delete item_;
    item_ = 0;
    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}


void SchematicScene::dropEvent(QGraphicsSceneDragDropEvent* event)
{
  if(item_) {
    SupportedItemType type = (SupportedItemType) item_->data(0).toInt();
    qreal xoffset = item_->boundingRect().width() / 2;
    qreal yoffset = item_->boundingRect().height() / 2;

    removeItem(item_);
    delete item_;
    item_ = 0;

    AddSupportedItem* command = new AddSupportedItem(this, type,
      event->scenePos() - QPointF(xoffset, yoffset));
    undoRedoStack_->push(command);

    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}


void SchematicScene::init()
{
  out_ = 0;
  contextMenu_ = 0;
  validNode_ = false;
  gridVisible_ = true;
  undoRedoStack_ = new QUndoStack;
  lastMousePressPos_ = QPointF(0, 0);
  setItemIndexMethod(NoIndex);
  activeItem_ = NilItemType;
  item_ = 0;

  userDefSessionRequested_ = false;
  userDefMD5_ = QByteArray();
  userDefSize_ = 0;

  wireSessionRequested_ = false;
  wireInProgress_ = 0;
  preWireItem_ = 0;
  postWireItem_ = 0;
  wirePen_ = QPen(
      Qt::SolidPattern,
      1,
      Qt::DashLine,
      Qt::RoundCap,
      Qt::RoundJoin
    );

  connect(undoRedoStack_, SIGNAL(indexChanged(int)),
    this, SLOT(resetNodes()));
}


void SchematicScene::setupProperties()
{
  groupManager_ = new QtGroupPropertyManager(this);

  boolManager_ = new QtBoolPropertyManager(this);
  stringManager_ = new QtStringPropertyManager(this);

  connect(boolManager_, SIGNAL(propertyChanged(QtProperty*)),
    this, SIGNAL(propertyChanged()));
  connect(stringManager_, SIGNAL(propertyChanged(QtProperty*)),
    this, SIGNAL(propertyChanged()));


  properties_ = groupManager_->addProperty(tr("Circuit"));

  itemCntMap_[CapacitorItemType] = 0;
  typeRootMap_[CapacitorItemType] =
    groupManager_->addProperty(itemNameByType(CapacitorItemType));
//   properties_->addSubProperty(typeRootMap_[CapacitorItemType]);

  itemCntMap_[CCCSItemType] = 0;
  typeRootMap_[CCCSItemType] =
    groupManager_->addProperty(itemNameByType(CCCSItemType));
//   properties_->addSubProperty(typeRootMap_[CCCSItemType]);

  itemCntMap_[CCVSItemType] = 0;
  typeRootMap_[CCVSItemType] =
    groupManager_->addProperty(itemNameByType(CCVSItemType));
//   properties_->addSubProperty(typeRootMap_[CCVSItemType]);

  itemCntMap_[ConductanceItemType] = 0;
  typeRootMap_[ConductanceItemType] =
    groupManager_->addProperty(itemNameByType(ConductanceItemType));
//   properties_->addSubProperty(typeRootMap_[ConductanceItemType]);

  itemCntMap_[CurrentSourceItemType] = 0;
  typeRootMap_[CurrentSourceItemType] =
    groupManager_->addProperty(itemNameByType(CurrentSourceItemType));
//   properties_->addSubProperty(typeRootMap_[CurrentSourceItemType]);

  itemCntMap_[InductorItemType] = 0;
  typeRootMap_[InductorItemType] =
    groupManager_->addProperty(itemNameByType(InductorItemType));
//   properties_->addSubProperty(typeRootMap_[InductorItemType]);

  itemCntMap_[OpAmplItemType] = 0;
  typeRootMap_[OpAmplItemType] =
    groupManager_->addProperty(itemNameByType(OpAmplItemType));
//   properties_->addSubProperty(typeRootMap_[OpAmplItemType]);

  itemCntMap_[ResistorItemType] = 0;
  typeRootMap_[ResistorItemType] =
    groupManager_->addProperty(itemNameByType(ResistorItemType));
//   properties_->addSubProperty(typeRootMap_[ResistorItemType]);

  itemCntMap_[VCCSItemType] = 0;
  typeRootMap_[VCCSItemType] =
    groupManager_->addProperty(itemNameByType(VCCSItemType));
//   properties_->addSubProperty(typeRootMap_[VCCSItemType]);

  itemCntMap_[VCVSItemType] = 0;
  typeRootMap_[VCVSItemType] =
    groupManager_->addProperty(itemNameByType(VCVSItemType));
//   properties_->addSubProperty(typeRootMap_[VCVSItemType]);

  itemCntMap_[SingleVoltageSourceItemType] = 0;
  typeRootMap_[SingleVoltageSourceItemType] =
    groupManager_->addProperty(itemNameByType(SingleVoltageSourceItemType));

  itemCntMap_[VoltageSourceItemType] = 0;
  typeRootMap_[VoltageSourceItemType] =
    groupManager_->addProperty(itemNameByType(VoltageSourceItemType));
//   properties_->addSubProperty(typeRootMap_[VoltageSourceItemType]);

  itemCntMap_[TransformerItemType] = 0;
  typeRootMap_[TransformerItemType] =
    groupManager_->addProperty(itemNameByType(TransformerItemType));
//   properties_->addSubProperty(typeRootMap_[TransformerItemType]);

  itemCntMap_[MutualInductanceItemType] = 0;
  typeRootMap_[MutualInductanceItemType] =
    groupManager_->addProperty(itemNameByType(MutualInductanceItemType));
//   properties_->addSubProperty(typeRootMap_[MutualInductanceItemType]);

  itemCntMap_[UserDefItemType] = 0;
  typeRootMap_[UserDefItemType] =
    groupManager_->addProperty(itemNameByType(UserDefItemType));
//   properties_->addSubProperty(typeRootMap_[UserDefItemType]);
}


void SchematicScene::setPrePostWireLine(QPointF toPoint)
{
  QPointF realToPoint = closestGridPoint(toPoint);

  // the bend follows the first move away from the corner: moving
  // sideways first gives horizontal-then-vertical, and vice versa
  if(realToPoint == fromPoint_) {
    bendDecided_ = false;
  } else if(!bendDecided_) {
    const QPointF d = toPoint - fromPoint_;
    horizontalFirst_ = qAbs(d.x()) >= qAbs(d.y());
    bendDecided_ = true;
  }

  if(fromPoint_.x() == realToPoint.x() || fromPoint_.y() == realToPoint.y()) {
    preWireItem_->setLine(QLineF(fromPoint_, realToPoint));
    postWireItem_->setVisible(false);
  } else {
    QPointF joint(
        (horizontalFirst_ ? realToPoint.x() : fromPoint_.x()),
        (horizontalFirst_ ? fromPoint_.y() : realToPoint.y())
      );

    preWireItem_->setLine(QLineF(fromPoint_, joint));
    postWireItem_->setLine(QLineF(joint, realToPoint));
    postWireItem_->setVisible(true);
  }
}


bool SchematicScene::isConnectionPoint(const QPointF& point) const
{
  const QRectF probe(point - QPointF(1.5, 1.5), QSizeF(3, 3));
  foreach(QGraphicsItem* item, items(probe)) {
    if(!item->isVisible() || item == preWireItem_ || item == postWireItem_)
      continue;
    if(dynamic_cast<GraphicsNode*>(item))
      return true;      // a pin
    if(Wire* wire = dynamic_cast<Wire*>(item))
      if(wire != wireInProgress_)
        return true;    // an end or the middle of a wire (T-junction)
  }
  return false;
}


void SchematicScene::endWireChainMacro()
{
  if(chainMacroOpen_) {
    chainMacroOpen_ = false;
    undoRedoStack_->endMacro();
  }
}


QList<Wire*> SchematicScene::wireRun(Wire* start) const
{
  QList<Wire*> run;
  if(!start)
    return run;

  QList<Wire*> todo = { start };
  while(!todo.isEmpty()) {
    Wire* wire = todo.takeLast();
    if(run.contains(wire))
      continue;
    run.append(wire);

    const QPointF ends[] = { wire->mapToScene(wire->fromPoint()),
                             wire->mapToScene(wire->toPoint()) };
    for(const QPointF& end : ends)
    {
      // what meets at this end
      QList<GraphicsNode*> here;
      const QRectF probe(end - QPointF(0.5, 0.5), QSizeF(1, 1));
      foreach(QGraphicsItem* item, items(probe))
        if(GraphicsNode* node = dynamic_cast<GraphicsNode*>(item))
          if(QLineF(node->scenePos(), end).length() < 0.5)
            here.append(node);

      int branches = 0;
      foreach(GraphicsNode* node, here)
        branches += node->branches();
      if(branches != 2)
        continue;   // open end, junction

      // a plain bend: continue along the other wire
      foreach(GraphicsNode* node, here)
        if(Wire* next = dynamic_cast<Wire*>(node->owner()))
          if(next != wire && !run.contains(next))
            todo.append(next);
    }
  }
  return run;
}


void SchematicScene::finishWireChain()
{
  endWireChainMacro();
  hasValidFromPoint_ = false;
  bendDecided_ = false;
  if(preWireItem_) {
    preWireItem_->setVisible(false);
    postWireItem_->setVisible(false);
    preWireItem_->setLine(QLineF(fromPoint_, fromPoint_));
    postWireItem_->setLine(QLineF(fromPoint_, fromPoint_));
  }
}


void SchematicScene::flipWireBend()
{
  horizontalFirst_ = !horizontalFirst_;
  bendDecided_ = true;
  setPrePostWireLine(lastMousePos_);
}


void SchematicScene::addWires()
{
  QPointF pos;

  // nothing to add for a click on the starting point
  if(preWireItem_->line().length() == 0
      && (!postWireItem_->isVisible() || postWireItem_->line().length() == 0))
    return;

  if(wireInProgress_) {
    undoRedoStack_->beginMacro(tr("Modify wire"));
    undoRedoStack_->push(new RemoveItems(this, wireInProgress_));
    wireInProgress_->setVisible(true);
  }

  if(preWireItem_->line().y1() == preWireItem_->line().y2())
  {
    if(preWireItem_->line().x1() > preWireItem_->line().x2())
      pos = preWireItem_->line().p2();
    else
      pos = preWireItem_->line().p1();
  } else {
    if(preWireItem_->line().y1() > preWireItem_->line().y2())
      pos = preWireItem_->line().p2();
    else
      pos = preWireItem_->line().p1();
  }

  AddWire* preWireCommand = new AddWire(this, pos,
    preWireItem_->line(), connectedWire_);
  undoRedoStack_->push(preWireCommand);

  if(postWireItem_->isVisible()) {
    if(postWireItem_->line().y1() == postWireItem_->line().y2())
    {
      if(postWireItem_->line().x1() > postWireItem_->line().x2())
        pos = postWireItem_->line().p2();
      else
        pos = postWireItem_->line().p1();
    } else {
      if(postWireItem_->line().y1() > postWireItem_->line().y2())
        pos = postWireItem_->line().p2();
      else
        pos = postWireItem_->line().p1();
    }

    AddWire* postWireCommand = new AddWire(this, pos,
      postWireItem_->line(), connectedWire_);
    undoRedoStack_->push(postWireCommand);
  }

  if(wireInProgress_) {
    undoRedoStack_->endMacro();
    wireInProgress_ = 0;
  }
}


}
