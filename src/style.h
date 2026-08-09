#pragma once

#include <QString>

namespace substitute {

struct StyleSheetBundle
{
    QString styleSheet;
    QString overridePath;
};

StyleSheetBundle loadAppStyleSheet(const QString &requestedOverridePath = {});

}  // namespace substitute
