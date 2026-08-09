#include "mainwindow.h"
#include "style.h"
#include "uimetrics.h"

#include <QApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QIcon>
#include <QStringList>

namespace {

QString parseStyleOverridePath(const QStringList &arguments)
{
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument == QStringLiteral("--style") && index + 1 < arguments.size()) {
            return arguments.at(index + 1);
        }
        if (argument.startsWith(QStringLiteral("--style="))) {
            return argument.mid(QStringLiteral("--style=").size());
        }
    }
    return {};
}

bool previewModeEnabled(const QStringList &arguments)
{
    return arguments.contains(QStringLiteral("--ui-preview"))
        || qEnvironmentVariableIntValue("SUBSTITUTE_UI_PREVIEW") > 0;
}

}  // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("均程代课管理"));
    app.setApplicationDisplayName(QStringLiteral("均程代课管理"));
    app.setOrganizationName(QStringLiteral("DDY"));

    const QStringList arguments = QCoreApplication::arguments();
    const QString requestedStylePath = parseStyleOverridePath(arguments);
    const auto applyStyle = [&app, &requestedStylePath]() {
        const substitute::StyleSheetBundle bundle = substitute::loadAppStyleSheet(requestedStylePath);
        app.setStyleSheet(bundle.styleSheet);
        return bundle;
    };

    substitute::StyleSheetBundle styleBundle = applyStyle();
    QFileSystemWatcher styleWatcher;
    if (!styleBundle.overridePath.isEmpty()) {
        styleWatcher.addPath(styleBundle.overridePath);
        QObject::connect(
            &styleWatcher,
            &QFileSystemWatcher::fileChanged,
            &app,
            [&styleWatcher, &styleBundle, &applyStyle](const QString &changedPath) {
                styleBundle = applyStyle();
                const QString watchPath =
                    !styleBundle.overridePath.isEmpty() ? styleBundle.overridePath : changedPath;
                if (QFileInfo::exists(watchPath) && !styleWatcher.files().contains(watchPath)) {
                    styleWatcher.addPath(watchPath);
                }
            });
    }

    app.setWindowIcon(QIcon(QStringLiteral(":/app/app_icon.png")));

    QFont font(QStringLiteral("Microsoft YaHei UI"));
    if (!font.exactMatch()) {
        font = QFont(QStringLiteral("Microsoft YaHei"));
    }
    font.setPointSizeF(substitute::ui::kAppFontPointSize);
    app.setFont(font);

    substitute::MainWindow window;
    if (previewModeEnabled(arguments)) {
        window.resize(substitute::ui::kPreviewWindowWidth, substitute::ui::kPreviewWindowHeight);
        window.show();
    } else {
        window.showMaximized();
    }
    return app.exec();
}
