#pragma once

#include "types.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <optional>

namespace substitute {

class DataStore
{
public:
    explicit DataStore(QString dbPath);
    ~DataStore();

    DataStore(const DataStore &) = delete;
    DataStore &operator=(const DataStore &) = delete;

    bool ensureCoreTables(QString *error = nullptr);
    bool importSchedule(const QVector<ScheduleEntry> &entries, QString *error = nullptr);
    QVector<ScheduleEntry> loadSchedule(QString *error = nullptr) const;
    SubjectMap buildSubjectMap(const QVector<ScheduleEntry> &entries) const;

    QString getSetting(const QString &key, QString *error = nullptr) const;
    bool setSetting(const QString &key, const QString &value, QString *error = nullptr);

    QVector<TaskSummary> loadTasks(QString *error = nullptr) const;
    std::optional<TaskDetail> loadTask(qint64 taskId, QString *error = nullptr) const;
    QVector<Assignment> loadInheritedAssignments(qint64 baseTaskId, QString *error = nullptr) const;

    bool saveTask(
        const TaskDetail &detail,
        qint64 currentTaskId,
        qint64 *savedTaskId,
        QString *error = nullptr);
    bool deleteTask(qint64 taskId, QString *error = nullptr);
    bool backupDatabase(const QString &outputPath, QString *error = nullptr) const;
    bool restoreDatabase(const QString &backupPath, QString *error = nullptr);

    QString databasePath() const;

private:
    QVector<Assignment> loadAssignmentsForTask(qint64 taskId, QString *error = nullptr) const;
    bool execStatement(const QString &sql, QString *error = nullptr) const;

    QString m_dbPath;
    QString m_connectionName;
    mutable QSqlDatabase m_db;
};

}  // namespace substitute
