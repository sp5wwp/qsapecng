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
#include "model/metacircuit.h"
#include "parser/parser_factory.h"
#include "parser/crc_circuit.h"

#include "gui/qtsolutions/qtpropertyeditor/QtProperty"
#include "gui/editor/schematiceditor.h"
#include "gui/editor/schematicsceneparser.h"
#include "gui/settings.h"
#include "gui/qlogger.h"

#include <QPointer>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QUndoStack>
#include <QApplication>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QFocusEvent>

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


bool SchematicEditor::accept(WorkPlane& workplane)
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

  std::map<std::string, double> values;
  QList<Item*> items = scene_->activeItems();
  while(!items.isEmpty()) {
    Item* item = items.takeFirst();
    
    if(SchematicScene::itemType(item) == SchematicScene::UserDefItemType) {
      items.append(item->data(101)
        .value< QPointer<qsapecng::SchematicScene> >()->activeItems());
    } else {
      if(SchematicScene::itemProperties(item)) {
        QtProperty* props = SchematicScene::itemProperties(item);
        QHash<QString, QString> subs;
        
        if(props) {
          subs.insert("__NAME", props->valueText());
          foreach(QtProperty* prop, props->subProperties())
            subs.insert(prop->propertyName(), prop->valueText());
        }

        if(subs.contains("Value"))
          values[subs.value("__NAME").toStdString()]
            = subs.value("Value").toDouble();
        else if(subs.contains("M"))
          values[subs.value("__NAME").toStdString()]
            = subs.value("M").toDouble();

        if(subs.contains("lp:name") && subs.contains("lp:value"))
          values[subs.value("lp:name").toStdString()]
            = subs.value("lp:value").toDouble();

        if(subs.contains("ls:name") && subs.contains("ls:value"))
          values[subs.value("ls:name").toStdString()]
            = subs.value("ls:value").toDouble();
      }
    }
  }

  workplane.setData(
      values,
      solver_.raw_numerator(),
      solver_.raw_denominator()
    );

  return true;
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

  SchematicSceneParser* parser = new SchematicSceneParser(*scene_);

  if(out)
    parser->parse(*out);

  delete parser;
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

    SchematicSceneBuilder* out = new SchematicSceneBuilder(*scene_);

    QFileInfo fileInfo(file);
    std::ifstream in_file(QFile::encodeName(fileName));
    sapecng::abstract_parser* parser =
      sapecng::parser_factory::parser(
        fileInfo.suffix().toStdString(), in_file);

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
  solver_.start();
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
  solved_ = true;
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
    if(solver_.isRunning())
      solver_.wait();
    
    QMdiSubWindow::closeEvent(event);
    emit(aboutToCloseEditor());
    event->accept();
  } else {
    event->ignore();
  }
}


void SchematicEditor::setDirty()
{
  solved_ = false;
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
  lastResult_.mixed = formula::simplified(
    solver_.mixed_numerator(), solver_.mixed_denominator(), sources);
  lastResult_.raw = formula::simplified(
    solver_.raw_numerator(), solver_.raw_denominator(), sources);
  lastResult_.digit = formula::simplified(
    solver_.digit_numerator(), solver_.digit_denominator());
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
