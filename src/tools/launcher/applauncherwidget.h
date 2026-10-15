// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

// Must be included before #if def, otherwise Q_OS_WIN is unknown
#include <QSysInfo>

#if defined(Q_OS_WIN)
#include "utils/winlnkfileparse.h"
#else
#include "utils/desktopfileparse.h"
#endif

#include <QMap>
#include <QPixmap>
#include <QWidget>

class QTabWidget;
class QCheckBox;
class QVBoxLayout;
class QLineEdit;
class QListWidget;

class AppLauncherWidget : public QWidget
{
    Q_OBJECT
public:
    // Launch opens the capture with the selected app. Pick only reports the
    // selected app through appPicked() and is used to choose a default app.
    enum class Mode
    {
        Launch,
        Pick
    };

    explicit AppLauncherWidget(const QPixmap& p,
                               Mode mode = Mode::Launch,
                               QWidget* parent = nullptr);

    // Returns the path of the saved file, or an empty string on failure.
    static QString saveToTempFile(const QPixmap& pixmap, QWidget* parent);
    static void launchApp(const QString& command,
                          bool inTerminal,
                          const QString& tempFilePath,
                          QWidget* parent);

signals:
    void appPicked(const QString& command, bool inTerminal);

private slots:
    void launch(const QModelIndex& index);
    void checkboxClicked(const bool enabled);
    void setAsDefaultClicked(const bool enabled);
    void searchChanged(const QString& text);

private:
    void initListWidget();
    void initAppMap();
    void configureListView(QListWidget* widget);
    void addAppsToListWidget(QListWidget* widget,
                             const QVector<DesktopAppData>& appList);
    void keyPressEvent(QKeyEvent* keyEvent) override;

#if defined(Q_OS_WIN)
    WinLnkFileParser m_parser;
#else
    DesktopFileParser m_parser;
#endif
    QPixmap m_pixmap;
    Mode m_mode;
    QString m_tempFilePath;
    bool m_keepOpen;
    QMap<QString, QVector<DesktopAppData>> m_appsMap;
    QCheckBox* m_setAsDefaultCheckbox;
    QCheckBox* m_keepOpenCheckbox;
    QCheckBox* m_terminalCheckbox;
    QVBoxLayout* m_layout;
    QLineEdit* m_lineEdit;
    QListWidget* m_filterList;
    QTabWidget* m_tabWidget;
};
