#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

namespace substitute {

struct ScheduleEntry {
    QString subject;
    QString teacher;
    QString day;
    int period = 0;
    QString className;
};

struct Assignment {
    QString absentTeacher;
    QString day;
    int period = 0;
    QString className;
    QString subject;
    QString substituteTeacher;
    QString substituteSubject;
};

struct SlotSelection {
    QString absentTeacher;
    QString day;
    int period = 0;
    QString className;
    QString subject;
};

struct TaskSummary {
    qint64 id = 0;
    QString name;
    QString startDate;
    QString endDate;
    QString updatedAt;
    qint64 baseTaskId = 0;
    QString reason;
};

struct TaskDetail {
    TaskSummary summary;
    QVector<Assignment> assignments;
};

inline const QStringList kDays = {
    QStringLiteral("周一"),
    QStringLiteral("周二"),
    QStringLiteral("周三"),
    QStringLiteral("周四"),
    QStringLiteral("周五"),
    QStringLiteral("周六"),
    QStringLiteral("周日"),
};

inline const QStringList kDefaultSchoolDays = {
    QStringLiteral("周一"),
    QStringLiteral("周二"),
    QStringLiteral("周三"),
    QStringLiteral("周四"),
    QStringLiteral("周五"),
};

inline const QList<int> kPeriods = {1, 2, 3, 4, 5, 6, 7, 8};

inline const QStringList kSubjectOrder = {
    QStringLiteral("语文"),
    QStringLiteral("数学"),
    QStringLiteral("英语"),
    QStringLiteral("物理"),
    QStringLiteral("化学"),
    QStringLiteral("生物"),
    QStringLiteral("政治"),
    QStringLiteral("历史"),
    QStringLiteral("地理"),
};

inline const QString kFreeSubjectAll = QStringLiteral("全部学科");
inline const QStringList kTaskReasons = {
    QStringLiteral("请假"),
    QStringLiteral("公差"),
};

using SubjectMap = QHash<QString, QString>;

}  // namespace substitute
