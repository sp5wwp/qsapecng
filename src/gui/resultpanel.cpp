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
#include <QTabBar>
#include <QTextBrowser>
#include <cmath>
#include <QTimer>
#include <QTextDocument>
#include <QFont>
#include <QUrl>
#include <QVBoxLayout>


namespace qsapecng
{


namespace
{


// width of a piece of the fraction as shown (large font)
int htmlWidth(const QString& html, const QFont& font)
{
  QTextDocument doc;
  doc.setDefaultFont(font);
  doc.setDocumentMargin(0);
  doc.setHtml(QStringLiteral("<span style='font-size: large;'>%1</span>")
    .arg(html));
  return int(std::ceil(doc.idealWidth()));
}


// The expression on one line of at most 'width' pixels: whole terms
// (highest power of s first) while they fit, then as many products of the
// next term's coefficient as fit, and "+ ...":
//   C1 C2 R1 R2 s^4 + (C1 C2 R1 + C1 C2 R2 + ...) s^3 + ...
QString elidedHtml(const formula::Expr& e, int width, const QFont& font)
{
  using formula::Style;
  const QString full = formula::render(e, Style::Html);
  if(htmlWidth(full, font) <= width)
    return full;
  const QString more = QStringLiteral(" + …");

  auto join = [](const QString& left, const QString& right) {
      if(left.isEmpty())
        return right;
      if(right.startsWith(QStringLiteral("−")))
        return left + QStringLiteral(" − ") + right.mid(1);
      return left + QStringLiteral(" + ") + right;
    };
  auto power = [](int degree) {
      if(degree == 1)
        return QStringLiteral(" · <i>s</i>");
      if(degree > 1)
        return QStringLiteral(" · <i>s</i><sup>%1</sup>").arg(degree);
      return QString();
    };

  QString shown;
  for(auto d = e.rbegin(); d != e.rend(); ++d) {
    // the whole term
    formula::Expr one;
    one[d->first] = d->second;
    const QString whole = join(shown, formula::render(one, Style::Html));
    if(htmlWidth(whole + more, font) <= width) {
      shown = whole;
      continue;
    }
    // part of it: the first products of its coefficient
    QString products;
    for(const auto& term : d->second) {
      formula::Expr p;
      p[0].push_back(term);
      const QString next = join(products, formula::render(p, Style::Html));
      const QString candidate = join(shown, QStringLiteral("(") + next + more
        + QStringLiteral(")") + power(d->first)) + more;
      // at least one product when nothing is shown yet
      if(htmlWidth(candidate, font) > width
          && !(shown.isEmpty() && products.isEmpty()))
        break;
      products = next;
    }
    if(!products.isEmpty())
      shown = join(shown, QStringLiteral("(") + products + more
        + QStringLiteral(")") + power(d->first));
    break;
  }
  return shown + more;
}


// width: 0 = everything; otherwise numerator and denominator are each cut
// to one line (elidedHtml)
QString fractionHtml(const formula::Fraction& f, const QColor& rule,
  int width = 0, const QFont& font = QFont())
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
      line += width > 0 ? elidedHtml(f.num, width, font)
                        : formula::render(f.num, formula::Style::Html);
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
      .arg(width > 0 ? elidedHtml(f.num, width, font)
                     : formula::render(f.num, formula::Style::Html),
           width > 0 ? elidedHtml(f.den, width, font)
                     : formula::render(f.den, formula::Style::Html),
           rule.name());
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

  tabs_ = new QTabBar;
  tabs_->setDocumentMode(true);
  tabs_->setExpanding(false);
  tabs_->setDrawBase(true);
  tabs_->hide();
  connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
      if(index < 0 || index >= result_.outputs.size())
        return;
      currentName_ = result_.outputs.at(index).name;
      render();
    });

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(tabs_);
  layout->addWidget(view_);

  render();
}


void ResultPanel::showResult(const SolveResult& result, bool outOfDate)
{
  result_ = result;
  hasResult_ = true;
  outOfDate_ = outOfDate;
  updateTabs();
  render();
}


void ResultPanel::showPlaceholder()
{
  hasResult_ = false;
  outOfDate_ = false;
  result_ = SolveResult();
  updateTabs();
  render();
}


void ResultPanel::updateTabs()
{
  QSignalBlocker block(tabs_);
  while(tabs_->count())
    tabs_->removeTab(0);

  int index = 0;
  for(int i = 0; i < result_.outputs.size(); ++i) {
    const OutputResult& o = result_.outputs.at(i);
    tabs_->addTab(o.name);
    if(o.singular)
      tabs_->setTabToolTip(i, tr("No unique solution"));
    if(o.name == currentName_)
      index = i;
  }
  tabs_->setCurrentIndex(index);
  if(index < result_.outputs.size())
    currentName_ = result_.outputs.at(index).name;
  // tabs only when there is a choice
  tabs_->setVisible(hasResult_ && result_.outputs.size() > 1);
}


const OutputResult* ResultPanel::current() const
{
  const int index = tabs_->currentIndex();
  if(index >= 0 && index < result_.outputs.size())
    return &result_.outputs.at(index);
  return result_.outputs.isEmpty() ? nullptr : &result_.outputs.first();
}


QString ResultPanel::currentOutput() const
{
  const OutputResult* out = current();
  return out ? out->name : QString();
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

  if(url.scheme() == QLatin1String("expand")
      || url.scheme() == QLatin1String("collapse")) {
    const QString key = currentOutput() + QLatin1Char('/') + url.path();
    if(url.scheme() == QLatin1String("expand"))
      expanded_.insert(key);
    else
      expanded_.remove(key);
    render();
    return;
  }

  const bool latex = url.scheme() == QLatin1String("latex");
  if(!latex && url.scheme() != QLatin1String("copy"))
    return;

  const OutputResult* out = current();
  if(!out)
    return;
  const QString which = url.path();
  const formula::Fraction* f = 0;
  if(which == QLatin1String("result"))
    f = &out->mixed;
  else if(which == QLatin1String("raw"))
    f = &out->raw;
  else if(which == QLatin1String("digit"))
    f = &out->digit;
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

  const OutputResult* out = current();
  QString caption = QFileInfo(result_.file).fileName().toHtmlEscaped();
  // the output's name, unless the tabs already say it
  if(out && !out->name.isEmpty() && result_.outputs.size() == 1)
    caption = (caption.isEmpty() ? QString() : caption + QStringLiteral(" · "))
      + tr("output %1").arg(out->name.toHtmlEscaped());
  html += QStringLiteral("<p style='color:%1; margin: 0;'>%2</p>")
    .arg(dimCss, caption);

  if(!out) {
    html += QStringLiteral("</body></html>");
    view_->setHtml(html);
    return;
  }

  if(out->singular) {
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
      out->mixed },
    { QStringLiteral("raw"), tr("Raw"),
      tr("every component by name"),
      out->raw },
    { QStringLiteral("digit"), tr("Digit"),
      tr("numeric"),
      out->digit }
  };

  // a section's heading with its links (Expand / Collapse for a long one)
  auto heading = [&](const Section& s, const QString& toggle) {
      QString links = QStringLiteral(
          "<a href='copy:%1' style='color:%2;'>%3</a>&nbsp;&nbsp;"
          "<a href='latex:%1' style='color:%2;'>%4</a>")
        .arg(s.key, link.name(), tr("Copy"), tr("Copy LaTeX"));
      if(!toggle.isEmpty())
        links += QStringLiteral("&nbsp;&nbsp;<a href='%1:%2' style='color:%3;'>%4</a>")
          .arg(toggle, s.key, link.name(),
               toggle == QLatin1String("expand") ? tr("Expand") : tr("Collapse"));
      return QStringLiteral(
          "<p style='margin: 12px 0 0 0;'><b>%1</b> "
          "<span style='color:%2;'>&nbsp;%3&nbsp;&nbsp;</span>%4</p>")
        .arg(s.title, dimCss, s.note, links);
    };

  auto idealWidth = [this](const QString& html) {
      QTextDocument doc;
      doc.setDefaultFont(view_->font());
      doc.setDocumentMargin(0);
      doc.setHtml(html);
      return int(std::ceil(doc.idealWidth()));
    };

  QStringList blocks;
  QList<int> widths;           // the whole section (side by side or not)
  QList<int> fractionWidths;   // the formula alone (cut short or not)
  for(const Section& s : sections) {
    const QString fraction = fractionHtml(s.f, text);
    QString block = heading(s, QString()) + fraction;
    widths << idealWidth(block);
    fractionWidths << idealWidth(fraction);
    blocks << block;
  }

  // side by side when the three fit in the panel's width (wide screens),
  // otherwise one below the other
  const int gap = 32;
  int needed = 0;
  for(int w : widths)
    needed += w + gap;
  const int available = view_->viewport()->width()
    - 2 * int(view_->document()->documentMargin()) - 12;
  sideBySide_ = needed <= available;

  // one below the other: a section too wide for the panel is shown on
  // one line, cut short, with Expand (and Collapse when expanded)
  if(!sideBySide_) {
    const QString prefix = out->name + QLatin1Char('/');
    for(int i = 0; i < 3; ++i) {
      if(fractionWidths.at(i) <= available)
        continue;
      const Section& s = sections[i];
      if(expanded_.contains(prefix + s.key)) {
        blocks[i] = heading(s, QStringLiteral("collapse"))
          + fractionHtml(s.f, text);
      } else {
        // room for the sign, the input factor and the cell padding
        const int factor = htmlWidth(formula::renderFactor(s.f,
          formula::Style::Html), view_->font());
        const int width = qMax(120, available - factor - 80);
        blocks[i] = heading(s, QStringLiteral("expand"))
          + fractionHtml(s.f, text, width, view_->font());
      }
    }
  }
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
