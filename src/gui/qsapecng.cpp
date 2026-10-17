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


#include "gui/qsapecngwindow.h"
#include "gui/theme.h"
#include "config.h"

#include <QApplication>
#include <QSplashScreen>
#include <QPixmap>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QSettings>
#include <QElapsedTimer>
#include <QTimer>

#include <memory>


int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  // explicit names: dialogs without a title use the application name, which
  // would otherwise be the executable name (e.g. "AppRun.wrapped" in an
  // AppImage); the desktop file name lets Wayland compositors find the icon
  QApplication::setApplicationName(QStringLiteral(PACKAGE_NAME));
  QApplication::setApplicationVersion(QStringLiteral(PACKAGE_VERSION));
  QGuiApplication::setDesktopFileName(QStringLiteral("qsapecng"));
  qsapecng::theme::applyAppearance(app);
  
  // splash screen (Settings > Show splash screen), kept up for a moment
  // in front of the main window; a click closes it early
  const bool showSplash = QSettings(SETTINGS_ORGANIZATION, SETTINGS_APPLICATION)
    .value(QStringLiteral("Application/showSplash"), true).toBool();
  const int splashMs = 2500;

  QElapsedTimer started;
  started.start();
  std::unique_ptr<QSplashScreen> splash;
  if(showSplash) {
    splash.reset(new QSplashScreen(QPixmap(":/images/qsapecng-splash.png"),
      Qt::WindowStaysOnTopHint));
    splash->show();
    splash->showMessage(QObject::tr("Loading QSapecNG..."),
      Qt::AlignBottom | Qt::AlignLeft);
    app.processEvents();
  }

  qsapecng::QSapecNGWindow window;

  // Show the main window before opening files given on the command line:
  // QMdiArea only activates sub-windows properly once it is visible.
  window.show();

  const QStringList args = app.arguments();
  if(args.size() > 1) {
    if(splash) {
      splash->showMessage(QObject::tr("Loading file(s)..."),
        Qt::AlignBottom | Qt::AlignLeft);
      app.processEvents();
    }

    for(int i = 1; i < args.size(); ++i)
      window << args.at(i);
  }

  if(splash) {
    splash->raise();
    const int left = qMax(0, splashMs - int(started.elapsed()));
    QTimer::singleShot(left, splash.get(), &QSplashScreen::close);
  }

  return app.exec();
}
