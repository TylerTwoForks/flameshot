// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "applauncherwidget.h"
#include "tools/launcher/launcheritemdelegate.h"
#include "tools/launcher/terminallauncher.h"
#include "utils/confighandler.h"
#include "utils/filenamehandler.h"
#include "utils/globalvalues.h"

#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryFile>

namespace {

#if defined(Q_OS_WIN)
QMap<QString, QString> catIconNames({ { "Graphics", "image.svg" },
                                      { "Utility", "apps.svg" } });
}
#else
QMap<QString, QString> catIconNames(
  { { "Multimedia", "applications-multimedia" },
    { "Development", "applications-development" },
    { "Graphics", "applications-graphics" },
    { "Network", "preferences-system-network" },
    { "Office", "applications-office" },
    { "Science", "applications-science" },
    { "Settings", "preferences-desktop" },
    { "System", "preferences-system" },
    { "Utility", "applications-utilities" } });
}
#endif

AppLauncherWidget::AppLauncherWidget(const QPixmap& p,
                                     Mode mode,
                                     QWidget* parent)
  : QWidget(parent)
  , m_pixmap(p)
  , m_mode(mode)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowIcon(QIcon(GlobalValues::iconPath()));
    setWindowTitle(m_mode == Mode::Pick ? tr("Choose Default Application")
                                        : tr("Open With"));

    m_keepOpen = ConfigHandler().keepOpenAppLauncher();

#if defined(Q_OS_WIN)
    QDir userAppsFolder(
      QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)
        .at(0));
    m_parser.processDirectory(userAppsFolder);

    QString dir(m_parser.getAllUsersStartMenuPath());
    if (!dir.isEmpty()) {
        QDir allUserAppsFolder(dir);
        m_parser.processDirectory(allUserAppsFolder);
    }
#else
    QStringList appsLocations =
      QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);

    for (const auto& appsLocation : appsLocations) {
        QDir appsDir(appsLocation);
        m_parser.processDirectory(QDir(appsDir));
    }

#endif

    initAppMap();
    initListWidget();

    m_terminalCheckbox = new QCheckBox(tr("Launch in terminal"), this);
    m_keepOpenCheckbox = new QCheckBox(tr("Keep open after selection"), this);
    m_keepOpenCheckbox->setChecked(ConfigHandler().keepOpenAppLauncher());
    connect(m_keepOpenCheckbox,
            &QCheckBox::clicked,
            this,
            &AppLauncherWidget::checkboxClicked);
    m_setAsDefaultCheckbox = new QCheckBox(
      tr("Set as default application for subsequent launches"), this);
    m_setAsDefaultCheckbox->setToolTip(
      tr("Clicking the Open With button will launch the default application "
         "directly. Hold Shift or Alt while clicking to show this window "
         "again. Uncheck to remove the default application."));
    m_setAsDefaultCheckbox->setChecked(!ConfigHandler().extPgm().isEmpty());
    connect(m_setAsDefaultCheckbox,
            &QCheckBox::clicked,
            this,
            &AppLauncherWidget::setAsDefaultClicked);

    // search items
    m_lineEdit = new QLineEdit;
    connect(m_lineEdit,
            &QLineEdit::textChanged,
            this,
            &AppLauncherWidget::searchChanged);
    m_filterList = new QListWidget;
    m_filterList->hide();
    configureListView(m_filterList);
    connect(
      m_filterList, &QListWidget::clicked, this, &AppLauncherWidget::launch);

    m_layout = new QVBoxLayout(this);
    m_layout->addWidget(m_filterList);
    m_layout->addWidget(m_tabWidget);
    m_layout->addWidget(m_lineEdit);
    m_layout->addWidget(m_keepOpenCheckbox);
    m_layout->addWidget(m_terminalCheckbox);
    m_layout->addWidget(m_setAsDefaultCheckbox);
    if (m_mode == Mode::Pick) {
        m_keepOpenCheckbox->hide();
        m_setAsDefaultCheckbox->hide();
    }
    m_lineEdit->setFocus();
}

void AppLauncherWidget::launch(const QModelIndex& index)
{
    QString name = index.data(Qt::DisplayRole).toString();
    QString command = index.data(Qt::UserRole).toString();
    bool inTerminal =
      index.data(Qt::UserRole + 1).toBool() || m_terminalCheckbox->isChecked();
    if (m_mode == Mode::Pick) {
        emit appPicked(name, command, inTerminal);
        close();
        return;
    }
    if (m_setAsDefaultCheckbox->isChecked()) {
        setDefaultApp(name, command, inTerminal);
    }
    if (m_tempFilePath.isEmpty() || !QFileInfo(m_tempFilePath).isReadable()) {
        m_tempFilePath = saveToTempFile(m_pixmap, this);
        if (m_tempFilePath.isEmpty()) {
            return;
        }
    }
    launchApp(command, inTerminal, m_tempFilePath, this);
    if (!m_keepOpen) {
        close();
    }
}

QString AppLauncherWidget::saveToTempFile(const QPixmap& pixmap,
                                          QWidget* parent)
{
    // _XXXXXX is how Qt temporary files are mocked before substitution
    QTemporaryFile tempFile(QDir::tempPath() + "/" +
                            FileNameHandler().parsedPattern() + "_XXXXXX.png");
    tempFile.setAutoRemove(false);
    if (!tempFile.open()) {
        QMessageBox::about(
          parent, tr("Error"), tr("Unable to write in") + QDir::tempPath());
        return {};
    }
    tempFile.close();
    if (!pixmap.save(tempFile.fileName())) {
        QMessageBox::about(
          parent, tr("Error"), tr("Unable to write in") + QDir::tempPath());
        return {};
    }
    return tempFile.fileName();
}

void AppLauncherWidget::launchApp(const QString& command,
                                  bool inTerminal,
                                  const QString& tempFilePath,
                                  QWidget* parent)
{
    // Heuristically, if there is a % in the command we assume it is the file
    // name slot
#if defined(Q_OS_WIN)
    // Do not split on Windows, since file path can contain spaces
    // and % is not used in lnk files
    QStringList prog_args;
    prog_args << command;
#else
    QStringList prog_args = QProcess::splitCommand(command);
#endif
    // no quotes because it is going in an array!
    static const QRegularExpression regexp("(\\%.)");
    if (command.contains("%")) {
        // but that means we need to substitute IN the array not the string!
        for (auto& i : prog_args) {
            if (i.contains("%"))
                i.replace(regexp, tempFilePath);
        }
    } else {
        // we really should append the file name if there
        prog_args.append(tempFilePath); // were no replacements
    }
    QString app_name = prog_args.at(0);
    if (inTerminal) {
        bool ok = TerminalLauncher::launchDetached(prog_args);
        if (!ok) {
            QMessageBox::about(
              parent, tr("Error"), tr("Unable to launch in terminal."));
        }
    } else {
        QFileInfo fi(tempFilePath);
        QString workingDir = fi.absolutePath();
        prog_args.removeAt(0); // strip program name out
        QProcess::startDetached(app_name, prog_args, workingDir);
    }
}

void AppLauncherWidget::checkboxClicked(const bool enabled)
{
    m_keepOpen = enabled;
    ConfigHandler().setKeepOpenAppLauncher(enabled);
    m_keepOpenCheckbox->setChecked(enabled);
}

void AppLauncherWidget::setAsDefaultClicked(const bool enabled)
{
    if (!enabled) {
        setDefaultApp(QString(), QString(), false);
    }
}

void AppLauncherWidget::setDefaultApp(const QString& name,
                                      const QString& command,
                                      bool inTerminal)
{
    ConfigHandler config;
    config.setExtPgm(command);
    config.setExtPgmName(name);
    config.setExtPgmTerminal(inTerminal);
}

QString AppLauncherWidget::defaultAppName()
{
    ConfigHandler config;
    QString name = config.extPgmName();
    if (name.isEmpty()) {
#if defined(Q_OS_WIN)
        // Windows commands are .lnk paths that may contain spaces
        name = QFileInfo(config.extPgm()).completeBaseName();
#else
        const QStringList args = QProcess::splitCommand(config.extPgm());
        if (!args.isEmpty()) {
            name = QFileInfo(args.first()).fileName();
        }
#endif
    }
    return name;
}

void AppLauncherWidget::searchChanged(const QString& text)
{
    if (text.isEmpty()) {
        m_filterList->hide();
        m_tabWidget->show();
    } else {
        m_tabWidget->hide();
        m_filterList->show();
        m_filterList->clear();
        const QRegularExpression regexp(
          QRegularExpression::wildcardToRegularExpression("*" + text + "*"),
          QRegularExpression::CaseInsensitiveOption);
        QVector<DesktopAppData> apps;

        for (auto const& i : catIconNames.toStdMap()) {
            const QString& cat = i.first;
            if (!m_appsMap.contains(cat)) {
                continue;
            }
            const QVector<DesktopAppData>& appList = m_appsMap[cat];
            for (const DesktopAppData& app : appList) {
                if (!apps.contains(app) && (app.name.contains(regexp) ||
                                            app.description.contains(regexp))) {
                    apps.append(app);
                }
            }
        }
        addAppsToListWidget(m_filterList, apps);
    }
}

void AppLauncherWidget::initListWidget()
{
    m_tabWidget = new QTabWidget;
    const int size = GlobalValues::buttonBaseSize();
    m_tabWidget->setIconSize(QSize(size, size));

    for (auto const& i : catIconNames.toStdMap()) {
        const QString& cat = i.first;
        const QString& iconName = i.second;

        if (!m_appsMap.contains(cat)) {
            continue;
        }

        auto* itemsWidget = new QListWidget();
        configureListView(itemsWidget);

        const QVector<DesktopAppData>& appList = m_appsMap[cat];
        addAppsToListWidget(itemsWidget, appList);

#if defined(Q_OS_WIN)
        QColor background = this->palette().window().color();
        bool isDark = ColorUtils::colorIsDark(background);
        QString modifier =
          isDark ? PathInfo::whiteIconPath() : PathInfo::blackIconPath();
        m_tabWidget->addTab(
          itemsWidget, QIcon(modifier + iconName), QLatin1String(""));
#else
        m_tabWidget->addTab(
          itemsWidget, QIcon::fromTheme(iconName), QLatin1String(""));
#endif
        m_tabWidget->setTabToolTip(m_tabWidget->count(), cat);
        if (cat == QLatin1String("Graphics")) {
            m_tabWidget->setCurrentIndex(m_tabWidget->count() - 1);
        }
    }
}

void AppLauncherWidget::initAppMap()
{
    QStringList categories({ "AudioVideo",
                             "Audio",
                             "Video",
                             "Development",
                             "Graphics",
                             "Network",
                             "Office",
                             "Science",
                             "Settings",
                             "System",
                             "Utility" });

    m_appsMap = m_parser.getAppsByCategory(categories);

    // Unify multimedia.
    QVector<DesktopAppData> multimediaList;
    QStringList multimediaNames;
    multimediaNames << QStringLiteral("AudioVideo") << QStringLiteral("Audio")
                    << QStringLiteral("Video");
    for (const QString& name : std::as_const(multimediaNames)) {
        if (!m_appsMap.contains(name)) {
            continue;
        }
        for (const auto& i : m_appsMap[name]) {
            if (!multimediaList.contains(i)) {
                multimediaList.append(i);
            }
        }
        m_appsMap.remove(name);
    }

    if (!multimediaList.isEmpty()) {
        m_appsMap.insert(QStringLiteral("Multimedia"), multimediaList);
    }
}

void AppLauncherWidget::configureListView(QListWidget* widget)
{
    widget->setItemDelegate(new LauncherItemDelegate());
    widget->setViewMode(QListWidget::IconMode);
    widget->setResizeMode(QListView::Adjust);
    widget->setSpacing(4);
    widget->setFlow(QListView::LeftToRight);
    widget->setDragEnabled(false);
    widget->setMinimumWidth(GlobalValues::buttonBaseSize() * 11);
    connect(widget, &QListWidget::clicked, this, &AppLauncherWidget::launch);
}

void AppLauncherWidget::addAppsToListWidget(
  QListWidget* widget,
  const QVector<DesktopAppData>& appList)
{
    for (const DesktopAppData& app : appList) {
        auto* buttonItem = new QListWidgetItem(widget);
        buttonItem->setData(Qt::DecorationRole, app.icon);
        buttonItem->setData(Qt::DisplayRole, app.name);
        buttonItem->setData(Qt::UserRole, app.exec);
        buttonItem->setData(Qt::UserRole + 1, app.showInTerminal);
        QColor foregroundColor =
          this->palette().color(QWidget::foregroundRole());
        buttonItem->setForeground(foregroundColor);

        buttonItem->setIcon(app.icon);
        buttonItem->setText(app.name);
        buttonItem->setToolTip(app.description);
    }
}

void AppLauncherWidget::keyPressEvent(QKeyEvent* keyEvent)
{
    if (keyEvent->key() == Qt::Key_Escape) {
        close();
    } else if (keyEvent->key() == Qt::Key_Return) {
        auto* widget = (QListWidget*)m_tabWidget->currentWidget();
        if (m_filterList->isVisible())
            widget = m_filterList;
        auto* item = widget->currentItem();
        if (item == nullptr) {
            item = widget->item(0);
            widget->setCurrentItem(item);
        }
        QModelIndex const idx = widget->currentIndex();
        AppLauncherWidget::launch(idx);
    } else {
        QWidget::keyPressEvent(keyEvent);
    }
}
