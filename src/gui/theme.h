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


#ifndef THEME_H
#define THEME_H


#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QString>


class QApplication;


namespace qsapecng
{
namespace theme
{


// Pick a modern widget style (Fusion unless the user or platform already
// selected a modern one) and apply small global polish. Call once, right
// after constructing QApplication.
void applyAppearance(QApplication& app);

// Colour scheme chosen by the user (Settings > Appearance), persisted in
// QSettings. System follows the platform; Light/Dark force Fusion with a
// matching palette (useful on desktops/Qt versions that do not report a
// dark preference).
enum class Mode { System, Light, Dark };
Mode mode();
void setMode(Mode mode);

// True when the current application palette is a dark one.
bool isDark();

// Line icon from :/icons/<name>.svg, tinted with the palette text colour
// at paint time (so it follows light/dark and the disabled state) and
// rendered as vector at any size / device pixel ratio.
QIcon icon(const QString& name);

// Circuit symbol from :/images/symbols/<name>.svg as an icon, tinted like
// icon() and drawn with its aspect ratio preserved.
QIcon symbolIcon(const QString& name);

// Circuit symbol rendered to a pixmap of the given logical size (HiDPI
// aware), for places that need a QPixmap (list decorations, drag images).
QPixmap symbolPixmap(const QString& name, const QSize& size);

// Sans-serif font for text drawn on the schematic (labels, values).
QFont schematicFont(int pointSize);

// Colours used by the schematic sheet, which always stays "paper" white.
QColor sheetBackground();
QColor sheetForeground();
QColor sheetGrid();

// Makes a user-configured log colour readable on the current background:
// pure black follows the palette text colour, and very dark colours are
// lightened when the theme is dark.
QColor readableOn(const QColor& color);


}
}


#endif // THEME_H
