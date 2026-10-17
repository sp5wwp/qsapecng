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


#include "gui/resultpanel.h"
#include "gui/theme.h"
#include "gui/workplane/workplane.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFileInfo>
#include <QPalette>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBrowser>
#include <cmath>
#include <QTimer>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>


namespace qsapecng
{


namespace
{


QString fractionHtml(const formula::Fraction& f, const QColor& rule)
{
  // [-] num / den [* factor], laid out as one table row
  const QString cell = QStringLiteral(
    "<td valign='middle' style='padding: 0 4px; font-size: large;'>%1</td>");
  const QString sign = f.negative ? cell.arg(QStringLiteral("−")) : QString();
  const QString factor = formula::renderFactor(f, formula::Style::Html);
  const bool plainNum = formula::isOne(f.num) && !factor.isEmpty();

  QString middle;
  if(formula::isOne(f.den)) {
    // one line: "−10 · V1"
    QString line = f.negative ? QStringLiteral("−") : QString();
    if(!plainNum)
      line += formula::render(f.num, formula::Style::Html);
    if(!factor.isEmpty())
      line += (plainNum ? QString() : QStringLiteral(" · ")) + factor;
    return QStringLiteral(
        "<p style='margin: 6px 0 4px 22px; font-size: large;'>%1</p>")
      .arg(line);
  } else {
    // the fraction line is the bottom border of the numerator cell
    middle = QStringLiteral(
        "<td valign='middle'><table cellspacing='0' cellpadding='0' "
        "style='border-collapse: collapse;'>"
        "<tr><td align='center' style='padding: 2px 10px 4px 10px; "
        "border-bottom-style: solid; border-bottom-width: 1px; "
        "border-bottom-color: %3; font-size: large;'>%1</td></tr>"
        "<tr><td align='center' style='padding: 4px 10px 2px 10px; "
        "font-size: large;'>%2</td></tr>"
        "</table></td>")
      .arg(formula::render(f.num, formula::Style::Html),
           formula::render(f.den, formula::Style::Html), rule.name());
  }

  QString tail;
  if(!factor.isEmpty())
    tail = cell.arg((middle.isEmpty() ? QString() : QStringLiteral("· "))
                    + factor);

  return QStringLiteral(
      "<table cellspacing='0' cellpadding='0' style='margin: 6px 0 4px 12px;'>"
      "<tr>%1%2%3</tr></table>").arg(sign, middle, tail);
}


}


ResultPanel::ResultPanel(QWidget* parent)
  : QWidget(parent), hasResult_(false), outOfDate_(false)
{
  view_ = new QTextBrowser;
  view_->setOpenLinks(false);
  view_->setFrameShape(QFrame::NoFrame);
  connect(view_, &QTextBrowser::anchorClicked,
    this, &ResultPanel::linkClicked);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(view_);

  render();
}


void ResultPanel::showResult(const SolveResult& result, bool outOfDate)
{
  result_ = result;
  hasResult_ = true;
  outOfDate_ = outOfDate;
  render();
}


void ResultPanel::showPlaceholder()
{
  hasResult_ = false;
  outOfDate_ = false;
  render();
}


void ResultPanel::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  // the layout (side by side or stacked) may change with the width
  if(hasResult_)
    QTimer::singleShot(0, this, [this]() { render(); });
}


void ResultPanel::changeEvent(QEvent* event)
{
  QWidget::changeEvent(event);
  if(event->type() == QEvent::PaletteChange)
    render();
}


void ResultPanel::linkClicked(const QUrl& url)
{
  if(url.scheme() == QLatin1String("plot")) {
    emit plotRequested(url.path().toInt());
    return;
  }

  const bool latex = url.scheme() == QLatin1String("latex");
  if(!latex && url.scheme() != QLatin1String("copy"))
    return;

  const QString which = url.path();
  const formula::Fraction* f = 0;
  if(which == QLatin1String("result"))
    f = &result_.mixed;
  else if(which == QLatin1String("raw"))
    f = &result_.raw;
  else if(which == QLatin1String("digit"))
    f = &result_.digit;
  if(!f)
    return;

  const QString text = formula::clipboardText(*f,
    latex ? formula::Style::Latex : formula::Style::Text);
  if(!text.isEmpty())
    QApplication::clipboard()->setText(text);
}


void ResultPanel::render()
{
  const QPalette pal = palette();
  const QColor text = pal.color(QPalette::Text);
  QColor dim = text;
  dim.setAlphaF(0.6);
  const QString dimCss = QStringLiteral("rgba(%1,%2,%3,0.6)")
    .arg(dim.red()).arg(dim.green()).arg(dim.blue());
  const QColor link = pal.color(QPalette::Link);

  QString html = QStringLiteral("<html><body style='margin: 6px;'>");

  if(!hasResult_) {
    html += QStringLiteral("<p style='color:%1;'>%2</p>")
      .arg(dimCss, tr("No result yet. Press <b>Resolve</b> (F8) to solve "
                      "the active schematic."));
    view_->setHtml(html + QStringLiteral("</body></html>"));
    return;
  }

  if(outOfDate_) {
    const QColor bg = theme::isDark() ? QColor(0x4a, 0x3b, 0x12)
                                      : QColor(0xfd, 0xf1, 0xc8);
    const QColor fg = theme::isDark() ? QColor(0xf5, 0xd9, 0x8a)
                                      : QColor(0x6b, 0x4e, 0x00);
    html += QStringLiteral(
        "<table width='100%' cellpadding='6' cellspacing='0' "
        "bgcolor='%1'><tr><td style='color:%2;'>%3</td></tr></table><br>")
      .arg(bg.name(), fg.name(),
           tr("Out of date: the circuit changed since it was solved. "
              "Resolve again (F8) to update."));
  }

  html += QStringLiteral("<p style='color:%1; margin: 0;'>%2</p>")
    .arg(dimCss, QFileInfo(result_.file).fileName().toHtmlEscaped());

  if(result_.singular) {
    const QColor fg = theme::isDark() ? QColor(0xff, 0x9a, 0x8a)
                                      : QColor(0xa0, 0x1b, 0x10);
    html += QStringLiteral("<p style='margin: 12px 0 0 0; color:%1;'><b>%2</b></p>"
        "<p style='margin: 6px 0 0 0;'>%3</p><ul style='margin-top: 4px;'>"
        "<li>%4</li><li>%5</li><li>%6</li><li>%7</li></ul>")
      .arg(fg.name(), tr("No unique solution"),
           tr("The circuit's equations are singular, so there is no transfer "
              "function to show. Typical causes:"),
           tr("voltage sources (or a source and a short) in a loop"),
           tr("current sources in series, or a current source into a node "
              "with no other path"),
           tr("a node or a group of parts not connected to the ground"),
           tr("an ideal op-amp whose inputs cannot be held equal (no "
              "negative feedback path)"));
    html += QStringLiteral("</body></html>");
    view_->setHtml(html);
    return;
  }

  struct Section { QString key, title, note; formula::Fraction f; };
  const Section sections[] = {
    { QStringLiteral("result"), tr("Result"),
      tr("symbolic components by name, the others by value"),
      result_.mixed },
    { QStringLiteral("raw"), tr("Raw"),
      tr("every component by name"),
      result_.raw },
    { QStringLiteral("digit"), tr("Digit"),
      tr("numeric"),
      result_.digit }
  };

  QStringList blocks;
  for(const Section& s : sections) {
    QString block = QStringLiteral(
        "<p style='margin: 12px 0 0 0;'><b>%1</b> "
        "<span style='color:%2;'>&nbsp;%3&nbsp;&nbsp;</span>"
        "<a href='copy:%4' style='color:%5;'>%6</a>&nbsp;&nbsp;"
        "<a href='latex:%4' style='color:%5;'>%7</a></p>")
      .arg(s.title, dimCss, s.note, s.key, link.name(), tr("Copy"),
           tr("Copy LaTeX"));
    block += fractionHtml(s.f, text);
    blocks << block;
  }

  // side by side when the three fit in the panel's width (wide screens),
  // otherwise one below the other
  const int gap = 32;
  int needed = 0;
  for(const QString& block : blocks) {
    QTextDocument doc;
    doc.setDefaultFont(view_->font());
    doc.setDocumentMargin(0);
    doc.setHtml(block);
    needed += int(std::ceil(doc.idealWidth())) + gap;
  }
  const int available = view_->viewport()->width()
    - 2 * int(view_->document()->documentMargin()) - 12;
  sideBySide_ = needed <= available;
  if(sideBySide_) {
    html += QStringLiteral("<table cellspacing='0' cellpadding='0'><tr>");
    for(const QString& block : blocks)
      html += QStringLiteral("<td valign='top' style='padding-right: %1px;'>%2</td>")
        .arg(gap).arg(block);
    html += QStringLiteral("</tr></table>");
  } else {
    html += blocks.join(QString());
  }

  html += QStringLiteral("</body></html>");

  // keep the reading position (e.g. when only the out-of-date state changes)
  const int scroll = view_->verticalScrollBar()->value();
  view_->setHtml(html);
  view_->verticalScrollBar()->setValue(scroll);
}


}
