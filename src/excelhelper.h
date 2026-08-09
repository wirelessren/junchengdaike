#pragma once

#include "types.h"

#include <QString>
#include <QVector>

namespace substitute {

class ExcelHelper
{
public:
    static bool validateWorkbook(const QString &path, QString *error = nullptr);
    static QVector<ScheduleEntry> readSchedule(
        const QString &path,
        QString *error = nullptr,
        QString *warning = nullptr);
    static bool exportFreeTeachers(
        const QString &outputPath,
        const QVector<QString> &teachers,
        const SubjectMap &subjectMap,
        const QString &slotText,
        QString *error = nullptr);
    static bool exportNotices(
        const QString &templatePath,
        const QString &outputPath,
        const QVector<Assignment> &assignments,
        const QString &reason,
        const QString &startDate,
        const QString &endDate,
        QString *error = nullptr);
    static bool exportTaskData(
        const QString &outputPath,
        const QVector<TaskDetail> &tasks,
        QString *error = nullptr);
    static bool exportSubstituteStatistics(
        const QString &outputPath,
        const QVector<TaskDetail> &tasks,
        const QString &rangeText,
        QString *error = nullptr);
};

}  // namespace substitute
