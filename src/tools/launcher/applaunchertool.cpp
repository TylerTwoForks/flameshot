// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "applaunchertool.h"
#include "tools/launcher/applauncherwidget.h"
#include "utils/confighandler.h"

#include <QGuiApplication>

AppLauncher::AppLauncher(QObject* parent)
  : AbstractActionTool(parent)
{}

bool AppLauncher::closeOnButtonPressed() const
{
    return true;
}

QIcon AppLauncher::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "open_with.svg");
}
QString AppLauncher::name() const
{
    return tr("App Launcher");
}

CaptureTool::Type AppLauncher::type() const
{
    return CaptureTool::TYPE_OPEN_APP;
}

QString AppLauncher::description() const
{
    if (ConfigHandler().extPgm().isEmpty()) {
        return tr("Choose an app to open the capture");
    }
    return tr("Open the capture with the default app (Shift+click to choose "
              "another app)");
}

QWidget* AppLauncher::widget()
{
    return new AppLauncherWidget(capture);
}

CaptureTool* AppLauncher::copy(QObject* parent)
{
    return new AppLauncher(parent);
}

void AppLauncher::pressed(CaptureContext& context)
{
    capture = context.selectedScreenshotArea();
    emit requestAction(REQ_CAPTURE_DONE_OK);

    const QString defaultApp = ConfigHandler().extPgm();
    const bool showChooser = QGuiApplication::keyboardModifiers().testAnyFlags(
      Qt::ShiftModifier | Qt::AltModifier);
    if (defaultApp.isEmpty() || showChooser) {
        emit requestAction(REQ_ADD_EXTERNAL_WIDGETS);
    } else {
        const QString path =
          AppLauncherWidget::saveToTempFile(capture, nullptr);
        if (!path.isEmpty()) {
            AppLauncherWidget::launchApp(
              defaultApp, ConfigHandler().extPgmTerminal(), path, nullptr);
        }
    }
    emit requestAction(REQ_CLOSE_GUI);
}
