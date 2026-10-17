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


#include "gui/editor/propertytextitem.hpp"
#include "gui/units.h"

#include "gui/qtsolutions/qtpropertyeditor/qtpropertymanager.h"

#include <QTextDocument>
#include <QTextOption>



namespace qsapecng
{


PropertyTextItem::PropertyTextItem(QGraphicsItem* parent)
  : QGraphicsTextItem(parent), property_(0)
{
  init();
}


PropertyTextItem::PropertyTextItem(const QString& text, QGraphicsItem* parent)
  : QGraphicsTextItem(text, parent), property_(0)
{
  init();
}


void PropertyTextItem::init()
{
  setFont(theme::schematicFont(10));
  // the sheet follows the theme: don't inherit the widget text colour
  setDefaultTextColor(theme::sheetForeground());
  setTextInteractionFlags(Qt::NoTextInteraction);
  setFlags(QGraphicsItem::ItemIsMovable
    | QGraphicsItem::ItemSendsGeometryChanges);

  // tight box (the label is centred on its anchor) and centred lines
  document()->setDocumentMargin(1);
  QTextOption option = document()->defaultTextOption();
  option.setAlignment(Qt::AlignHCenter);
  document()->setDefaultTextOption(option);
}


void PropertyTextItem::refreshText()
{
  if(!property_)
    return;

  QString text = property_->valueText();

  if(theme::showComponentValues()) {
    // the value of a component is its "Value" (or, for a mutual
    // inductance, "M") sub-property
    foreach(QtProperty* sub, property_->subProperties()) {
      const QString name = sub->propertyName();
      if(name == QLatin1String("Value") || name == QLatin1String("M")) {
        QString value = units::format(sub->valueText(), unit_);
        if(name == QLatin1String("M"))
          value.prepend(QStringLiteral("M = "));
        text += QLatin1Char('\n') + value;
        break;
      }
    }
  }

  if(text != toPlainText()) {
    setPlainText(text);
    // centred lines need a text width
    setTextWidth(-1);
    setTextWidth(document()->idealWidth());
  }
}


void PropertyTextItem::valueChanged(QtProperty* property, const QString&)
{
  if(!property_)
    return;
  if(property == property_ || property_->subProperties().contains(property))
    refreshText();
}


QVariant PropertyTextItem::itemChange(
    GraphicsItemChange change, const QVariant& value)
{
  if(change == ItemPositionHasChanged && onMoved)
    onMoved();
  return QGraphicsTextItem::itemChange(change, value);
}


}
