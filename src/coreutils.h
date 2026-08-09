#pragma once

#include "types.h"

#include <QSet>
#include <QString>

namespace substitute {

QString normalizeDay(const QString &value);
int normalizePeriod(const QString &value);
QString inferTaskReason(const QString &name);
QString normalizeTaskReason(const QString &reason, const QString &taskName = {});
QString teacherInitials(const QString &name);
QString teacherInitial(const QString &name);
QString assignmentSlotKey(const Assignment &assignment);
bool assignmentEquals(const Assignment &left, const Assignment &right);
QVector<Assignment> mergeAssignments(
    const QVector<Assignment> &baseAssignments,
    const QVector<Assignment> &newAssignments);
QString safeFileComponent(const QString &text);
QString safeSheetTitle(const QString &base, const QSet<QString> &usedTitles);

}  // namespace substitute
