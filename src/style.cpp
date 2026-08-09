#include "style.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <QDebug>

namespace substitute {

namespace {

QString baseStyleSheet()
{
    return QStringLiteral(R"(
QWidget {
    background: #f4f5f7;
    color: #263238;
    font-family: "Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC";
    font-size: 10pt;
}

QLabel {
    background: transparent;
}

QTabWidget::pane {
    border: 1px solid #dfe3e8;
    background: #ffffff;
    border-radius: 10px;
    top: -1px;
}

QTabBar::tab {
    background: #e9ecef;
    color: #5f6872;
    border: 1px solid #dfe3e8;
    border-bottom: 2px solid #dfe3e8;
    padding: 9px 16px;
    margin-right: 4px;
    border-top-left-radius: 8px;
    border-top-right-radius: 8px;
    min-width: 106px;
    font-weight: 600;
}

QTabBar::tab:selected {
    background: #ffffff;
    color: #b85f2e;
    border-bottom: 2px solid #d97745;
}

QTabBar::tab:hover:!selected {
    background: #f1f3f5;
    color: #36424c;
}

QGroupBox {
    border: 1px solid #dfe3e8;
    border-radius: 10px;
    margin-top: 13px;
    padding: 12px 10px 10px 10px;
    background: #ffffff;
    font-weight: 600;
}

QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 6px;
    color: #5c6670;
}

QLineEdit,
QComboBox,
QDateEdit {
    background: #ffffff;
    border: 1px solid #cfd5dc;
    border-radius: 7px;
    padding: 5px 8px;
    min-height: 18px;
    selection-background-color: #f3b58e;
    selection-color: #263238;
}

QLineEdit:hover,
QComboBox:hover,
QDateEdit:hover {
    border-color: #b7bec6;
}

QLineEdit:focus,
QComboBox:focus,
QDateEdit:focus {
    border: 1px solid #d97745;
    background: #ffffff;
}

QTableWidget {
    background: #ffffff;
    border: 1px solid #dfe3e8;
    border-radius: 8px;
    padding: 0;
    selection-background-color: #ffe6d5;
    selection-color: #263238;
    gridline-color: #edf0f2;
    alternate-background-color: #fafbfc;
}

QTableWidget::item:hover {
    background: #fff4ec;
}

QTableWidget::item:selected {
    background: #ffe6d5;
    color: #263238;
}

QLabel#taskListLabel {
    color: #20303f;
    padding: 2px 4px 0 4px;
    font-size: 14pt;
    font-weight: 700;
}

QLabel#taskSearchLabel {
    color: #7a6757;
    font-weight: 600;
}

QLineEdit#taskSearchEdit {
    background: #ffffff;
    border: 1px solid #cfd5dc;
    border-radius: 7px;
    padding: 5px 10px;
}

QLineEdit#taskSearchEdit:hover {
    border-color: #b7bec6;
}

QLineEdit#taskSearchEdit:focus {
    background: white;
    border: 1px solid #d97745;
    padding: 5px 10px;
}

QTableWidget#taskTable QHeaderView::section:hover {
    background: #ffe8d7;
    color: #9a4f28;
}

QTableWidget#taskTable QHeaderView::section:checked {
    background: #d97745;
    color: white;
}

QTableWidget#taskTable QHeaderView::section:pressed {
    background: #b45f29;
    color: white;
}

QPushButton#openTaskButton {
    background: #ffffff;
    color: #8a522f;
    border: 1px solid #d8b294;
}

QPushButton#openTaskButton:hover {
    background: #fff4ec;
    border-color: #d97745;
}

QPushButton#deleteTaskButton {
    background: transparent;
    color: #a8443a;
    border: 1px solid #d59a93;
}

QPushButton#deleteTaskButton:hover {
    background: #fff0ee;
    border-color: #b84e43;
}

QPushButton#deleteTaskButton:pressed {
    background: #f4d7d3;
}

QLabel#backupDataTitleLabel,
QLabel#exportTaskDataTitleLabel,
QLabel#restoreDataTitleLabel {
    color: #3f4a54;
    font-weight: 700;
}

QGroupBox#statisticsExportGroup QDateEdit {
    min-width: 118px;
}

QCheckBox#statisticsAllCheckBox {
    color: #4f5963;
    spacing: 6px;
}

QLabel#backupDataDescriptionLabel,
QLabel#exportTaskDataDescriptionLabel,
QLabel#restoreDataDescriptionLabel {
    color: #77818a;
}

QPushButton#backupDataButton {
    background: #ffffff;
    color: #8a522f;
    border: 1px solid #d8b294;
}

QPushButton#backupDataButton:hover {
    background: #fff4ec;
    border-color: #d97745;
}

QPushButton#scheduleOpenButton,
QPushButton#templateOpenButton {
    background: #ffffff;
    color: #5b6670;
    border: 1px solid #cfd5dc;
}

QPushButton#scheduleOpenButton:hover,
QPushButton#templateOpenButton:hover {
    background: #f5f7f8;
    border-color: #aeb7c0;
}

QPushButton#restoreDataButton {
    background: #ffffff;
    color: #a8443a;
    border: 1px solid #d59a93;
}

QPushButton#restoreDataButton:hover {
    background: #fff0ee;
    border-color: #b84e43;
}

QPushButton {
    background: #d97745;
    color: white;
    border: none;
    border-radius: 7px;
    min-height: 30px;
    padding: 5px 10px;
    font-weight: 600;
}

QPushButton:hover {
    background: #c96735;
}

QPushButton:pressed {
    background: #b45f29;
}

QPushButton:disabled {
    background: #d9dde1;
    color: #8a929a;
}

QHeaderView::section {
    background: #f0f2f4;
    color: #4f5963;
    border: none;
    border-right: 1px solid #dfe3e8;
    border-bottom: 1px solid #dfe3e8;
    padding: 7px;
    font-weight: 700;
}

QLabel[role="summary"] {
    background: #fff4ec;
    border: 1px solid #f1cfb7;
    border-radius: 8px;
    padding: 8px 10px;
    color: #8d512e;
}

QSplitter::handle {
    background: #dfe3e8;
    width: 6px;
}

QSplitter::handle:hover {
    background: #c8ced5;
}
)");
}

QStringList styleOverrideCandidates(const QString &requestedOverridePath)
{
    QStringList candidates;
    auto addCandidate = [&candidates](QString path) {
        path = QDir::cleanPath(path);
        if (!path.isEmpty() && !candidates.contains(path)) {
            candidates.push_back(path);
        }
    };

    if (!requestedOverridePath.trimmed().isEmpty()) {
        addCandidate(requestedOverridePath.trimmed());
        return candidates;
    }

    const QString envPath = qEnvironmentVariable("SUBSTITUTE_UI_STYLE_PATH");
    if (!envPath.trimmed().isEmpty()) {
        addCandidate(envPath.trimmed());
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QDir currentDir(QDir::currentPath());

    addCandidate(appDir.filePath(QStringLiteral("ui/local_override.qss")));
    addCandidate(appDir.filePath(QStringLiteral("local_override.qss")));
    addCandidate(appDir.filePath(QStringLiteral("../ui/local_override.qss")));
    addCandidate(appDir.filePath(QStringLiteral("../local_override.qss")));
    addCandidate(currentDir.filePath(QStringLiteral("ui/local_override.qss")));
    addCandidate(currentDir.filePath(QStringLiteral("local_override.qss")));

    return candidates;
}

QString resolveStyleOverridePath(const QString &requestedOverridePath)
{
    for (const QString &candidate : styleOverrideCandidates(requestedOverridePath)) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

}  // namespace

StyleSheetBundle loadAppStyleSheet(const QString &requestedOverridePath)
{
    StyleSheetBundle bundle;
    bundle.styleSheet = baseStyleSheet();
    bundle.overridePath = resolveStyleOverridePath(requestedOverridePath);

    if (bundle.overridePath.isEmpty()) {
        return bundle;
    }

    QFile overrideFile(bundle.overridePath);
    if (!overrideFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Unable to open UI style override:" << bundle.overridePath;
        bundle.overridePath.clear();
        return bundle;
    }

    const QString overrideStyle = QString::fromUtf8(overrideFile.readAll()).trimmed();
    if (!overrideStyle.isEmpty()) {
        bundle.styleSheet += QStringLiteral("\n\n") + overrideStyle;
    }
    return bundle;
}

}  // namespace substitute
