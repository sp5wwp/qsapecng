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
#include <QUrl>
#include <QVBoxLayout>


namespace qsapecng
{


namespace
{


QString fractionHtml(const formula::Fraction& f, const QColor& rule)
{
  const QString num = formula::render(f.num, formula::Style::Html);

  // nothing to divide by: one line
  if(formula::isOne(f.den))
    return QStringLiteral(
        "<p style='margin: 6px 0 4px 22px; font-size: large;'>%1</p>")
      .arg(num);

  // the fraction line is the bottom border of the numerator cell
  return QStringLiteral(
      "<table cellspacing='0' cellpadding='0' "
      "style='margin: 6px 0 4px 12px; border-collapse: collapse;'>"
      "<tr><td align='center' style='padding: 2px 10px 4px 10px; "
      "border-bottom-style: solid; border-bottom-width: 1px; "
      "border-bottom-color: %3; font-size: large;'>%1</td></tr>"
      "<tr><td align='center' style='padding: 4px 10px 2px 10px; "
      "font-size: large;'>%2</td></tr>"
      "</table>")
    .arg(num, formula::render(f.den, formula::Style::Html), rule.name());
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

  // plots of the transfer function (open the Workplane)
  const QList< QPair<QString, int> > plots = {
    { tr("Magnitude"), WorkPlane::MAGNITUDE },
    { tr("Phase"), WorkPlane::PHASE },
    { tr("Gain (dB)"), WorkPlane::GAIN },
    { tr("Poles & zeros"), WorkPlane::ZEROS }
  };
  QStringList plotLinks;
  for(const auto& p : plots)
    plotLinks << QStringLiteral("<a href='plot:%1' style='color:%2;'>%3</a>")
      .arg(p.second).arg(link.name(), p.first);
  html += QStringLiteral("<p style='margin: 4px 0 0 0;'><b>%1</b>&nbsp; %2</p>")
    .arg(tr("Plot:"), plotLinks.join(QStringLiteral(" &nbsp;·&nbsp; ")));

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

  for(const Section& s : sections) {
    html += QStringLiteral(
        "<p style='margin: 12px 0 0 0;'><b>%1</b> "
        "<span style='color:%2;'>&nbsp;%3&nbsp;&nbsp;</span>"
        "<a href='copy:%4' style='color:%5;'>%6</a>&nbsp;&nbsp;"
        "<a href='latex:%4' style='color:%5;'>%7</a></p>")
      .arg(s.title, dimCss, s.note, s.key, link.name(), tr("Copy"),
           tr("Copy LaTeX"));
    html += fractionHtml(s.f, text);
  }

  html += QStringLiteral("</body></html>");

  // keep the reading position (e.g. when only the out-of-date state changes)
  const int scroll = view_->verticalScrollBar()->value();
  view_->setHtml(html);
  view_->verticalScrollBar()->setValue(scroll);
}


}
