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
#include "gui/resultpanel.h"
#include "config.h"

#include "exception/sapecngexception.h"

#include "gui/qsapecngwindow.h"

#include "gui/configdialog/configdialog.h"
#include "gui/settings.h"
#include "gui/qlogger.h"
#include "gui/delegate.h"

#include "gui/editor/schematiceditor.h"
#include "gui/editor/schematicview.h"
#include "gui/editor/schematicscene.h"
#include "gui/extendedmdiarea.h"
#include "gui/sidebarmodel.h"
#include "gui/sidebarview.h"


#include <QProgressBar>

#include <QMdiArea>
#include <QMdiSubWindow>

#include <QTabWidget>

#include <QMessageBox>
#include <QDialogButtonBox>

#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QSettings>

#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QAction>
#include <QToolBox>
#include <QTextDocument>
#include <QGroupBox>
#include <QPlainTextEdit>

#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>

#include <QSignalMapper>
#include <QActionGroup>
#include <QCloseEvent>
#include <QGraphicsItem>

#include <QUndoView>
#include <QUndoGroup>
#include <QUndoStack>

#include <QFileSystemModel>
#include <QTreeView>

#include <QPrinter>
#include <QPrintDialog>
#include <QPdfWriter>
#include <QSvgGenerator>
#include <QImage>
#include <QPageSize>

#include <QGridLayout>
#include <QVBoxLayout>
#include <QApplication>

#include <limits>


namespace qsapecng
{


QSapecNGWindow::QSapecNGWindow()
{
  Settings().load();

  windowMapper_ = new QSignalMapper(this);
  connect(windowMapper_, &QSignalMapper::mappedObject,
    this, [this](QObject* obj) {
      setActiveSubWindow(qobject_cast<QWidget*>(obj));
    });

  fMapper_ = new QSignalMapper(this);
  connect(fMapper_, &QSignalMapper::mappedInt, this, [this](int id) {
      plot(id);
    });

  mdiArea_ = new ExtendedMdiArea;
  mdiArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  mdiArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  mdiArea_->setViewMode(QMdiArea::TabbedView);
  mdiArea_->setDocumentMode(true);
  mdiArea_->setTabsClosable(true);
  mdiArea_->setTabsMovable(true);

  connect(static_cast<ExtendedMdiArea*>(mdiArea_),
    SIGNAL(dropFileEvent(const QString&)), this, SLOT(open(const QString&)));
  connect(mdiArea_, &QMdiArea::subWindowActivated,
    this, &QSapecNGWindow::syncWithCurrentEditor);

  createLogger();
  createDockWidgets();
  createActions();
  createToolBars();
  createStatusBar();
  createMenus();

  workplane_ = new WorkPlane;
  workplane_->setContextMenu(workplaneMenu_);
  workplane_->xAxisLogScale(xLogAct_->isChecked());
  workplane_->yAxisLogScale(yLogAct_->isChecked());
  workplane_->setMagnitudeDb(dbAct_->isChecked());
  workplane_->setPhaseDegrees(degreesAct_->isChecked());
  workplane_->setAngularFrequency(angularAct_->isChecked());
  workplane_->setData(
      std::map<std::string, double>(),
      sapecng::metacircuit::expression(),
      sapecng::metacircuit::expression()
    );
  workplane_->plot(WorkPlane::NOOP);

  central_ = new QTabWidget;
  central_->addTab(mdiArea_, tr("Editor"));
  central_->addTab(workplane_, tr("Workplane"));
  central_->setTabPosition(QTabWidget::East);

  connect(central_, SIGNAL(currentChanged(int)),
    this, SLOT(sceneSelectionChanged()));
  connect(central_, SIGNAL(currentChanged(int)),
    this, SLOT(updateMenus()));
  connect(central_, SIGNAL(currentChanged(int)),
    this, SLOT(updateDocks()));
  connect(central_, &QTabWidget::currentChanged,
    this, &QSapecNGWindow::updateStatus);

  setCentralWidget(central_);

  sideBarDock_->setObjectName("sideBarDock");
  messageDock_->setObjectName("messageDock");
  undoRedoDock_->setObjectName("undoRedoDock");
  workspaceDock_->setObjectName("workspaceDock");

  fileToolBar_->setObjectName("fileToolBar");
  viewToolBar_->setObjectName("viewToolBar");
  runToolBar_->setObjectName("runToolBar");
  toolToolBar_->setObjectName("toolToolBar");

  readSettings();

  sceneSelectionChanged();
  updateMenus();
  updateDocks();
  loadRecentFileList();
  updateStatus();

  setWindowIcon(QIcon(":/images/qsapecng.png"));
  setWindowTitle(QString("%1 - %2")
      .arg(PACKAGE_NAME)
      .arg(PACKAGE_VERSION)
    );
  setUnifiedTitleAndToolBarOnMac(true);
}


void QSapecNGWindow::closeEvent(QCloseEvent* event)
{
  mdiArea_->closeAllSubWindows();
  mdiArea_->activateNextSubWindow();

  if(activeEditor()) {
    event->ignore();
  } else {
    writeSettings();
    Settings().save();
    event->accept();
  }
}


void QSapecNGWindow::toggleScreenMode()
{
  setWindowState(windowState() ^ Qt::WindowFullScreen);
}


void QSapecNGWindow::configDialog()
{
  ConfigDialog configDialog(this);
  configDialog.exec();

  Settings settings;

  QApplication::setFont(settings.appFont());

  QLogger::setLevel((sapecng::Logger::LogLevel) settings.logLvl());
  policy_->setDebugColor(settings.debugColor());
  policy_->setInfoColor(settings.infoColor());
  policy_->setWarningColor(settings.warningColor());
  policy_->setErrorColor(settings.errorColor());
  policy_->setFatalColor(settings.fatalColor());
}


void QSapecNGWindow::changeWorkspace()
{
  QString dir = QFileDialog::getExistingDirectory(
    this, tr("Choose directory"), workspace_->text(),
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if(!dir.isEmpty()) {
    workspace_->setText(dir);
    wfsView_->setRootIndex(wfsModel_->setRootPath(dir));
    SettingsManager().setWorkspace(dir);
  }
}


void QSapecNGWindow::zoomIn()
{
  SchematicEditor* editor = activeEditor();
  if(editor) {
    editor->view().zoomIn();
    zoomOutAct_->setEnabled(true);
    zoomNormalAct_->setEnabled(true);
  }
}


void QSapecNGWindow::zoomOut()
{
  SchematicEditor* editor = activeEditor();
  if(editor)
    editor->view().zoomOut();
}


void QSapecNGWindow::zoomNormal()
{
  SchematicEditor* editor = activeEditor();
  if(editor)
    editor->view().normalSize();
}


void QSapecNGWindow::fit()
{
  SchematicEditor* editor = activeEditor();
  if(editor)
    editor->view().fitToView();
}


void QSapecNGWindow::wave()
{
  SchematicEditor* editor = activeEditor();
  if(editor)
      editor->accept(*workplane_)
    ? central_->setCurrentWidget(workplane_)
    : central_->setCurrentWidget(mdiArea_)
    ;
}


void QSapecNGWindow::assignNodes()
{
  SchematicEditor* editor = activeEditor();
  if(editor) {
    QLogger::info(tr("Assigning nodes..."));
    editor->scene().assignNodes();
  }
}


void QSapecNGWindow::resolve()
{
  SchematicEditor* editor = activeEditor();
  if(editor) {
    editor->solve();
    mdiArea_->setActiveSubWindow(0);
    mdiArea_->setActiveSubWindow(editor);
    syncWithCurrentEditor();
  }
}


void QSapecNGWindow::plot(int f)
{
  workplane_->plot(f);
}


void QSapecNGWindow::xAxisLogScale(bool log)
{
  workplane_->xAxisLogScale(log);
}


void QSapecNGWindow::yAxisLogScale(bool log)
{
  workplane_->yAxisLogScale(log);
}


void QSapecNGWindow::replot()
{
  workplane_->redraw();
}


void QSapecNGWindow::newFile()
{
  central_->setCurrentWidget(mdiArea_);
  QMdiSubWindow* subWindow = createSchematicEditor();
  subWindow->showMaximized();
  mdiArea_->setActiveSubWindow(subWindow);
  syncWithCurrentEditor();
}


void QSapecNGWindow::open(const QString& fileName)
{
  if(!fileName.isEmpty()) {
    central_->setCurrentWidget(mdiArea_);

    QMdiSubWindow* existing = findEditor(fileName);
    if(existing) {
      mdiArea_->setActiveSubWindow(existing);
      syncWithCurrentEditor();
      return;
    }

    SchematicEditor* editor = createSchematicEditor();
    bool loaded = false;
    try {
      loaded = editor->loadFile(fileName);
    } catch (...) {
      QMessageBox::warning(this, tr("File corrupted"),
          tr("Error loading file.\n")
            .append("Something has gone wrong while reading the file %1.")
            .arg(fileName));
    }

    if(loaded) {
      updateRecentFileList(fileName);
      statusBar()->showMessage(tr("File loaded"), 2000);
      editor->showMaximized();
      mdiArea_->setActiveSubWindow(editor);
      syncWithCurrentEditor();
    } else {
      editor->setWindowModified(false);
      editor->close();
    }
  }
}


void QSapecNGWindow::open()
{
  QString fileName = QFileDialog::getOpenFileName(this,
      tr("Read file"), Settings().workspace(),
      QString("%1;;%2")
        .arg(tr("Info files (*.info)"))
        .arg(tr("XML files (*.xml)"))
    );

  open(fileName);
}


void QSapecNGWindow::openCrc()
{
  QString fileName = QFileDialog::getOpenFileName(this,
      tr("Read file"), Settings().workspace(),
      QString("%1")
        .arg(tr("Netlist files (*.crc)"))
    );

  QMessageBox::information(this, tr("Netlist file"),
      tr("We're sorry. Crc file analysis is still incomplete"));
}


void QSapecNGWindow::openRecent()
{
  QAction* action = qobject_cast<QAction*>(sender());
  if(action) {
    central_->setCurrentWidget(mdiArea_);

    QMdiSubWindow* existing = findEditor(action->data().toString());
    if(existing) {
      mdiArea_->setActiveSubWindow(existing);
      syncWithCurrentEditor();
      return;
    }

    SchematicEditor* editor = createSchematicEditor();
    bool loaded = false;
    try {
      loaded = editor->loadFile(action->data().toString());
    } catch (...) {
      QMessageBox::warning(this, tr("File corrupted"),
          tr("Error loading file.\n")
            .append("Something has gone wrong while reading the file %1.")
            .arg(action->data().toString()));
    }

    if(loaded) {
      updateRecentFileList(action->data().toString());
      statusBar()->showMessage(tr("File loaded"), 2000);
      editor->showMaximized();
      mdiArea_->setActiveSubWindow(editor);
      syncWithCurrentEditor();
    } else {
      editor->setWindowModified(false);
      editor->close();
    }
  }
}


void QSapecNGWindow::save()
{
  SchematicEditor* editor = activeEditor();
  if(editor) {
    editor->scene().assignNodes();
    if(editor->save()) {
      updateRecentFileList(editor->currentFile());
      statusBar()->showMessage(tr("File saved"), 2000);
    }
  }
}


void QSapecNGWindow::saveAs()
{
  SchematicEditor* editor = activeEditor();
  if(editor) {
    editor->scene().assignNodes();
    if(editor->saveAs()) {
      updateRecentFileList(editor->currentFile());
      statusBar()->showMessage(tr("File saved"), 2000);
    }
  }
}


namespace
{


// Draws a schematic as on paper: black on white, without the grid and the
// selection, whatever the theme.
void renderSchematic(SchematicScene& scene, QPainter* painter,
    const QRectF& target, const QRectF& source)
{
  const QList<QGraphicsItem*> selected = scene.selectedItems();
  const bool grid = scene.gridVisible();
  scene.clearSelection();
  scene.setGridVisible(false);

  {
    theme::PaperSheet paper;
    theme::refreshSheetText(&scene);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    scene.render(painter, target, source);
  }
  theme::refreshSheetText(&scene);

  scene.setGridVisible(grid);
  foreach(QGraphicsItem* item, selected)
    item->setSelected(true);
}


QRectF schematicArea(SchematicScene& scene)
{
  return scene.itemsBoundingRect().adjusted(-10, -10, 10, 10);
}


}


void QSapecNGWindow::exportSchematic()
{
  SchematicEditor* editor = activeEditor();
  if(!editor)
    return;

  const QString svgFilter = tr("SVG image (*.svg)");
  const QString pngFilter = tr("PNG image (*.png)");
  const QString pdfFilter = tr("PDF document (*.pdf)");

  QFileInfo current(editor->currentFile());
  QString suggested = current.completeBaseName().isEmpty()
    ? tr("schematic") : current.completeBaseName();
  suggested = QFileInfo(Settings().workspace(), suggested + ".svg")
    .absoluteFilePath();

  QString filter = svgFilter;
  QString fileName = QFileDialog::getSaveFileName(this, tr("Export schematic"),
    suggested, svgFilter + ";;" + pngFilter + ";;" + pdfFilter, &filter);
  if(fileName.isEmpty())
    return;

  QString suffix = QFileInfo(fileName).suffix().toLower();
  if(suffix != "svg" && suffix != "png" && suffix != "pdf") {
    suffix = filter == pngFilter ? "png" : filter == pdfFilter ? "pdf" : "svg";
    fileName += "." + suffix;
  }

  SchematicScene& scene = editor->scene();
  const QRectF source = schematicArea(scene);
  bool ok = true;

  if(suffix == "svg") {
    QSvgGenerator svg;
    svg.setFileName(fileName);
    svg.setSize(source.size().toSize());
    svg.setViewBox(QRectF(QPointF(0, 0), source.size()));
    svg.setTitle(QFileInfo(fileName).completeBaseName());
    svg.setDescription(tr("Schematic exported from %1").arg(PACKAGE_NAME));
    QPainter painter;
    ok = painter.begin(&svg);
    if(ok) {
      renderSchematic(scene, &painter,
        QRectF(QPointF(0, 0), source.size()), source);
      painter.end();
    }
  } else if(suffix == "png") {
    const qreal scale = 4.;   // ~ 300 dpi for a typical schematic
    QImage image((source.size() * scale).toSize(), QImage::Format_ARGB32);
    image.setDotsPerMeterX(11811);
    image.setDotsPerMeterY(11811);
    image.fill(Qt::white);
    QPainter painter(&image);
    renderSchematic(scene, &painter, QRectF(image.rect()), source);
    painter.end();
    ok = image.save(fileName, "PNG");
  } else {
    QPdfWriter pdf(fileName);
    pdf.setTitle(QFileInfo(fileName).completeBaseName());
    pdf.setCreator(PACKAGE_NAME);
    pdf.setResolution(300);
    // page sized to the drawing (scene units taken as 1/96 in)
    const QSizeF inches = source.size() / 96.;
    pdf.setPageSize(QPageSize(inches, QPageSize::Inch));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter;
    ok = painter.begin(&pdf);
    if(ok) {
      const QRect page = painter.viewport();
      renderSchematic(scene, &painter, QRectF(page), source);
      painter.end();
    }
  }

  if(ok)
    QLogger::info(tr("Schematic exported to %1").arg(fileName));
  else
    QLogger::error(tr("Could not write %1").arg(fileName));
}


void QSapecNGWindow::print()
{
  QPrinter printer;
  if(central_->currentWidget() == mdiArea_) {
    SchematicEditor* editor = activeEditor();

    if(editor) {
      if(QPrintDialog(&printer).exec() != QDialog::Accepted)
        return;

      // black on white, also when the dark theme is active
      QPainter painter(&printer);
      renderSchematic(editor->scene(), &painter, QRectF(),
        schematicArea(editor->scene()));
    }
  } else {
    if(QPrintDialog(&printer).exec() != QDialog::Accepted)
      return;

    workplane_->print(printer);
  }
}


void QSapecNGWindow::props()
{
  // same dialog as double-clicking the component
  SchematicEditor* editor = activeEditor();
  if(!editor)
    return;

  foreach(QGraphicsItem* item, editor->scene().selectedItems())
    if(SchematicScene::itemProperties(item)) {
      editor->scene().editItemProperties(item);
      return;
    }
}


void QSapecNGWindow::bin()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().binSelectedItems();
}


void QSapecNGWindow::cut()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().cutSelectedItems();
}


void QSapecNGWindow::copy()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().copySelectedItems();
}


void QSapecNGWindow::paste()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().pasteItems();
}


void QSapecNGWindow::rotate()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().rotatePlacedOrSelected();
}


void QSapecNGWindow::mirror()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().mirrorSelectedItems();
}


void QSapecNGWindow::bringToFront()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().bringToFrontSelectedItem();
}


void QSapecNGWindow::sendToBack()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().sendToBackSelectedItem();
}


void QSapecNGWindow::license()
{
  QString title = tr("License");
  QString body =
    "QSapecNG, Qt-based GUI for SapecNG<br>"
    "Copyright (C) 2009, Michele Caini<br>"
    "Copyright (C) 2026, Wojciech Kaczmarski<br>"
    "<br>"
    "This program is free software: you can redistribute it and/or modify "
    "it under the terms of the GNU General Public License as published by "
    "the Free Software Foundation, either version 3 of the License, or "
    "(at your option) any later version.<br>"
    "<br>"
    "This program is distributed in the hope that it will be useful, "
    "but WITHOUT ANY WARRANTY; without even the implied warranty of "
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
    "GNU General Public License for more details.<br>"
    "<br>"
    "You should have received a copy of the GNU General Public License "
    "along with this program.  If not, see "
    "<a href=\"http://www.gnu.org/licenses/\">http://www.gnu.org/licenses/</a>.";

  QMessageBox::about(this, title, body);
}


void QSapecNGWindow::setResistorStyle(theme::ResistorStyle style)
{
  theme::setResistorStyle(style);

  foreach(QMdiSubWindow* window, mdiArea_->subWindowList())
    if(SchematicEditor* editor = qobject_cast<SchematicEditor*>(window))
      editor->scene().updateResistorSymbols();

  sideBarModel_->refreshSymbols();
}


void QSapecNGWindow::setShowComponentValues(bool show)
{
  theme::setShowComponentValues(show);

  foreach(QMdiSubWindow* window, mdiArea_->subWindowList())
    if(SchematicEditor* editor = qobject_cast<SchematicEditor*>(window))
      editor->scene().refreshComponentLabels();
}


void QSapecNGWindow::about()
{
  QMessageBox::about(this, tr("About %1").arg(PACKAGE_NAME),
    tr("<h3>%1 %2</h3>"
       "<p>Qt-based GUI for SapecNG, symbolic analysis of linear analog "
       "circuits.</p>"
       "<p><b>Authors</b><br>"
       "Michele Caini<br>"
       "Wojciech Kaczmarski</p>"
       "<p>%1 is based in part on the work of the "
       "<a href=\"https://qwt.sourceforge.io\">Qwt project</a>.</p>")
      .arg(PACKAGE_NAME, PACKAGE_VERSION)
  );
}


void QSapecNGWindow::changeEvent(QEvent* event)
{
  QMainWindow::changeEvent(event);

  if(event->type() == QEvent::PaletteChange) {
    // QMdiArea keeps the background brush it was created with, and the
    // component list holds pixmaps tinted for the old palette
    mdiArea_->setBackground(palette().brush(QPalette::Dark));
    sideBarModel_->refreshSymbols();
  }
}


void QSapecNGWindow::activated(const QModelIndex& index)
{
  SchematicEditor* editor = activeEditor();

  // picking a component with no schematic open starts a new one
  if(!editor) {
    newFile();
    editor = activeEditor();
  }

  if(editor)
    editor->scene().setActiveItem(
        (SchematicScene::SupportedItemType)
        sideBarModel_->data(index, Qt::UserRole).toInt()
      );
}


void QSapecNGWindow::sceneSelectionChanged()
{
  bool hasSelectedItems = false;
  SchematicEditor* editor = activeEditor();

  if(editor)
    hasSelectedItems =
      (editor->scene().selectedItems().size() != 0);

  propsAct_->setEnabled(hasSelectedItems);
  binAct_->setEnabled(hasSelectedItems);
  cutAct_->setEnabled(hasSelectedItems);
  copyAct_->setEnabled(hasSelectedItems);
  rotateAct_->setEnabled(hasSelectedItems
    || (editor && editor->scene().isPlacingItem()));
  mirrorAct_->setEnabled(hasSelectedItems);
  bringToFrontAct_->setEnabled(hasSelectedItems);
  sendToBackAct_->setEnabled(hasSelectedItems);
}


void QSapecNGWindow::updateMenus()
{
  bool hasEditor = (activeEditor() != 0);
  bool hasWorkplane = (central_->currentWidget() == workplane_);

  saveAct_->setEnabled(hasEditor);
  saveAsAct_->setEnabled(hasEditor);
  printAct_->setEnabled(hasEditor);
  exportAct_->setEnabled(hasEditor && central_->currentWidget() == mdiArea_);

  pasteAct_->setEnabled(hasEditor);
  if(!hasEditor)
    undoRedoGroup_->setActiveStack(0);

  zoomInAct_->setEnabled(hasEditor);
  zoomOutAct_->setEnabled(hasEditor);
  zoomNormalAct_->setEnabled(hasEditor);
  fitAct_->setEnabled(hasEditor);

  nodeAct_->setEnabled(hasEditor);
  resolveAct_->setEnabled(hasEditor);
  waveAct_->setEnabled(hasEditor);

  cursorAct_->setEnabled(hasEditor);
  userdefAct_->setEnabled(hasEditor);
  disconnectedWireSessionAct_->setEnabled(hasEditor);
  groundSessionAct_->setEnabled(hasEditor);
  portSessionAct_->setEnabled(hasEditor);
  outSessionAct_->setEnabled(hasEditor);
  voltmeterSessionAct_->setEnabled(hasEditor);
  ammeterSessionAct_->setEnabled(hasEditor);
  labelSessionAct_->setEnabled(hasEditor);

  closeAct_->setEnabled(hasEditor);
  closeAllAct_->setEnabled(hasEditor);
  tileAct_->setEnabled(hasEditor);
  cascadeAct_->setEnabled(hasEditor);
  nextAct_->setEnabled(hasEditor);
  previousAct_->setEnabled(hasEditor);

  magnitudeAct_->setEnabled(hasWorkplane);
  phaseAct_->setEnabled(hasWorkplane);
  bodeAct_->setEnabled(hasWorkplane);
  zerosPolesAct_->setEnabled(hasWorkplane);
  rangeAct_->setEnabled(hasWorkplane);
  vrangeAct_->setEnabled(hasWorkplane);
  resetViewAct_->setEnabled(hasWorkplane);

  xLogAct_->setEnabled(hasWorkplane);
  yLogAct_->setEnabled(hasWorkplane && !dbAct_->isChecked());
  replotAct_->setEnabled(hasWorkplane);
}


void QSapecNGWindow::updateWindowMenu()
{
  windowMenu_->clear();
  windowMenu_->addMenu(showMenu_);
  windowMenu_->addSeparator();
  windowMenu_->addAction(closeAct_);
  windowMenu_->addAction(closeAllAct_);
  windowMenu_->addSeparator();
  windowMenu_->addAction(tileAct_);
  windowMenu_->addAction(cascadeAct_);
  windowMenu_->addSeparator();
  windowMenu_->addAction(nextAct_);
  windowMenu_->addAction(previousAct_);
  windowMenu_->addAction(separatorAct_);

  QList<QMdiSubWindow*> windows = mdiArea_->subWindowList();
  separatorAct_->setVisible(!windows.isEmpty());

  for(int i = 0; i < windows.size(); ++i) {
    SchematicEditor* child = qobject_cast<SchematicEditor*>(windows.at(i));

    QString text;
    if(i < 9) {
      text = QString("&%1 %2").arg(i + 1)
        .arg(child->userFriendlyCurrentFile());
    } else {
      text = QString("%1 %2").arg(i + 1)
        .arg(child->userFriendlyCurrentFile());
    }
    QAction* action = windowMenu_->addAction(text);
    action->setCheckable(true);
    action->setChecked(child == activeEditor());
    connect(action, SIGNAL(triggered()), windowMapper_, SLOT(map()));
    windowMapper_->setMapping(action, windows.at(i));
  }
}


void QSapecNGWindow::updateDocks()
{
  bool hasEditor = (activeEditor() != 0);

  // the component list stays usable: a pick opens a new schematic
  undoRedoDock_->widget()->setEnabled(hasEditor);
}


void QSapecNGWindow::createLogger()
{
  policy_ = new QLogPolicy(this);
  policy_->setDebugColor(Qt::black);
  policy_->setInfoColor(Qt::black);
  policy_->setWarningColor(Qt::blue);
  policy_->setErrorColor(Qt::red);

  QLogger::setLevel(sapecng::Logger::DEBUG);
  QLogger::setPolicy(policy_);
}


void QSapecNGWindow::createActions()
{
  newAct_ = new QAction(theme::icon("new"), tr("&New"), this);
  newAct_->setShortcuts(QKeySequence::New);
  newAct_->setStatusTip(tr("New file"));
  connect(newAct_, SIGNAL(triggered()), this, SLOT(newFile()));

  openAct_ = new QAction(theme::icon("open"), tr("&Open..."), this);
  openAct_->setShortcuts(QKeySequence::Open);
  openAct_->setStatusTip(tr("Open"));
  connect(openAct_, SIGNAL(triggered()), this, SLOT(open()));

  openCrcAct_ = new QAction(theme::icon("open"), tr("Open &Crc (netlist)"), this);
  openCrcAct_->setStatusTip(tr("Open Crc"));
  connect(openCrcAct_, SIGNAL(triggered()), this, SLOT(openCrc()));

  saveAct_ = new QAction(theme::icon("save"), tr("&Save"), this);
  saveAct_->setShortcuts(QKeySequence::Save);
  saveAct_->setStatusTip(tr("Save"));
  connect(saveAct_, SIGNAL(triggered()), this, SLOT(save()));

  saveAsAct_ = new QAction(tr("Save &As..."), this);
  saveAsAct_->setShortcuts(QKeySequence::SaveAs);
  saveAsAct_->setStatusTip(tr("Save As..."));
  connect(saveAsAct_, SIGNAL(triggered()), this, SLOT(saveAs()));

  exportAct_ = new QAction(tr("&Export Schematic..."), this);
  exportAct_->setShortcut(tr("Ctrl+E"));
  exportAct_->setStatusTip(tr("Save the schematic as an SVG, PNG or PDF file"));
  connect(exportAct_, &QAction::triggered,
    this, &QSapecNGWindow::exportSchematic);

  printAct_ = new QAction(theme::icon("print"), tr("&Print"), this);
  printAct_->setShortcuts(QKeySequence::Print);
  printAct_->setStatusTip(tr("Print"));
  connect(printAct_, SIGNAL(triggered()), this, SLOT(print()));

  quitAct_ = new QAction(theme::icon("cross"), tr("&Quit"), this);
  quitAct_->setShortcut(tr("Ctrl+Q"));
  quitAct_->setStatusTip(tr("Quit"));
  connect(quitAct_, SIGNAL(triggered()), qApp, SLOT(closeAllWindows()));

  binAct_ = new QAction(theme::icon("bin"), tr("&Delete"), this);
  binAct_->setShortcut(QKeySequence::Delete);
  binAct_->setStatusTip(tr("Delete"));
  connect(binAct_, SIGNAL(triggered()), this, SLOT(bin()));

  undoAct_ = new QAction(theme::icon("undo"), tr("&Undo"), this);
  undoAct_->setShortcut(QKeySequence::Undo);
  undoAct_->setStatusTip(tr("Undo"));
  undoAct_->setEnabled(undoRedoGroup_->canUndo());
  connect(undoAct_, SIGNAL(triggered()), undoRedoGroup_, SLOT(undo()));
  connect(
      undoRedoGroup_, SIGNAL(canUndoChanged(bool)),
      undoAct_, SLOT(setEnabled(bool))
    );

  redoAct_ = new QAction(theme::icon("redo"), tr("&Redo"), this);
  redoAct_->setShortcut(QKeySequence::Redo);
  redoAct_->setStatusTip(tr("Redo"));
  redoAct_->setEnabled(undoRedoGroup_->canRedo());
  connect(redoAct_, SIGNAL(triggered()), undoRedoGroup_, SLOT(redo()));
  connect(
      undoRedoGroup_, SIGNAL(canRedoChanged(bool)),
      redoAct_, SLOT(setEnabled(bool))
    );

  propsAct_ = new QAction(tr("Properties..."), this);
  propsAct_->setStatusTip(tr("Properties..."));
  connect(propsAct_, SIGNAL(triggered()), this, SLOT(props()));

  cutAct_ = new QAction(theme::icon("cut"), tr("Cu&t"), this);
  cutAct_->setShortcut(QKeySequence::Cut);
  cutAct_->setStatusTip(tr("Cut"));
  connect(cutAct_, SIGNAL(triggered()), this, SLOT(cut()));

  copyAct_ = new QAction(theme::icon("copy"), tr("&Copy"), this);
  copyAct_->setShortcut(QKeySequence::Copy);
  copyAct_->setStatusTip(tr("Copy"));
  connect(copyAct_, SIGNAL(triggered()), this, SLOT(copy()));

  pasteAct_ = new QAction(theme::icon("paste"), tr("&Paste"), this);
  pasteAct_->setShortcut(QKeySequence::Paste);
  pasteAct_->setStatusTip(tr("Paste"));
  connect(pasteAct_, SIGNAL(triggered()), this, SLOT(paste()));

  rotateAct_ = new QAction(theme::icon("rotate"), tr("R&otate"), this);
  rotateAct_->setShortcut(QKeySequence(Qt::Key_R));
  rotateAct_->setStatusTip(tr("Rotate the selected items, or the item being placed (R)"));
  connect(rotateAct_, SIGNAL(triggered()), this, SLOT(rotate()));

  mirrorAct_ = new QAction(theme::icon("mirror"), tr("&Mirror"), this);
  mirrorAct_->setStatusTip(tr("Mirror"));
  connect(mirrorAct_, SIGNAL(triggered()), this, SLOT(mirror()));

  bringToFrontAct_ =
    new QAction(theme::icon("bringfront"), tr("Bring to &front"), this);
  bringToFrontAct_->setStatusTip(tr("Bring to front"));
  connect(bringToFrontAct_, SIGNAL(triggered()), this, SLOT(bringToFront()));

  sendToBackAct_ =
    new QAction(theme::icon("sendback"), tr("Send to &back"), this);
  sendToBackAct_->setStatusTip(tr("Send to back"));
  connect(sendToBackAct_, SIGNAL(triggered()), this, SLOT(sendToBack()));

  zoomInAct_ = new QAction(theme::icon("zoomin"), tr("Zoom &In"), this);
  zoomInAct_->setShortcuts(QKeySequence::ZoomIn);
  zoomInAct_->setStatusTip(tr("Zoom In"));
  connect(zoomInAct_, SIGNAL(triggered()), this, SLOT(zoomIn()));

  zoomOutAct_ = new QAction(theme::icon("zoomout"), tr("Zoom &Out"), this);
  zoomOutAct_->setShortcuts(QKeySequence::ZoomOut);
  zoomOutAct_->setStatusTip(tr("Zoom Out"));
  connect(zoomOutAct_, SIGNAL(triggered()), this, SLOT(zoomOut()));

  zoomNormalAct_ = new QAction(theme::icon("zoomnormal"), tr("Zoom &Normal"), this);
  zoomNormalAct_->setStatusTip(tr("Zoom Normal"));
  connect(zoomNormalAct_, SIGNAL(triggered()), this, SLOT(zoomNormal()));

  fitAct_ = new QAction(theme::icon("fit"), tr("&Fit"), this);
  fitAct_->setStatusTip(tr("Fit"));
  connect(fitAct_, SIGNAL(triggered()), this, SLOT(fit()));

  nodeAct_ = new QAction(theme::icon("node"), tr("Assign &nodes"), this);
  nodeAct_->setShortcut(tr("F7"));
  nodeAct_->setStatusTip(tr("Assign nodes"));
  connect(nodeAct_, SIGNAL(triggered()), this, SLOT(assignNodes()));

  resolveAct_ = new QAction(theme::icon("resolve"), tr("&Resolve..."), this);
  resolveAct_->setShortcut(tr("F8"));
  resolveAct_->setStatusTip(tr("Resolve"));
  connect(resolveAct_, SIGNAL(triggered()), this, SLOT(resolve()));

  waveAct_ = new QAction(theme::icon("wave"), tr("&Workplane..."), this);
  waveAct_->setShortcut(tr("F9"));
  waveAct_->setStatusTip(tr("Workplane"));
  connect(waveAct_, SIGNAL(triggered()), this, SLOT(wave()));

  toggleScreenModeAct_ = new QAction(tr("F&ull Screen Mode"), this);
  toggleScreenModeAct_->setStatusTip(tr("Switch to Full Screen Mode"));
  toggleScreenModeAct_->setCheckable(true);
  connect(toggleScreenModeAct_, SIGNAL(triggered()),
    this, SLOT(toggleScreenMode()));

  configureAppAct_ = new QAction(theme::icon("configuration"),
    tr("&Configure QSapecNG..."), this);
  configureAppAct_->setStatusTip(tr("Configure QSapecNG..."));
  connect(configureAppAct_, SIGNAL(triggered()),
    this, SLOT(configDialog()));

  closeAct_ = new QAction(tr("&Close"), this);
  closeAct_->setShortcut(tr("Ctrl+F4"));
  closeAct_->setStatusTip(tr("Close active window"));
  connect(closeAct_, SIGNAL(triggered()),
    mdiArea_, SLOT(closeActiveSubWindow()));

  closeAllAct_ = new QAction(tr("Close &All"), this);
  closeAllAct_->setStatusTip(tr("Close all windows"));
  connect(closeAllAct_, SIGNAL(triggered()),
    mdiArea_, SLOT(closeAllSubWindows()));

  tileAct_ = new QAction(tr("&Tile"), this);
  tileAct_->setStatusTip(tr("Tile"));
  connect(tileAct_, SIGNAL(triggered()), mdiArea_, SLOT(tileSubWindows()));

  cascadeAct_ = new QAction(tr("Casca&de"), this);
  cascadeAct_->setStatusTip(tr("Cascade"));
  connect(cascadeAct_, SIGNAL(triggered()), mdiArea_, SLOT(cascadeSubWindows()));

  nextAct_ = new QAction(tr("&Next"), this);
  nextAct_->setShortcuts(QKeySequence::NextChild);
  nextAct_->setStatusTip(tr("Next window"));
  connect(nextAct_, SIGNAL(triggered()),
    mdiArea_, SLOT(activateNextSubWindow()));

  previousAct_ = new QAction(tr("&Previous"), this);
  previousAct_->setShortcuts(QKeySequence::PreviousChild);
  previousAct_->setStatusTip(tr("Previous window"));
  connect(previousAct_, SIGNAL(triggered()),
    mdiArea_, SLOT(activatePreviousSubWindow()));

  separatorAct_ = new QAction(this);
  separatorAct_->setSeparator(true);

  licenseAct_ = new QAction(theme::icon("license"), tr("&License"), this);
  licenseAct_->setStatusTip(tr("Show License"));
  connect(licenseAct_, SIGNAL(triggered()), this, SLOT(license()));

  aboutAct_ = new QAction(theme::icon("info"), tr("&About"), this);
  aboutAct_->setStatusTip(tr("About QSapecNG"));
  connect(aboutAct_, SIGNAL(triggered()), this, SLOT(about()));

  aboutQtAct_ = new QAction(QIcon(":/images/qt-logo.png"), tr("&Qt"), this);
  aboutQtAct_->setStatusTip(tr("About Qt"));
  connect(aboutQtAct_, SIGNAL(triggered()), qApp, SLOT(aboutQt()));

  userdefAct_ =
    new QAction(theme::symbolIcon("userdef"), tr("&User def"), this);
  userdefAct_->setStatusTip(tr("User def"));
  connect(userdefAct_, SIGNAL(triggered()), this, SLOT(userDefRequested()));

  disconnectedWireSessionAct_ =
    new QAction(theme::symbolIcon("wire"), tr("&Wire"), this);
  disconnectedWireSessionAct_->setShortcut(Qt::Key_W);
  disconnectedWireSessionAct_->setStatusTip(tr("Draw wires: click to start, "
    "click to add a corner, end on a pin or a wire (or double-click); "
    "right-click or / flips the bend, Esc stops"));
  connect(disconnectedWireSessionAct_, SIGNAL(triggered()),
    this, SLOT(disconnectedWireSessionRequested()));

  groundSessionAct_ =
    new QAction(theme::symbolIcon("ground"), tr("&Ground"), this);
  groundSessionAct_->setStatusTip(tr("Ground Session Request"));
  connect(groundSessionAct_, SIGNAL(triggered()),
    this, SLOT(groundSessionRequested()));

  portSessionAct_ =
    new QAction(theme::symbolIcon("port"), tr("&Port"), this);
  portSessionAct_->setStatusTip(tr("Port Session Request"));
  connect(portSessionAct_, SIGNAL(triggered()),
    this, SLOT(portSessionRequested()));

  outSessionAct_ =
    new QAction(theme::symbolIcon("out"), tr("&Out"), this);
  outSessionAct_->setStatusTip(tr("Out Session Request"));
  connect(outSessionAct_, SIGNAL(triggered()),
    this, SLOT(outSessionRequested()));

  voltmeterSessionAct_ =
    new QAction(theme::symbolIcon("voltmeter"), tr("&Voltmeter"), this);
  voltmeterSessionAct_->setStatusTip(tr("Voltmeter Session Request"));
  connect(voltmeterSessionAct_, SIGNAL(triggered()),
    this, SLOT(voltmeterSessionRequested()));

  ammeterSessionAct_ =
    new QAction(theme::symbolIcon("ammeter"), tr("&Ammeter"), this);
  ammeterSessionAct_->setStatusTip(tr("Ammeter Session Request"));
  connect(ammeterSessionAct_, SIGNAL(triggered()),
    this, SLOT(ammeterSessionRequested()));

  labelSessionAct_ =
    new QAction(theme::icon("label"), tr("&Label"), this);
  labelSessionAct_->setStatusTip(tr("Insert Label"));
  connect(labelSessionAct_, SIGNAL(triggered()),
    this, SLOT(labelSessionRequested()));

  cursorAct_ = new QAction(theme::icon("cursor"), tr("&Cursor"), this);
  cursorAct_->setStatusTip(tr("Clear pending request"));
  connect(cursorAct_, SIGNAL(triggered()), this, SLOT(clearSessionRequested()));

  toggleGridAct_ =
    new QAction(theme::icon("grid"), tr("&Toggle grid"), this);
  toggleGridAct_->setStatusTip(tr("Toggle grid"));
  toggleGridAct_->setCheckable(true);
  connect(toggleGridAct_, SIGNAL(triggered(bool)),
    this, SIGNAL(toggleGrid(bool)));
  toggleGridAct_->setChecked(true);

  // what to plot; units and scales are options of the WorkPlane menu
  QSettings plotSettings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION);

  magnitudeAct_ = new QAction(tr("&Frequency Response"), this);
  magnitudeAct_->setStatusTip(tr("Plot the magnitude of the transfer function"));
  connect(magnitudeAct_, &QAction::triggered, this,
    [this]() { workplane_->showKind(WorkPlane::MagnitudeKind); });

  phaseAct_ = new QAction(tr("&Phase Response"), this);
  phaseAct_->setStatusTip(tr("Plot the phase of the transfer function"));
  connect(phaseAct_, &QAction::triggered, this,
    [this]() { workplane_->showKind(WorkPlane::PhaseKind); });

  bodeAct_ = new QAction(tr("&Bode Plot"), this);
  bodeAct_->setStatusTip(
    tr("Magnitude and phase on one plot (phase on the right axis)"));
  connect(bodeAct_, &QAction::triggered, this,
    [this]() { workplane_->showKind(WorkPlane::BodeKind); });

  vrangeAct_ = new QAction(tr("&Vertical Range..."), this);
  vrangeAct_->setStatusTip(tr("Fixed or automatic range of the vertical axis"));
  connect(vrangeAct_, &QAction::triggered, this,
    [this]() { workplane_->editVerticalRange(); });

  resetViewAct_ = new QAction(tr("Reset &View"), this);
  resetViewAct_->setShortcut(Qt::Key_Home);
  resetViewAct_->setStatusTip(tr("Undo zooming (mouse wheel) and panning "
                                 "(middle button)"));
  connect(resetViewAct_, &QAction::triggered, this,
    [this]() { workplane_->resetView(); });

  rangeAct_ = new QAction(tr("Frequency &Range..."), this);
  rangeAct_->setStatusTip(tr("Start, stop and resolution of the sweep"));
  connect(rangeAct_, &QAction::triggered, this,
    [this]() { workplane_->editFrequencyRange(); });

  zerosPolesAct_ = new QAction(tr("P&oles && Zeros"), this);
  zerosPolesAct_->setStatusTip(tr("Plot the poles and zeros in the s-plane"));
  connect(zerosPolesAct_, &QAction::triggered, this,
    [this]() { workplane_->showKind(WorkPlane::PoleZeroKind); });

  dbAct_ = new QAction(tr("Magnitude in &dB"), this);
  dbAct_->setCheckable(true);
  dbAct_->setChecked(plotSettings.value("Workplane/magnitudeDb", true).toBool());
  connect(dbAct_, &QAction::toggled, this, [this](bool on) {
      QSettings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION)
        .setValue("Workplane/magnitudeDb", on);
      workplane_->setMagnitudeDb(on);
      updateMenus();   // a dB axis is never logarithmic
    });

  degreesAct_ = new QAction(tr("Phase in de&grees"), this);
  degreesAct_->setCheckable(true);
  degreesAct_->setChecked(
    plotSettings.value("Workplane/phaseDegrees", true).toBool());
  connect(degreesAct_, &QAction::toggled, this, [this](bool on) {
      QSettings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION)
        .setValue("Workplane/phaseDegrees", on);
      workplane_->setPhaseDegrees(on);
    });

  angularAct_ = new QAction(tr("Frequency in &rad/s"), this);
  angularAct_->setCheckable(true);
  angularAct_->setChecked(
    plotSettings.value("Workplane/angularFrequency", false).toBool());
  connect(angularAct_, &QAction::toggled, this, [this](bool on) {
      QSettings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION)
        .setValue("Workplane/angularFrequency", on);
      workplane_->setAngularFrequency(on);
    });

  xLogAct_ = new QAction(tr("Logarithmic &frequency axis"), this);
  xLogAct_->setStatusTip(tr("Logarithmic or linear frequency axis"));
  xLogAct_->setCheckable(true);
  xLogAct_->setChecked(plotSettings.value("Workplane/xLog", true).toBool());
  connect(xLogAct_, &QAction::toggled, this, [this](bool on) {
      QSettings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION)
        .setValue("Workplane/xLog", on);
      xAxisLogScale(on);
    });

  yLogAct_ = new QAction(tr("Logarithmic &magnitude axis"), this);
  yLogAct_->setStatusTip(
    tr("Logarithmic or linear axis for a linear (not dB) magnitude"));
  yLogAct_->setCheckable(true);
  yLogAct_->setChecked(plotSettings.value("Workplane/yLog", false).toBool());
  connect(yLogAct_, &QAction::toggled, this, [this](bool on) {
      QSettings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION)
        .setValue("Workplane/yLog", on);
      yAxisLogScale(on);
    });

  replotAct_ = new QAction(tr("&Replot"), this);
  replotAct_->setStatusTip(tr("Replot function"));
  connect(replotAct_, SIGNAL(triggered()), this, SLOT(replot()));

  for(int i = 0; i < maxRecentFiles; ++i) {
    openRecentActs_[i] = new QAction(this);
    openRecentActs_[i]->setVisible(false);
    connect(openRecentActs_[i], SIGNAL(triggered()), this, SLOT(openRecent()));
  }
}


void QSapecNGWindow::createDockWidgets()
{
  setDockOptions(QMainWindow::AllowTabbedDocks);
  setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
  setTabPosition(Qt::RightDockWidgetArea, QTabWidget::North);
  setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::South);

  sideBarDock_ = new QDockWidget(tr("Components"), this);
  sideBarDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  sideBarDock_->setTitleBarWidget(0);
  createSideBarDockLayout();
  sideBarDock_->setFocusPolicy(Qt::NoFocus);

  messageDock_ = new QDockWidget(tr("Messages"), this);
  messageDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
  messageDock_->setTitleBarWidget(0);
  createMessageDockLayout();

  undoRedoDock_ = new QDockWidget(tr("Undo/Redo"), this);
  undoRedoDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  undoRedoDock_->setTitleBarWidget(0);
  createUndoRedoDockLayout();

  workspaceDock_ = new QDockWidget(tr("Workspace"), this);
  workspaceDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  workspaceDock_->setTitleBarWidget(0);
  createWorkspaceDockLayout();

  resultDock_ = new QDockWidget(tr("Result"), this);
  resultDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  resultPanel_ = new ResultPanel;
  connect(resultPanel_, &ResultPanel::plotRequested, this, [this](int kind) {
      wave();
      if(central_->currentWidget() == workplane_)
        workplane_->showKind(kind);
    });
  resultDock_->setWidget(resultPanel_);

  // object names identify the docks in the saved layout
  sideBarDock_->setObjectName(QStringLiteral("componentsDock"));
  messageDock_->setObjectName(QStringLiteral("messagesDock"));
  undoRedoDock_->setObjectName(QStringLiteral("undoRedoDock"));
  workspaceDock_->setObjectName(QStringLiteral("workspaceDock"));
  resultDock_->setObjectName(QStringLiteral("resultDock"));

  addDockWidget(Qt::LeftDockWidgetArea, workspaceDock_);
  addDockWidget(Qt::LeftDockWidgetArea, sideBarDock_);
  addDockWidget(Qt::BottomDockWidgetArea, messageDock_);
  addDockWidget(Qt::BottomDockWidgetArea, resultDock_);
  addDockWidget(Qt::RightDockWidgetArea, undoRedoDock_);
  tabifyDockWidget(messageDock_, resultDock_);
}


void QSapecNGWindow::createMenus()
{
  fileMenu_ = menuBar()->addMenu(tr("&File"));
  fileMenu_->addAction(newAct_);
  fileMenu_->addAction(openAct_);
  recentMenu_ = fileMenu_->addMenu(theme::icon("open"), tr("Open &Recent"));
  for(int i = 0; i < maxRecentFiles; ++i)
    recentMenu_->addAction(openRecentActs_[i]);
  createExamplesMenu(fileMenu_);
  fileMenu_->addAction(openCrcAct_);
  fileMenu_->addAction(saveAct_);
  fileMenu_->addAction(saveAsAct_);
  fileMenu_->addSeparator();
  fileMenu_->addAction(exportAct_);
  fileMenu_->addAction(printAct_);
  fileMenu_->addSeparator();
  fileMenu_->addAction(quitAct_);

  editMenu_ = menuBar()->addMenu(tr("&Edit"));
  editMenu_->addAction(undoAct_);
  editMenu_->addAction(redoAct_);
  editMenu_->addSeparator();
  editMenu_->addAction(propsAct_);
  editMenu_->addSeparator();
  editMenu_->addAction(cutAct_);
  editMenu_->addAction(copyAct_);
  editMenu_->addAction(pasteAct_);
  editMenu_->addSeparator();
  editMenu_->addAction(rotateAct_);
  editMenu_->addAction(mirrorAct_);
  editMenu_->addAction(bringToFrontAct_);
  editMenu_->addAction(sendToBackAct_);
  editMenu_->addSeparator();
  editMenu_->addAction(binAct_);

  viewMenu_ = menuBar()->addMenu(tr("&View"));
  viewMenu_->addAction(zoomInAct_);
  viewMenu_->addAction(zoomOutAct_);
  viewMenu_->addAction(zoomNormalAct_);
  viewMenu_->addSeparator();
  viewMenu_->addAction(fitAct_);

  runMenu_ = menuBar()->addMenu(tr("&Run"));
  runMenu_->addAction(nodeAct_);
  runMenu_->addAction(resolveAct_);
  runMenu_->addAction(waveAct_);

  workplaneMenu_ = menuBar()->addMenu(tr("W&orkPlane"));
  workplaneMenu_->addAction(magnitudeAct_);
  workplaneMenu_->addAction(phaseAct_);
  workplaneMenu_->addAction(bodeAct_);
  workplaneMenu_->addAction(zerosPolesAct_);
  workplaneMenu_->addSeparator();
  workplaneMenu_->addAction(rangeAct_);
  workplaneMenu_->addAction(vrangeAct_);
  workplaneMenu_->addAction(resetViewAct_);
  workplaneMenu_->addSeparator();
  workplaneMenu_->addAction(dbAct_);
  workplaneMenu_->addAction(degreesAct_);
  workplaneMenu_->addAction(angularAct_);
  workplaneMenu_->addSeparator();
  workplaneMenu_->addAction(xLogAct_);
  workplaneMenu_->addAction(yLogAct_);
  workplaneMenu_->addSeparator();
  workplaneMenu_->addAction(replotAct_);
  workplaneMenu_->addSeparator();
  QAction* clearMarkersAct = workplaneMenu_->addAction(tr("&Clear Markers"));
  clearMarkersAct->setStatusTip(
    tr("Remove the markers placed on the plots (click a curve to place one, "
       "click a marker to remove it)"));
  connect(clearMarkersAct, &QAction::triggered, this, [this]() {
      workplane_->clearMarkers();
    });

  toolMenu_ = menuBar()->addMenu(tr("&Tools"));
  toolMenu_->addAction(cursorAct_);
  toolMenu_->addAction(labelSessionAct_);
  toolMenu_->addSeparator();
  toolMenu_->addAction(userdefAct_);
  toolMenu_->addAction(disconnectedWireSessionAct_);
  toolMenu_->addAction(groundSessionAct_);
  toolMenu_->addAction(portSessionAct_);
  toolMenu_->addAction(outSessionAct_);
  toolMenu_->addAction(voltmeterSessionAct_);
  toolMenu_->addAction(ammeterSessionAct_);
  toolMenu_->addSeparator();
  toolMenu_->addAction(toggleGridAct_);

  settingMenu_ = menuBar()->addMenu(tr("&Settings"));
  toolbarMenu_ = settingMenu_->addMenu(tr("Toolbars"));
  toolbarMenu_->addAction(fileToolBar_->toggleViewAction());
  toolbarMenu_->addAction(viewToolBar_->toggleViewAction());
  toolbarMenu_->addAction(runToolBar_->toggleViewAction());
  toolbarMenu_->addAction(toolToolBar_->toggleViewAction());
  settingMenu_->addAction(toggleScreenModeAct_);

  QMenu* appearanceMenu = settingMenu_->addMenu(tr("&Appearance"));
  QActionGroup* appearanceGroup = new QActionGroup(appearanceMenu);
  const QList< QPair<QString, theme::Mode> > modes = {
    { tr("Follow &system"), theme::Mode::System },
    { tr("&Light"), theme::Mode::Light },
    { tr("&Dark"), theme::Mode::Dark }
  };
  for(const auto& m : modes) {
    QAction* act = appearanceMenu->addAction(m.first);
    act->setCheckable(true);
    act->setChecked(theme::mode() == m.second);
    appearanceGroup->addAction(act);
    const theme::Mode mode = m.second;
    connect(act, &QAction::triggered, this, [mode]() { theme::setMode(mode); });
  }

  QMenu* resistorMenu = settingMenu_->addMenu(tr("&Resistor symbol"));
  QActionGroup* resistorGroup = new QActionGroup(resistorMenu);
  const QList< QPair<QString, theme::ResistorStyle> > resistorStyles = {
    { tr("&ANSI"), theme::ResistorStyle::Ansi },
    { tr("&IEC / DIN"), theme::ResistorStyle::Din }
  };
  for(const auto& r : resistorStyles) {
    QAction* act = resistorMenu->addAction(r.first);
    act->setCheckable(true);
    act->setChecked(theme::resistorStyle() == r.second);
    resistorGroup->addAction(act);
    const theme::ResistorStyle style = r.second;
    connect(act, &QAction::triggered, this, [this, style]() {
      setResistorStyle(style);
    });
  }

  QMenu* labelMenu = settingMenu_->addMenu(tr("Component &labels"));
  QActionGroup* labelGroup = new QActionGroup(labelMenu);
  const QList< QPair<QString, bool> > labelModes = {
    { tr("&Name only"), false },
    { tr("Name and &value"), true }
  };
  for(const auto& l : labelModes) {
    QAction* act = labelMenu->addAction(l.first);
    act->setCheckable(true);
    act->setChecked(theme::showComponentValues() == l.second);
    labelGroup->addAction(act);
    const bool show = l.second;
    connect(act, &QAction::triggered, this, [this, show]() {
      setShowComponentValues(show);
    });
  }

  QAction* splashAct = settingMenu_->addAction(tr("Show &splash screen"));
  splashAct->setCheckable(true);
  splashAct->setChecked(QSettings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION)
    .value(QStringLiteral("Application/showSplash"), true).toBool());
  connect(splashAct, &QAction::toggled, this, [](bool on) {
      QSettings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION)
        .setValue(QStringLiteral("Application/showSplash"), on);
    });

  settingMenu_->addSeparator();
  settingMenu_->addAction(configureAppAct_);

  windowMenu_ = menuBar()->addMenu(tr("&Window"));
  showMenu_ = windowMenu_->addMenu(tr("&Show"));
  updateWindowMenu();
  connect(windowMenu_, SIGNAL(aboutToShow()), this, SLOT(updateWindowMenu()));
  showMenu_->addAction(sideBarDock_->toggleViewAction());
  showMenu_->addAction(messageDock_->toggleViewAction());
  showMenu_->addAction(resultDock_->toggleViewAction());
  showMenu_->addAction(undoRedoDock_->toggleViewAction());
  showMenu_->addAction(workspaceDock_->toggleViewAction());

  menuBar()->addSeparator();

  helpMenu_ = menuBar()->addMenu(tr("&Help"));
  helpMenu_->addAction(licenseAct_);
  helpMenu_->addSeparator();
  helpMenu_->addAction(aboutAct_);
  helpMenu_->addAction(aboutQtAct_);

  sceneMenu_ = new QMenu(this);
  sceneMenu_->addMenu(editMenu_);
  sceneMenu_->addMenu(viewMenu_);
  sceneMenu_->addMenu(runMenu_);
  sceneMenu_->addMenu(toolMenu_);
}


void QSapecNGWindow::createToolBars()
{
  fileToolBar_ = addToolBar(tr("File"));
  fileToolBar_->setObjectName(QStringLiteral("fileToolBar"));
  fileToolBar_->addAction(newAct_);
  fileToolBar_->addAction(openAct_);
  fileToolBar_->addAction(saveAct_);
  fileToolBar_->addSeparator();
  fileToolBar_->addAction(printAct_);

  viewToolBar_ = addToolBar(tr("View"));
  viewToolBar_->setObjectName(QStringLiteral("viewToolBar"));
  viewToolBar_->addAction(zoomInAct_);
  viewToolBar_->addAction(zoomOutAct_);
  viewToolBar_->addAction(zoomNormalAct_);
  viewToolBar_->addSeparator();
  viewToolBar_->addAction(fitAct_);

  runToolBar_ = addToolBar(tr("Run"));
  runToolBar_->setObjectName(QStringLiteral("runToolBar"));
  runToolBar_->addAction(nodeAct_);
  runToolBar_->addAction(resolveAct_);
  runToolBar_->addAction(waveAct_);

  toolToolBar_ = addToolBar(tr("Tools"));
  toolToolBar_->setObjectName(QStringLiteral("toolToolBar"));
  toolToolBar_->addAction(cursorAct_);
  toolToolBar_->addAction(labelSessionAct_);
  toolToolBar_->addSeparator();
  toolToolBar_->addAction(userdefAct_);
  toolToolBar_->addAction(disconnectedWireSessionAct_);
  toolToolBar_->addAction(groundSessionAct_);
  toolToolBar_->addAction(portSessionAct_);
  toolToolBar_->addAction(outSessionAct_);
  toolToolBar_->addAction(voltmeterSessionAct_);
  toolToolBar_->addAction(ammeterSessionAct_);
  toolToolBar_->addSeparator();
  toolToolBar_->addAction(toggleGridAct_);
}


void QSapecNGWindow::createStatusBar()
{
  // left: what is going on and what the active tool expects; right: size
  // of the drawing, zoom and pointer position. Transient messages
  // (showMessage) cover the left part for a moment.
  statusLabel_ = new QLabel;
  statusLabel_->setTextFormat(Qt::PlainText);
  statusBar()->addWidget(statusLabel_, 1);

  auto field = [](const QString& tip) {
    QLabel* label = new QLabel;
    label->setToolTip(tip);
    label->setContentsMargins(8, 0, 8, 0);
    return label;
  };
  countLabel_ = field(tr("Parts and wires in the schematic"));
  zoomLabel_ = field(tr("Zoom (mouse wheel)"));
  positionLabel_ = field(tr("Pointer position on the sheet (grid units)"));
  positionLabel_->setMinimumWidth(
    positionLabel_->fontMetrics().horizontalAdvance("x -0000  y -0000") + 16);
  statusBar()->addPermanentWidget(countLabel_);
  statusBar()->addPermanentWidget(zoomLabel_);
  statusBar()->addPermanentWidget(positionLabel_);
  
  progressBar_ = new QProgressBar;
  statusBar()->insertPermanentWidget(0, progressBar_, 0);
  progressBar_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
  progressBar_->setMaximumWidth(160);
  progressBar_->reset();
  progressBar_->hide();   // only while a Resolve is running
}


void QSapecNGWindow::createSideBarDockLayout()
{
  sideBarModel_ = new SideBarModel(this);
  sideBarView_ = new SideBarView;
  sideBarView_->setModel(sideBarModel_);
  sideBarDock_->setWidget(sideBarView_);

  connect(sideBarView_, SIGNAL(activated(const QModelIndex&)),
    this, SLOT(activated(const QModelIndex&)));
}


void QSapecNGWindow::createMessageDockLayout()
{
  logViewer_ = new LogViewer;
  logViewer_->setAcceptRichText(false);
  logViewer_->setLineWrapMode(QTextEdit::NoWrap);
  logViewer_->setWordWrapMode(QTextOption::NoWrap);
  QFont logFont = logViewer_->font();
  logFont.setPointSize(QApplication::font().pointSize() - 1);
  logViewer_->setFont(logFont);
  logViewer_->setReadOnly(true);
  logViewer_->setUndoRedoEnabled(false);
  logViewer_->setTextInteractionFlags(
        Qt::TextSelectableByMouse
      | Qt::TextSelectableByKeyboard
    );

  connect(policy_, &QLogPolicy::entry, logViewer_, &LogViewer::addEntry);

  messageDock_->setWidget(logViewer_);
}


void QSapecNGWindow::createUndoRedoDockLayout()
{
  undoRedoGroup_ = new QUndoGroup(this);
  undoRedoView_ = new QUndoView(undoRedoGroup_, this);
  undoRedoView_->setCleanIcon(theme::icon("save"));

  undoRedoDock_->setWidget(undoRedoView_);
}


void QSapecNGWindow::createWorkspaceDockLayout()
{
  QPushButton* wpChange = new QPushButton("...");
  connect(wpChange, SIGNAL(clicked(bool)), this, SLOT(changeWorkspace()));

  workspace_ = new QLineEdit(Settings().workspace());
  workspace_->setReadOnly(true);

  wfsModel_ = new QFileSystemModel;
  wfsView_ = new QTreeView;

  QStringList filters;
  filters << "*.info";
  filters << "*.xml";

  wfsModel_->setNameFilters(filters);
  wfsModel_->setRootPath(workspace_->text());
  wfsModel_->setResolveSymlinks(false);

  wfsView_->setModel(wfsModel_);
  wfsView_->setDragEnabled(true);
  wfsView_->setDragDropMode(QAbstractItemView::DragOnly);
  wfsView_->setRootIndex(wfsModel_->index(wfsModel_->rootPath()));
  wfsView_->setSelectionMode(QAbstractItemView::SingleSelection);
  wfsView_->sortByColumn(0, Qt::AscendingOrder);
  wfsView_->setAlternatingRowColors(true);
  wfsView_->setSortingEnabled(true);

  QGridLayout* wpLayout = new QGridLayout;;
  wpLayout->addWidget(workspace_, 0, 0);
  wpLayout->addWidget(wpChange, 0, 1);
  wpLayout->addWidget(wfsView_, 1, 0, 1, 2);

  QWidget* widget = new QWidget;
  widget->setLayout(wpLayout);

  workspaceDock_->setWidget(widget);
}


void QSapecNGWindow::readSettings()
{
  Settings settings;

  QApplication::setFont(settings.appFont());

  move(settings.mwPos());
  resize(settings.mwSize());

  // Default layout: only Components (left) and Messages (bottom) open;
  // Workspace and Undo/Redo are in Window > Show. A saved
  // layout replaces this, unless it was saved with an older layout version.
  workspaceDock_->hide();
  undoRedoDock_->hide();
  sideBarDock_->raise();
  messageDock_->raise();
  restoreState(settings.mwState(), LayoutVersion);

  QLogger::setLevel((sapecng::Logger::LogLevel) settings.logLvl());
  policy_->setDebugColor(settings.debugColor());
  policy_->setInfoColor(settings.infoColor());
  policy_->setWarningColor(settings.warningColor());
  policy_->setErrorColor(settings.errorColor());
  policy_->setFatalColor(settings.fatalColor());
}


void QSapecNGWindow::writeSettings()
{
  SettingsManager manager;

  manager.setMWPos(pos());
  manager.setMWSize(size());
  manager.setMWState(saveState(LayoutVersion));
}


SchematicEditor* QSapecNGWindow::activeEditor()
{
  if(mdiArea_->currentSubWindow()) {
    SchematicEditor* editor =
      qobject_cast<SchematicEditor*>(mdiArea_->currentSubWindow());
    if(editor && !editor->isRunning())
      return editor;
  }

  return 0;
}


QMdiSubWindow* QSapecNGWindow::findEditor(const QString& fileName)
{
  QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();

  foreach (QMdiSubWindow* window, mdiArea_->subWindowList()) {
    SchematicEditor* child = qobject_cast<SchematicEditor*>(window);
    if (child->currentFile() == canonicalFilePath)
      return window;
  }

  return 0;
}


SchematicEditor* QSapecNGWindow::createSchematicEditor()
{
  QApplication::setOverrideCursor(Qt::WaitCursor);

  SchematicEditor* editor = new SchematicEditor;
  setupSchematicEditor(editor);

  QApplication::restoreOverrideCursor();

  return editor;
}


void QSapecNGWindow::setupSchematicEditor(SchematicEditor* editor)
{
  // status bar: follow the tool, selection, solve state, zoom and pointer
  connect(&editor->scene(), &QGraphicsScene::changed,
    this, [this, editor]() { if(editor == activeEditor()) updateStatus(); });
  connect(&editor->scene(), &QGraphicsScene::selectionChanged,
    this, [this, editor]() { if(editor == activeEditor()) updateStatus(); });
  connect(&editor->view(), &SchematicView::zoomChanged,
    this, [this, editor]() { if(editor == activeEditor()) updateStatus(); });
  connect(&editor->view(), &SchematicView::cursorMoved,
    this, [this, editor](const QPointF& p) {
      if(editor != activeEditor())
        return;
      const int step = SchematicScene::GridStep;
      positionLabel_->setText(QStringLiteral("x %1  y %2")
        .arg(qRound(p.x() / step)).arg(qRound(p.y() / step)));
    });

  editor->scene().setGridVisible(toggleGridAct_->isChecked());
  editor->scene().setContextMenu(sceneMenu_);

  undoRedoGroup_->addStack(editor->scene().undoRedoStack());
  editor->scene().undoRedoStack()->setActive(true);

  connect(editor, SIGNAL(stackEditor(SchematicEditor*)),
    this, SLOT(stackEditor(SchematicEditor*)));
  connect(editor, &SchematicEditor::solved, this, [this, editor]() {
    if(editor != activeEditor())
      return;
    updateResultPanel();
    updateStatus();
    resultDock_->show();
    resultDock_->raise();
  });
  connect(editor, &SchematicEditor::dirtyChanged, this, [this, editor]() {
    if(editor == activeEditor()) {
      updateResultPanel();
      updateStatus();
    }
  });
  connect(&(editor->scene()), &SchematicScene::placementChanged,
      this, &QSapecNGWindow::sceneSelectionChanged);
  connect(&(editor->scene()), SIGNAL(selectionChanged()),
      this, SLOT(sceneSelectionChanged()));
  connect(this, SIGNAL(toggleGrid(bool)),
      &(editor->scene()), SLOT(setGridVisible(bool)));

  editor->setAttribute(Qt::WA_DeleteOnClose);
  editor->setWindowIcon(theme::icon("schematic"));
  mdiArea_->addSubWindow(editor, Qt::SubWindow);
}


void QSapecNGWindow::createExamplesMenu(QMenu* parent)
{
  // installed next to the program (Windows: <dir>/examples, Linux and the
  // AppImage: share/qsapecng/examples), or the source tree for a build
  const QString app = QCoreApplication::applicationDirPath();
  const QStringList candidates = {
    app + "/../examples",
    app + "/examples",
    app + "/../share/qsapecng/examples",
    QStringLiteral(QSAPECNG_SOURCE_EXAMPLES)
  };

  QFileInfoList files;
  foreach(const QString& dir, candidates) {
    files = QDir(dir).entryInfoList(QStringList("*.info"), QDir::Files,
      QDir::Name);
    if(!files.isEmpty())
      break;
  }
  if(files.isEmpty())
    return;

  QMenu* menu = parent->addMenu(tr("Open E&xample"));
  foreach(const QFileInfo& file, files) {
    const QString path = file.absoluteFilePath();
    menu->addAction(file.fileName(), this, [this, path]() { open(path); });
  }
}


void QSapecNGWindow::updateRecentFileList(const QString& fileName)
{
  QStringList files = Settings().recentFiles();

  files.removeAll(fileName);
  files.prepend(fileName);
  while (files.size() > maxRecentFiles)
    files.removeLast();

  SettingsManager().setRecentFiles(files);
  loadRecentFileList();
}


void QSapecNGWindow::loadRecentFileList()
{
  QStringList files = Settings().recentFiles();

  int numRecentFiles = qMin(files.size(), (int) maxRecentFiles);
  for (int i = 0; i < numRecentFiles; ++i) {
    QString text = QString("&%1 %2").arg(i + 1).arg(files[i]);
    openRecentActs_[i]->setText(text);
    openRecentActs_[i]->setData(files[i]);
    openRecentActs_[i]->setVisible(true);
  }
  for (int j = numRecentFiles; j < maxRecentFiles; ++j)
    openRecentActs_[j]->setVisible(false);
}


void QSapecNGWindow::userDefRequested()
{
  SchematicEditor* editor = activeEditor();

  try {
    if(editor)
      editor->scene().setUserDefRequest();
  } catch (...) {
    QMessageBox::warning(this, tr("File corrupted"),
      tr("Incorrect definition of user-defined component."));
  }
}


void QSapecNGWindow::disconnectedWireSessionRequested()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().setWireSessionRequest(false);
}


void QSapecNGWindow::groundSessionRequested()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().setActiveItem(SchematicScene::GroundItemType);
}


void QSapecNGWindow::portSessionRequested()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().setActiveItem(SchematicScene::PortItemType);
}


void QSapecNGWindow::outSessionRequested()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().setActiveItem(SchematicScene::OutItemType);
}


void QSapecNGWindow::voltmeterSessionRequested()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().setActiveItem(SchematicScene::VoltmeterItemType);
}


void QSapecNGWindow::ammeterSessionRequested()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().setActiveItem(SchematicScene::AmmeterItemType);
}


void QSapecNGWindow::labelSessionRequested()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().createLabel();
}


void QSapecNGWindow::clearSessionRequested()
{
  SchematicEditor* editor = activeEditor();

  if(editor)
    editor->scene().resetStatus();
}


void QSapecNGWindow::setActiveSubWindow(QWidget* window)
{
  if (!window)
    return;

  mdiArea_->setActiveSubWindow(qobject_cast<QMdiSubWindow*>(window));
}


void QSapecNGWindow::syncWithCurrentEditor()
{
  // QMdiArea does not always emit subWindowActivated() in tabbed mode
  // (e.g. for files opened before the main window is shown), so the
  // window state is refreshed from currentSubWindow() explicitly.
  subWindowActivated(mdiArea_->currentSubWindow());
  sceneSelectionChanged();
  updateMenus();
  updateDocks();
  updateResultPanel();
  updateStatus();
}


void QSapecNGWindow::updateStatus()
{
  SchematicEditor* editor = activeEditor();
  const bool onWorkplane = central_->currentWidget() == workplane_;

  QString status;
  if(onWorkplane) {
    status = tr("Workplane - click a curve to place a marker, click a marker "
                "to remove it");
  } else if(!editor) {
    status = tr("No schematic open - File > New (Ctrl+N), File > Open, or "
                "pick a component to start one");
  } else if(editor->isRunning()) {
    status = tr("Solving...");
  } else {
    SchematicScene& scene = editor->scene();
    const QString hint = scene.toolHint();
    const int selected = scene.selectedItems().size();
    if(!hint.isEmpty())
      status = hint;
    else if(selected > 0)
      status = (selected == 1 ? tr("1 item selected")
                              : tr("%1 items selected").arg(selected))
        + tr(" - drag to move, R rotates, Del deletes, double-click edits");
    else if(editor->isSolved())
      status = tr("Solved - the transfer function is in the Result panel");
    else if(editor->hasResult())
      status = tr("Changed since the last Resolve - press F8 to update the "
                  "result");
    else
      status = tr("Ready - draw the circuit (W draws wires), mark the "
                  "output with Out, then Resolve (F8)");
  }
  statusLabel_->setText(status);

  const bool showSheet = editor && !onWorkplane;
  countLabel_->setVisible(showSheet);
  zoomLabel_->setVisible(showSheet);
  positionLabel_->setVisible(showSheet);
  if(showSheet) {
    const int parts = editor->scene().partCount();
    const int wires = editor->scene().wireCount();
    countLabel_->setText(
        (parts == 1 ? tr("1 part") : tr("%1 parts").arg(parts))
      + QStringLiteral(" · ")
      + (wires == 1 ? tr("1 wire") : tr("%1 wires").arg(wires)));
    const QTransform t = editor->view().transform();
    zoomLabel_->setText(QStringLiteral("%1 %")
      .arg(qRound(100. * (qAbs(t.m11()) + qAbs(t.m12())))));
  }
}


void QSapecNGWindow::updateResultPanel()
{
  SchematicEditor* editor = activeEditor();
  if(editor && editor->hasResult())
    resultPanel_->showResult(editor->lastResult(), !editor->isSolved());
  else
    resultPanel_->showPlaceholder();
}


void QSapecNGWindow::subWindowActivated(QMdiSubWindow* window)
{
  progressBar_->setRange(0, 1);
  progressBar_->reset();
  progressBar_->hide();

  if(window) {
    SchematicEditor* editor = qobject_cast<SchematicEditor*>(window);
    if(editor && editor->isRunning()) {
      progressBar_->setRange(0, 0);
      progressBar_->show();
      undoRedoGroup_->setActiveStack(0);
    }
  }
}


void QSapecNGWindow::stackEditor(SchematicEditor* editor)
{
  setupSchematicEditor(editor);
  central_->setCurrentWidget(mdiArea_);
  editor->showMaximized();
  mdiArea_->setActiveSubWindow(editor);
  syncWithCurrentEditor();

  statusBar()->showMessage(tr("Component loaded"), 2000);
}


QSapecNGWindow& operator<<(QSapecNGWindow& window, const QString& str)
{
  if(QFile::exists(str))
    window.open(str);

  return window;
}


}
