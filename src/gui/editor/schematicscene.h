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


#ifndef SCHEMATICSCENE_H
#define SCHEMATICSCENE_H


#include <QGraphicsScene>

#include <QList>
#include <QRect>
#include <QHash>
#include <QPointF>
#include <QVariant>
#include <QMetaType>
#include <QPointer>

#include "gui/qtsolutions/qtpropertyeditor/qtpropertybrowser.h"

#include <memory>


class QMenu;
class QEvent;
class QPainter;
class QKeyEvent;
class QUndoStack;
class QUndoCommand;
class QGraphicsItem;
class QGraphicsSceneMouseEvent;
class QGraphicsSceneDragDropEvent;
class QGraphicsSceneContextMenuEvent;

class QtBoolPropertyManager;
class QtGroupPropertyManager;
class QtStringPropertyManager;


class QtProperty;


namespace qsapecng
{


class Component;
class Item;
class Wire;


class SchematicScene: public QGraphicsScene
{

  Q_OBJECT

public:
  enum { Ground = 0 };
  enum { GridStep = 10 };

  enum SupportedItemType {
    NilItemType = 0,
    
    UserDefItemType,

    CapacitorItemType,
    CCCSItemType,
    CCVSItemType,
    ConductanceItemType,
    CurrentSourceItemType,
    InductorItemType,
    OpAmplItemType,
    ResistorItemType,
    VCCSItemType,
    VCVSItemType,
    VoltageSourceItemType,
    TransformerItemType,
    MutualInductanceItemType,

    VoltmeterItemType,
    AmmeterItemType,

    GroundItemType,
    PortItemType,
    OutItemType,
    WireItemType,

    LabelItemType,
  };

  static QPainterPath nilPath();

  static QPainterPath groundPath();
  static QPainterPath portPath();
  static QPainterPath outPath();
  static QPainterPath voltmeterPath();
  static QPainterPath ammeterPath();

  static QPainterPath capacitorPath();
  static QPainterPath cccsPath();
  static QPainterPath ccvsPath();
  static QPainterPath conductancePath();
  static QPainterPath currentSourcePath();
  static QPainterPath inductorPath();
  static QPainterPath opAmplPath();
  static QPainterPath resistorPath();
  static QPainterPath vccsPath();
  static QPainterPath vcvsPath();
  static QPainterPath voltageSourcePath();
  static QPainterPath transformerPath();
  static QPainterPath mutualInductancePath();

  static QtProperty* itemProperties(QGraphicsItem* item);
  static SchematicScene::SupportedItemType itemType(QGraphicsItem* item);
  static Item* itemByType(SupportedItemType type);
  static QString itemNameByType(SchematicScene::SupportedItemType type);
  static QChar itemIdByType(SchematicScene::SupportedItemType type);
  // unit of the value of a component type ("F", "Ω"...; empty: none)
  static QString valueUnit(SchematicScene::SupportedItemType type);

  static QPainterPath userDefPath(uint ports);
  static QList<QPointF> userDefNodes(uint ports);

public:
  SchematicScene(QObject* parent = 0);
  ~SchematicScene();

  QPointF closestGridPoint(const QPointF& pos) const;
  inline bool gridVisible() const { return gridVisible_; }
  inline bool isGridPoint(QPointF point) const
    { return (point == closestGridPoint(point)); }

  void setActiveItem(SupportedItemType item);
  inline SupportedItemType activeItem() const { return activeItem_; }
  QList<Item*> activeItems() const;

  QByteArray registerUserDef(const SchematicScene& scene);
  std::string queryUserDef(QByteArray md5);
  void setUserDefRequest();

  void addItems(QList<QGraphicsItem*> items);
  void addItems(QGraphicsItem* item);
  void addSupportedItem(QGraphicsItem* gItem, bool init = true);
  void removeSupportedItem(QGraphicsItem* gItem);
  void clearSupportedItem(QGraphicsItem* gItem);

  void setWireSessionRequest(bool connectedWire);
  void joinWires(Wire* w1, Wire* w2);
  void modifyWire(Wire* wire);

  void createLabel();

  void moveSelectedItems(QPointF pos);
  void cutSelectedItems();
  void copySelectedItems();
  void pasteItems();
  void rotateSelectedItems();
  // R key / Rotate action: rotates the item being placed if there is one,
  // otherwise the selected items
  void rotatePlacedOrSelected();
  // redraw resistors/conductances after the resistor symbol style changed
  void updateResistorSymbols();
  // rebuild every label (e.g. after the value display setting changed)
  void refreshComponentLabels();
  // open the parameters dialog of an item (double-click); undoable
  void editItemProperties(QGraphicsItem* item);
  inline bool isPlacingItem() const
    { return hasActiveItem() || hasUserDefReq(); }
  void mirrorSelectedItems();
  void binSelectedItems();
  void bringToFrontSelectedItem();
  void sendToBackSelectedItem();
  void clearSchematicScene();

  inline void setContextMenu(QMenu* menu) { contextMenu_ = menu; }
  inline QMenu* contextMenu() const { return contextMenu_; }

  inline QUndoStack* undoRedoStack() const { return undoRedoStack_; }

  inline QtProperty* properties() const { return properties_; }

  QVector<int> ports();
  inline int size() { return portList_.size(); }
  void assignNodes(int seed = SchematicScene::Ground + 1);

public slots:
  // recompute wire taps (T-junction connections) for all wires
  void updateWireTaps();
  void resetNodes();
  void setGridVisible(bool visible = true);
  void resetStatus();

signals:
  void showUserDef(SchematicScene& scene);
  void propertyChanged();
  // placing a new item started or ended (see isPlacingItem())
  void placementChanged();

protected:
  void drawBackground(QPainter* painter, const QRectF& rect);
  void mousePressEvent(QGraphicsSceneMouseEvent* event);
  void mouseReleaseEvent(QGraphicsSceneMouseEvent* event);
  void mouseMoveEvent(QGraphicsSceneMouseEvent* event);
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event);
  void contextMenuEvent(QGraphicsSceneContextMenuEvent* event);
  void keyPressEvent(QKeyEvent* event);
  bool event(QEvent* event);

  void dragEnterEvent(QGraphicsSceneDragDropEvent* event);
  void dragMoveEvent(QGraphicsSceneDragDropEvent* event);
  void dragLeaveEvent(QGraphicsSceneDragDropEvent* event);
  void dropEvent(QGraphicsSceneDragDropEvent* event);

private:
  void init();
  void setupProperties();

  inline bool hasActiveItem() const { return (activeItem_ != NilItemType); }
  inline bool hasUserDefReq() const { return userDefSessionRequested_; }
  inline bool hasPendingWire() const { return wireSessionRequested_; }
  void setPrePostWireLine(QPointF toPoint);
  void addWires();
  // wire drawing: a pin or a wire at this point ends the chain of segments
  bool isConnectionPoint(const QPointF& point) const;
  // ends the chain being drawn; the wire tool stays active
  void finishWireChain();
  void flipWireBend();
  // the segments of a bent wire: all wires joined end to end through plain
  // bends (two branches), up to junctions and pins
  QList<Wire*> wireRun(Wire* wire) const;
  void selectWireRunAt(QGraphicsSceneMouseEvent* event);
  void endWireChainMacro();

private:
  bool gridVisible_;
  QMenu* contextMenu_;
  QPointF lastMousePressPos_;
  QPointF lastMousePos_;
  bool validNode_;
  
  SupportedItemType activeItem_;
  Item* item_;

  bool wireSessionRequested_;
  bool horizontalFirst_;
  bool chainMacroOpen_ = false;   // one undo step per drawn wire
  bool bendDecided_ = false;   // set by the first move or by flipping
  bool hasValidFromPoint_;
  bool connectedWire_;
  QPointF fromPoint_;
  QGraphicsLineItem* preWireItem_;
  QGraphicsLineItem* postWireItem_;
  Wire* wireInProgress_;
  QPen wirePen_;

  Item* out_;
  QList<Item*> outList_;
  QList<Item*> portList_;
  QList<Item*> groundList_;
  QList<Item*> standardList_;
  QList<Item*> wireList_;
  QList<Item*> labelList_;
  QList<Item*> userDefList_;
  
  int userDefSize_;
  QByteArray userDefMD5_;
  QHash<QByteArray, std::string> userDefMap_;
  bool userDefSessionRequested_;

  QUndoStack* undoRedoStack_;

  QtBoolPropertyManager* boolManager_;
  QtGroupPropertyManager* groupManager_;
  QtStringPropertyManager* stringManager_;

  QtProperty* properties_;
  bool wireTapsPending_ = false;
  QString nextFreeName(SupportedItemType type);
  QHash<SupportedItemType, unsigned int> itemCntMap_;
  QHash<SupportedItemType, QtProperty*> typeRootMap_;

};


}


Q_DECLARE_METATYPE(QPointer<qsapecng::SchematicScene>);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
Q_DECLARE_METATYPE(QtProperty*);
#endif


#endif // SCHEMATICSCENE_H
