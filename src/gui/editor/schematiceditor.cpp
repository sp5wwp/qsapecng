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


#include "config.h"
#include "gui/theme.h"
#include "model/metacircuit.h"
#include "parser/parser_factory.h"
#include "parser/crc_circuit.h"

#include "gui/qtsolutions/qtpropertyeditor/QtProperty"
#include "gui/editor/schematiceditor.h"
#include "gui/editor/schematicsceneparser.h"
#include "gui/editor/component.h"
#include <cmath>
#include "gui/units.h"
#include <functional>
#include "gui/editor/undoredocommand.h"
#include "gui/settings.h"
#include "gui/qlogger.h"

#include <QPointer>
#include <QSettings>
#include <QThread>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QUndoStack>
#include <QApplication>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QFocusEvent>

#include "parser/spice_circuit.h"

#include <fstream>
#include <sstream>
#include <map>


namespace qsapecng
{


SchematicEditor::SchematicEditor(QWidget* parent, Qt::WindowFlags flags)
  : QMdiSubWindow(parent, flags), solved_(false)
{
  scene_ = new SchematicScene(this);
  init();
}


SchematicEditor::SchematicEditor(
    SchematicScene& scene,
    QWidget* parent,
    Qt::WindowFlags flags
  ) : QMdiSubWindow(parent, flags), solved_(false)
{
  scene_ = &scene;
  init();
}


QString SchematicEditor::userFriendlyCurrentFile() const
{
  return strippedName(curFile_);
}


QString SchematicEditor::currentFile() const
{
  return curFile_;
}


bool SchematicEditor::accept(WorkPlane& workplane, const QString& output)
{
  if(!solved_) {
    QMessageBox::StandardButton ret;
    ret = QMessageBox::warning(this, tr("Dirty circuit"),
      QString("'%1'").arg(userFriendlyCurrentFile()
        + tr(" has been modified.\n Solve the circuit?")),
      QMessageBox::Yes | QMessageBox::No
        | QMessageBox::Cancel
    );

    if(ret == QMessageBox::Cancel)
      return false;

    if(ret == QMessageBox::Yes) {
      solve();
      return false;
    }
  }

  // the output by name (the order may change between results); the
  // first when it is gone
  int k = 0;
  for(int i = 0; i < solver_.outputCount(); ++i)
    if(QString::fromStdString(solver_.outputName(i)) == output)
      k = i;

  // no transfer function to plot
  if(solver_.singular(k))
    return false;

  // the numeric result (every value already in): what the Result panel
  // shows as Digit, so plots and result always agree. (The plots used to
  // evaluate the symbolic result with values collected again from the
  // parts, where any name not found counted as 0)
  workplane.setData(
      std::map<std::string, double>(),
      solver_.digit_numerator(k),
      solver_.digit_denominator(k)
    );

  // a source with internal resistance Rs: the magnitude as transducer
  // gain (load power over the power the source can give), with the load
  // of the Out marker, or Rs when it has none: |H| 2 sqrt(Rs / RL)
  double rs = 0.;
  int sources = 0;
  foreach(Item* item, scene_->activeItems()) {
    const SchematicScene::SupportedItemType t = SchematicScene::itemType(item);
    if(t != SchematicScene::VoltageSourceItemType
        && t != SchematicScene::SingleVoltageSourceItemType)
      continue;
    ++sources;
    if(QtProperty* p = SchematicScene::itemProperties(item)) {
      QHash<QString, QString> sub;
      foreach(QtProperty* s, p->subProperties())
        sub.insert(s->propertyName(), s->valueText());
      if(sub.contains("Ideal") && !QVariant(sub.value("Ideal")).toBool())
        units::parse(sub.value("Rs"), QStringLiteral("Ω"), &rs);
    }
  }
  double rl = 0.;
  const QString outName = QString::fromStdString(solver_.outputName(k));
  foreach(Item* item, scene_->activeItems())
    if(SchematicScene::itemType(item) == SchematicScene::OutItemType) {
      const Component* c = dynamic_cast<Component*>(item);
      if(SchematicScene::outputName(c) == outName)
        rl = SchematicScene::outputLoad(c);
    }
  if(sources == 1 && rs > 0.) {
    const bool assumed = !(rl > 0.);
    if(assumed)
      rl = rs;
    workplane.setGainScale(2. * std::sqrt(rs / rl), true, assumed);
  } else
    workplane.setGainScale(1., false, false);

  return true;
}


QStringList SchematicEditor::resultOutputs() const
{
  QStringList names;
  for(const OutputResult& o : lastResult_.outputs)
    names << o.name;
  return names;
}


bool SchematicEditor::save()
{
  if(isUntitled_) {
    return saveAs();
  } else {
    return saveFile(curFile_);
  }
}


bool SchematicEditor::saveAs()
{
  const QString infoFilter = tr("Info files (*.info)");
  const QString xmlFilter = tr("XML files (*.xml)");
  const QString crcFilter = tr("Netlist files (*.crc)");
  QString selectedFilter = infoFilter;

  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save As"), Settings().workspace(),
      QString("%1;;%2;;%3").arg(infoFilter, xmlFilter, crcFilter),
      &selectedFilter
    );

  if(fileName.isEmpty())
    return false;

  // Without an extension the file format is unknown (issue #4): add the one
  // matching the file type chosen in the dialog.
  if(QFileInfo(fileName).suffix().isEmpty()) {
    if(selectedFilter == xmlFilter)
      fileName += ".xml";
    else if(selectedFilter == crcFilter)
      fileName += ".crc";
    else
      fileName += ".info";

    // the dialog only confirmed overwriting the name without extension
    if(QFileInfo::exists(fileName)
        && QMessageBox::question(this, tr("Save As"),
             tr("%1 already exists.\nDo you want to replace it?")
               .arg(QFileInfo(fileName).fileName()))
           != QMessageBox::Yes)
      return false;
  }

  return saveFile(fileName);
}


bool SchematicEditor::saveFile(const QString& fileName)
{
  // Check the format before touching the file: opening it for writing
  // truncates it, and an unknown suffix used to produce an empty file while
  // reporting success (issue #4, PR #7).
  const QString suffix = QFileInfo(fileName).suffix().toLower();
  if(suffix != "info" && suffix != "xml" && suffix != "crc") {
    QMessageBox::warning(this, tr("Write file"),
      tr("Unable to write file %1:\nunknown file type \"%2\".\n"
         "Use the .info, .xml or .crc extension.")
        .arg(fileName, suffix.isEmpty() ? tr("(none)") : suffix));
    return false;
  }

  QFile file(fileName);
  
  if(!file.open(QFile::WriteOnly | QFile::Text)) {
    QMessageBox::warning(this, tr("Write file"),
      tr("Unable to write file ")
        + QString("%1:\n%2.")
          .arg(fileName)
          .arg(file.errorString())
    );
    
    return false;
  } else {
    file.close();
  }

  QLogger::info(QObject::tr("Saving file..."));
//   QApplication::setOverrideCursor(Qt::WaitCursor);
  QCursor cur = cursor();
  setCursor(Qt::WaitCursor);

  std::ofstream out_file(QFile::encodeName(fileName));
  sapecng::abstract_builder* out =
    sapecng::builder_factory::builder(suffix.toStdString(), out_file);

  // the file is also a netlist: nodes are numbered even when the circuit
  // was never solved (they used to be saved as 0 then)
  const bool numbered = solved_;
  if(!numbered)
    scene_->assignNodes();

  SchematicSceneParser* parser = new SchematicSceneParser(*scene_);

  if(out)
    parser->parse(*out);

  delete parser;
  if(!numbered)
    scene_->resetNodes();
  delete out;

  out_file.flush();
  if(!out || !out_file) {
    setCursor(cur);
    QMessageBox::warning(this, tr("Write file"),
      tr("Unable to write file %1.").arg(fileName));
    return false;
  }

  setCurrentFile(fileName);
  externalCleanChanged_ = true;
  scene_->undoRedoStack()->setClean();
  emit fileSaved(fileName);

//   QApplication::restoreOverrideCursor();
  setCursor(cur);

  return true;
}


bool SchematicEditor::exportSpice(const QString& fileName,
  const QString& analysis)
{
  std::ofstream out_file(QFile::encodeName(fileName));
  if(!out_file)
    return false;

  sapecng::spice_builder builder(out_file);
  builder.set_title(QFileInfo(curFile_).fileName().toStdString());
  if(!analysis.isEmpty())
    builder.set_analysis(analysis.toStdString());

  // numbered nets, parts not connected to the circuit left out (as when
  // solving: SPICE would stop on them)
  const bool numbered = solved_;
  if(!numbered)
    scene_->assignNodes();
  SchematicSceneParser parser(*scene_);
  parser.setSkipFloating(true);
  parser.parse(builder);
  if(!numbered)
    scene_->resetNodes();

  out_file.flush();
  return bool(out_file);
}


bool SchematicEditor::loadFile(const QString& fileName)
{
  if(maybeSave()) {
    QFile file(fileName);
    if(!file.open(QFile::ReadOnly | QFile::Text)) {
      QMessageBox::warning(this, tr("Read file"),
        tr("Unable to read file ")
        + QString("%1:\n%2.")
          .arg(fileName)
          .arg(file.errorString())
      );
      return false;
    } else {
      file.close();
    }

    scene_->clearSchematicScene();
    QLogger::info(QObject::tr("Loading file..."));
//     QApplication::setOverrideCursor(Qt::WaitCursor);
    QCursor cur = cursor();
    setCursor(Qt::WaitCursor);

    QFileInfo fileInfo(file);
    // a netlist has no drawing: one is made up
    const bool netlist = fileInfo.suffix().toLower() == "crc";
    sapecng::abstract_builder* out = netlist
      ? static_cast<sapecng::abstract_builder*>(new NetlistSceneBuilder(*scene_))
      : static_cast<sapecng::abstract_builder*>(new SchematicSceneBuilder(*scene_));

    std::ifstream in_file(QFile::encodeName(fileName));
    sapecng::abstract_parser* parser =
      sapecng::parser_factory::parser(
        fileInfo.suffix().toLower().toStdString(), in_file);

    if(parser) {
      scene_->undoRedoStack()->beginMacro(QObject::tr("Load file"));

      try {
        parser->parse(*out);
      } catch (...) {
        delete parser;
        delete out;
        throw;
      }

      scene_->undoRedoStack()->endMacro();
    }

    delete parser;
    delete out;

    setCurrentFile(fileName);
    externalCleanChanged_ = true;
    // loading is not an edit: Undo must not take the drawing away (and
    // Redo brought it back with new names and default values). The items
    // are kept, the commands that added them dropped
    {
      QUndoStack* stack = scene_->undoRedoStack();
      std::function<void(const QUndoCommand*)> release =
        [&release](const QUndoCommand* cmd) {
          if(AddItems* add = dynamic_cast<AddItems*>(
              const_cast<QUndoCommand*>(cmd)))
            add->release();
          for(int i = 0; i < cmd->childCount(); ++i)
            release(cmd->child(i));
        };
      for(int i = 0; i < stack->count(); ++i)
        release(stack->command(i));
      stack->clear();
    }
    scene_->undoRedoStack()->setClean();
    emit fileLoaded(fileName);

//     QApplication::restoreOverrideCursor();
    setCursor(cur);
  }

  return true;
}


void SchematicEditor::solve()
{
  if(solved_) {
    emit solved();   // up to date: just show the result again
    return;
  }

  view_->setInteractive(false);
  scene_->assignNodes();

//   QApplication::setOverrideCursor(Qt::WaitCursor);
  setCursor(Qt::WaitCursor);
  QLogger::info(QObject::tr("Generating circuit..."));

  SchematicSceneParser* parser = new SchematicSceneParser(*scene_);
  parser->setSkipFloating(true);
  solver_.apply(*parser);
  delete parser;

  QLogger::info(QObject::tr("Solving..."));
  stopped_ = false;
  solver_.setMaxThreads(QSettings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION)
    .value(QStringLiteral("Application/solverThreads"),
      QThread::idealThreadCount()).toInt());
  solver_.start();
  emit solvingStarted();
}


void SchematicEditor::stop()
{
  if(solver_.isRunning())
    solver_.cancel();
}


void SchematicEditor::reset()
{
  scene_->clearSchematicScene();
}


void SchematicEditor::fileNameChanged(const QString& fileName)
{
  setWindowTitle(fileName + "[*]");
}


void SchematicEditor::finished()
{
//   QApplication::restoreOverrideCursor();
  setCursor(Qt::ArrowCursor);
  view_->setInteractive(true);
  if(solver_.cancelled()) {
    // nothing to show: the last result (if any) stays, out of date
    stopped_ = true;
    QLogger::info(QObject::tr("Stopped."));
    emit solvingStopped();
    return;
  }
  solved_ = true;
  dirtyNotified_ = false;
  showResult();
}


void SchematicEditor::stateChanged(
  Qt::WindowStates oldState, Qt::WindowStates newState)
{
  scene_->undoRedoStack()->setActive(
    newState.testFlag(Qt::WindowActive) && !isRunning());
}


void SchematicEditor::showUserDef(SchematicScene& scene)
{
  SchematicEditor* editor = new SchematicEditor(scene, this);
  editor->scene().undoRedoStack()->setClean();

  connect(editor, SIGNAL(dirtyChanged(bool)),
    this, SLOT(externalCleanChanged()));
  connect(this, SIGNAL(aboutToCloseEditor()), editor, SLOT(close()));

  emit(stackEditor(editor));
}


void SchematicEditor::externalCleanChanged()
{
  externalCleanChanged_ = false;
  cleanChanged();
}


void SchematicEditor::cleanChanged(bool clean)
{
  setWindowModified(!externalCleanChanged_ || !clean);
  setDirty();
}


void SchematicEditor::closeEvent(QCloseEvent *event)
{
  if(maybeSave()) {
    if(solver_.isRunning()) {
      // stop it rather than wait for it (that could take hours)
      solver_.cancel();
      solver_.wait();
    }

    QMdiSubWindow::closeEvent(event);
    emit(aboutToCloseEditor());
    event->accept();
  } else {
    event->ignore();
  }
}


void SchematicEditor::setDirty()
{
  // once: every edit after a solve came here, and each one re-rendered
  // the whole Result panel
  if(!solved_ && dirtyNotified_)
    return;
  solved_ = false;
  dirtyNotified_ = true;
  emit dirtyChanged(solved_);
}


void SchematicEditor::showResult()
{
  // results are displayed by the main window's Result panel
  lastResult_.file = currentFile();
  // the result reads as H(s) times the input: sources are factored out
  std::set<std::string> sources;
  foreach(const QString& name, scene_->sourceNames())
    sources.insert(name.toStdString());
  lastResult_.outputs.clear();
  bool singular = false;
  for(int k = 0; k < solver_.outputCount(); ++k) {
    OutputResult out;
    out.name = QString::fromStdString(solver_.outputName(k));
    out.mixed = formula::simplified(
      solver_.mixed_numerator(k), solver_.mixed_denominator(k), sources);
    out.raw = formula::simplified(
      solver_.raw_numerator(k), solver_.raw_denominator(k), sources);
    out.digit = formula::simplified(
      solver_.digit_numerator(k), solver_.digit_denominator(k));
    out.singular = solver_.singular(k);
    singular = singular || out.singular;
    lastResult_.outputs.append(out);
  }
  if(singular)
    QLogger::warning(QObject::tr("The circuit has no unique solution (its "
      "determinant is zero): look for a loop of voltage sources or "
      "inductors-only paths, current sources in series, a part or node "
      "left floating, or an op-amp without feedback"));
  hasResult_ = true;

  emit solved();
}


void SchematicEditor::setCurrentFile(const QString& fileName)
{
  curFile_ = QFileInfo(fileName).canonicalFilePath();
  isUntitled_ = false;
  setWindowModified(false);
  setWindowTitle(userFriendlyCurrentFile() + "[*]");
}


QString SchematicEditor::strippedName(const QString& fullFileName) const
{
  return QFileInfo(fullFileName).fileName();
}


bool SchematicEditor::maybeSave()
{
  if(isWindowModified()) {
    QMessageBox::StandardButton ret;
    ret = QMessageBox::warning(this, tr("Save file"),
      tr("'%1'").arg(userFriendlyCurrentFile()
        + QString(" has been modified.\n Save the file?")),
      QMessageBox::Save | QMessageBox::Discard
        | QMessageBox::Cancel
      );
    
    if(ret == QMessageBox::Save)
      return save();
    else if(ret == QMessageBox::Cancel)
      return false;
  }

  return true;
}


void SchematicEditor::init()
{
  view_ = new SchematicView(this);

  view_->setScene(scene_);
  // the view's scrollable area follows the drawing (see updateCanvas)
  connect(scene_, &QGraphicsScene::sceneRectChanged,
    view_, &SchematicView::updateCanvas);
//   scene_->setSceneRect(0, 0, 1024, 768);
  scene_->setSceneRect(QRectF());
  scene_->addRect(0, 0, 1E-99, 1E-99)->setVisible(false);

  setWidget(view_);

  connect(scene_, SIGNAL(propertyChanged()),
    this, SLOT(externalCleanChanged()));
  connect(scene_, SIGNAL(showUserDef(SchematicScene&)),
    this, SLOT(showUserDef(SchematicScene&)));
  connect(scene_->undoRedoStack(), SIGNAL(cleanChanged(bool)),
    this, SLOT(cleanChanged(bool)));
  connect(scene_->undoRedoStack(), SIGNAL(indexChanged(int)),
    this, SLOT(cleanChanged()));

  connect(&solver_, SIGNAL(finished()), this, SLOT(finished()));

  connect(this, SIGNAL(windowStateChanged(Qt::WindowStates, Qt::WindowStates)),
    this, SLOT(stateChanged(Qt::WindowStates, Qt::WindowStates)));

  curFile_ = tr("Untitled");
  isUntitled_ = true;
  externalCleanChanged_ = true;

  setGeometry(0, 0, 640, 480);
  setWindowTitle(curFile_ + "[*]");
  setWindowIcon(theme::icon("schematic"));
  setAttribute(Qt::WA_DeleteOnClose);
}


}
