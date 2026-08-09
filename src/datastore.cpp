#include "datastore.h"

#include "coreutils.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace substitute {

namespace {

QString lastErrorText(const QSqlQuery &query)
{
    return query.lastError().text();
}

QString lastErrorText(const QSqlDatabase &db)
{
    return db.lastError().text();
}

}  // namespace

DataStore::DataStore(QString dbPath)
    : m_dbPath(std::move(dbPath))
    , m_connectionName(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_db(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName))
{
    m_db.setDatabaseName(m_dbPath);
    m_db.open();
    QSqlQuery pragmaQuery(m_db);
    pragmaQuery.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
}

DataStore::~DataStore()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool DataStore::execStatement(const QString &sql, QString *error) const
{
    QSqlQuery query(m_db);
    if (query.exec(sql)) {
        return true;
    }
    if (error) {
        *error = lastErrorText(query);
    }
    return false;
}

bool DataStore::ensureCoreTables(QString *error)
{
    if (!m_db.isOpen()) {
        if (error) {
            *error = lastErrorText(m_db);
        }
        return false;
    }

    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schedule ("
            "科目 TEXT, 姓名 TEXT, 周几 TEXT, 节次 INTEGER, 班级 TEXT)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_schedule_day_period "
            "ON schedule(周几, 节次)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_schedule_teacher "
            "ON schedule(姓名)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tasks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL UNIQUE,"
            "start_date TEXT,"
            "end_date TEXT,"
            "reason TEXT,"
            "base_task_id INTEGER,"
            "created_at TEXT,"
            "updated_at TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS task_assignments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "task_id INTEGER NOT NULL,"
            "absent_teacher TEXT,"
            "day TEXT,"
            "period INTEGER,"
            "class_name TEXT,"
            "subject TEXT,"
            "sub_teacher TEXT,"
            "sub_subject TEXT,"
            "FOREIGN KEY(task_id) REFERENCES tasks(id) ON DELETE CASCADE)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS app_settings ("
            "key TEXT PRIMARY KEY,"
            "value TEXT)")
    };

    for (const QString &statement : statements) {
        if (!execStatement(statement, error)) {
            return false;
        }
    }

    QSqlQuery tableInfoQuery(m_db);
    if (!tableInfoQuery.exec(QStringLiteral("PRAGMA table_info(tasks)"))) {
        if (error) {
            *error = lastErrorText(tableInfoQuery);
        }
        return false;
    }

    bool hasBaseTaskId = false;
    bool hasReason = false;
    while (tableInfoQuery.next()) {
        const QString name = tableInfoQuery.value(1).toString();
        hasBaseTaskId = hasBaseTaskId || name == QStringLiteral("base_task_id");
        hasReason = hasReason || name == QStringLiteral("reason");
    }

    if (!hasBaseTaskId
        && !execStatement(QStringLiteral("ALTER TABLE tasks ADD COLUMN base_task_id INTEGER"), error)) {
        return false;
    }
    if (!hasReason
        && !execStatement(QStringLiteral("ALTER TABLE tasks ADD COLUMN reason TEXT"), error)) {
        return false;
    }

    if (!execStatement(
            QStringLiteral(
                "UPDATE tasks SET reason = CASE "
                "WHEN name LIKE '%请假%' THEN '请假' "
                "WHEN name LIKE '%公差%' THEN '公差' "
                "ELSE '公差' END "
                "WHERE COALESCE(reason, '') = ''"),
            error)) {
        return false;
    }

    return true;
}

bool DataStore::importSchedule(const QVector<ScheduleEntry> &entries, QString *error)
{
    if (!ensureCoreTables(error)) {
        return false;
    }

    if (!m_db.transaction()) {
        if (error) {
            *error = lastErrorText(m_db);
        }
        return false;
    }

    QSqlQuery clearQuery(m_db);
    if (!clearQuery.exec(QStringLiteral("DELETE FROM schedule"))) {
        if (error) {
            *error = lastErrorText(clearQuery);
        }
        m_db.rollback();
        return false;
    }

    QSqlQuery insertQuery(m_db);
    insertQuery.prepare(
        QStringLiteral(
            "INSERT INTO schedule (科目, 姓名, 周几, 节次, 班级) "
            "VALUES (?, ?, ?, ?, ?)"));

    for (const ScheduleEntry &entry : entries) {
        insertQuery.addBindValue(entry.subject);
        insertQuery.addBindValue(entry.teacher);
        insertQuery.addBindValue(entry.day);
        insertQuery.addBindValue(entry.period);
        insertQuery.addBindValue(entry.className);
        if (!insertQuery.exec()) {
            if (error) {
                *error = lastErrorText(insertQuery);
            }
            m_db.rollback();
            return false;
        }
        insertQuery.finish();
    }

    if (!m_db.commit()) {
        if (error) {
            *error = lastErrorText(m_db);
        }
        return false;
    }
    return true;
}

QVector<ScheduleEntry> DataStore::loadSchedule(QString *error) const
{
    QVector<ScheduleEntry> entries;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT 科目, 姓名, 周几, 节次, 班级 FROM schedule"))) {
        if (error) {
            *error = lastErrorText(query);
        }
        return entries;
    }

    while (query.next()) {
        ScheduleEntry entry;
        entry.subject = query.value(0).toString().trimmed();
        entry.teacher = query.value(1).toString().trimmed();
        entry.day = query.value(2).toString().trimmed();
        entry.period = query.value(3).toInt();
        entry.className = query.value(4).toString().trimmed();
        entries.push_back(entry);
    }
    return entries;
}

SubjectMap DataStore::buildSubjectMap(const QVector<ScheduleEntry> &entries) const
{
    QHash<QString, QHash<QString, int>> counter;
    for (const ScheduleEntry &entry : entries) {
        if (!entry.teacher.isEmpty() && !entry.subject.isEmpty()) {
            counter[entry.teacher][entry.subject] += 1;
        }
    }

    SubjectMap map;
    for (auto teacherIt = counter.cbegin(); teacherIt != counter.cend(); ++teacherIt) {
        QString bestSubject;
        int bestCount = -1;
        for (auto subjectIt = teacherIt->cbegin(); subjectIt != teacherIt->cend(); ++subjectIt) {
            if (subjectIt.value() > bestCount
                || (subjectIt.value() == bestCount && subjectIt.key() < bestSubject)) {
                bestSubject = subjectIt.key();
                bestCount = subjectIt.value();
            }
        }
        if (!bestSubject.isEmpty()) {
            map.insert(teacherIt.key(), bestSubject);
        }
    }
    return map;
}

QString DataStore::getSetting(const QString &key, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT value FROM app_settings WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec()) {
        if (error) {
            *error = lastErrorText(query);
        }
        return {};
    }
    return query.next() ? query.value(0).toString() : QString();
}

bool DataStore::setSetting(const QString &key, const QString &value, QString *error)
{
    if (!ensureCoreTables(error)) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("INSERT OR REPLACE INTO app_settings (key, value) VALUES (?, ?)"));
    query.addBindValue(key);
    query.addBindValue(value);
    if (query.exec()) {
        return true;
    }
    if (error) {
        *error = lastErrorText(query);
    }
    return false;
}

QVector<TaskSummary> DataStore::loadTasks(QString *error) const
{
    QVector<TaskSummary> tasks;
    QSqlQuery query(m_db);
    if (!query.exec(
            QStringLiteral(
                "SELECT id, name, start_date, end_date, updated_at, base_task_id, reason "
                "FROM tasks ORDER BY updated_at DESC, id DESC"))) {
        if (error) {
            *error = lastErrorText(query);
        }
        return tasks;
    }

    while (query.next()) {
        TaskSummary task;
        task.id = query.value(0).toLongLong();
        task.name = query.value(1).toString();
        task.startDate = query.value(2).toString();
        task.endDate = query.value(3).toString();
        task.updatedAt = query.value(4).toString();
        task.baseTaskId = query.value(5).isNull() ? 0 : query.value(5).toLongLong();
        task.reason = normalizeTaskReason(query.value(6).toString(), task.name);
        tasks.push_back(task);
    }
    return tasks;
}

QVector<Assignment> DataStore::loadAssignmentsForTask(qint64 taskId, QString *error) const
{
    QVector<Assignment> rows;
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral(
            "SELECT absent_teacher, day, period, class_name, subject, sub_teacher, sub_subject "
            "FROM task_assignments WHERE task_id = ? ORDER BY day, period"));
    query.addBindValue(taskId);
    if (!query.exec()) {
        if (error) {
            *error = lastErrorText(query);
        }
        return rows;
    }

    while (query.next()) {
        Assignment row;
        row.absentTeacher = query.value(0).toString();
        row.day = query.value(1).toString();
        row.period = query.value(2).toInt();
        row.className = query.value(3).toString();
        row.subject = query.value(4).toString();
        row.substituteTeacher = query.value(5).toString();
        row.substituteSubject = query.value(6).toString();
        rows.push_back(row);
    }
    return rows;
}

std::optional<TaskDetail> DataStore::loadTask(qint64 taskId, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral(
            "SELECT id, name, start_date, end_date, reason, base_task_id "
            "FROM tasks WHERE id = ?"));
    query.addBindValue(taskId);
    if (!query.exec()) {
        if (error) {
            *error = lastErrorText(query);
        }
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    TaskDetail detail;
    detail.summary.id = query.value(0).toLongLong();
    detail.summary.name = query.value(1).toString();
    detail.summary.startDate = query.value(2).toString();
    detail.summary.endDate = query.value(3).toString();
    detail.summary.reason = normalizeTaskReason(query.value(4).toString(), detail.summary.name);
    detail.summary.baseTaskId = query.value(5).isNull() ? 0 : query.value(5).toLongLong();
    detail.assignments = loadAssignmentsForTask(taskId, error);
    if (error && !error->isEmpty()) {
        return std::nullopt;
    }
    return detail;
}

QVector<Assignment> DataStore::loadInheritedAssignments(qint64 baseTaskId, QString *error) const
{
    QVector<Assignment> assignments;
    if (baseTaskId <= 0) {
        return assignments;
    }

    QVector<qint64> chain;
    QSet<qint64> visited;
    qint64 currentId = baseTaskId;
    while (currentId > 0 && !visited.contains(currentId)) {
        visited.insert(currentId);
        chain.push_back(currentId);

        QSqlQuery parentQuery(m_db);
        parentQuery.prepare(QStringLiteral("SELECT base_task_id FROM tasks WHERE id = ?"));
        parentQuery.addBindValue(currentId);
        if (!parentQuery.exec()) {
            if (error) {
                *error = lastErrorText(parentQuery);
            }
            return {};
        }
        if (!parentQuery.next() || parentQuery.value(0).isNull()) {
            break;
        }
        currentId = parentQuery.value(0).toLongLong();
    }

    std::reverse(chain.begin(), chain.end());
    for (qint64 taskId : chain) {
        const QVector<Assignment> taskAssignments = loadAssignmentsForTask(taskId, error);
        if (error && !error->isEmpty()) {
            return {};
        }
        assignments = mergeAssignments(assignments, taskAssignments);
    }
    return assignments;
}

bool DataStore::saveTask(
    const TaskDetail &detail,
    qint64 currentTaskId,
    qint64 *savedTaskId,
    QString *error)
{
    if (!ensureCoreTables(error)) {
        return false;
    }
    if (!m_db.transaction()) {
        if (error) {
            *error = lastErrorText(m_db);
        }
        return false;
    }

    qint64 taskId = currentTaskId;
    qint64 existingId = 0;
    qint64 existingBaseTaskId = 0;

    QSqlQuery existingQuery(m_db);
    existingQuery.prepare(QStringLiteral("SELECT id, base_task_id FROM tasks WHERE name = ?"));
    existingQuery.addBindValue(detail.summary.name);
    if (!existingQuery.exec()) {
        if (error) {
            *error = lastErrorText(existingQuery);
        }
        m_db.rollback();
        return false;
    }
    if (existingQuery.next()) {
        existingId = existingQuery.value(0).toLongLong();
        existingBaseTaskId = existingQuery.value(1).isNull() ? 0 : existingQuery.value(1).toLongLong();
    }

    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QVariant baseTaskValue;
    const qint64 requestedBaseTaskId = detail.summary.baseTaskId > 0
        ? detail.summary.baseTaskId
        : existingBaseTaskId;
    if (requestedBaseTaskId > 0) {
        baseTaskValue = requestedBaseTaskId;
    }

    if (currentTaskId > 0) {
        if (existingId > 0 && existingId != currentTaskId) {
            if (error) {
                *error = QStringLiteral("任务名称已存在，请使用其他名称。");
            }
            m_db.rollback();
            return false;
        }
        QSqlQuery updateQuery(m_db);
        updateQuery.prepare(
            QStringLiteral(
                "UPDATE tasks SET name = ?, start_date = ?, end_date = ?, reason = ?, "
                "base_task_id = ?, updated_at = ? WHERE id = ?"));
        updateQuery.addBindValue(detail.summary.name);
        updateQuery.addBindValue(detail.summary.startDate);
        updateQuery.addBindValue(detail.summary.endDate);
        updateQuery.addBindValue(normalizeTaskReason(detail.summary.reason, detail.summary.name));
        updateQuery.addBindValue(baseTaskValue);
        updateQuery.addBindValue(now);
        updateQuery.addBindValue(currentTaskId);
        if (!updateQuery.exec()) {
            if (error) {
                *error = lastErrorText(updateQuery);
            }
            m_db.rollback();
            return false;
        }
        taskId = currentTaskId;
    } else if (existingId > 0) {
        QSqlQuery updateQuery(m_db);
        updateQuery.prepare(
            QStringLiteral(
                "UPDATE tasks SET start_date = ?, end_date = ?, reason = ?, "
                "base_task_id = ?, updated_at = ? WHERE id = ?"));
        updateQuery.addBindValue(detail.summary.startDate);
        updateQuery.addBindValue(detail.summary.endDate);
        updateQuery.addBindValue(normalizeTaskReason(detail.summary.reason, detail.summary.name));
        updateQuery.addBindValue(baseTaskValue);
        updateQuery.addBindValue(now);
        updateQuery.addBindValue(existingId);
        if (!updateQuery.exec()) {
            if (error) {
                *error = lastErrorText(updateQuery);
            }
            m_db.rollback();
            return false;
        }
        taskId = existingId;
    } else {
        QSqlQuery insertQuery(m_db);
        insertQuery.prepare(
            QStringLiteral(
                "INSERT INTO tasks (name, start_date, end_date, reason, base_task_id, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)"));
        insertQuery.addBindValue(detail.summary.name);
        insertQuery.addBindValue(detail.summary.startDate);
        insertQuery.addBindValue(detail.summary.endDate);
        insertQuery.addBindValue(normalizeTaskReason(detail.summary.reason, detail.summary.name));
        insertQuery.addBindValue(baseTaskValue);
        insertQuery.addBindValue(now);
        insertQuery.addBindValue(now);
        if (!insertQuery.exec()) {
            if (error) {
                *error = lastErrorText(insertQuery);
            }
            m_db.rollback();
            return false;
        }
        taskId = insertQuery.lastInsertId().toLongLong();
    }

    QSqlQuery deleteAssignmentsQuery(m_db);
    deleteAssignmentsQuery.prepare(QStringLiteral("DELETE FROM task_assignments WHERE task_id = ?"));
    deleteAssignmentsQuery.addBindValue(taskId);
    if (!deleteAssignmentsQuery.exec()) {
        if (error) {
            *error = lastErrorText(deleteAssignmentsQuery);
        }
        m_db.rollback();
        return false;
    }

    QSqlQuery insertAssignmentQuery(m_db);
    insertAssignmentQuery.prepare(
        QStringLiteral(
            "INSERT INTO task_assignments "
            "(task_id, absent_teacher, day, period, class_name, subject, sub_teacher, sub_subject) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const Assignment &assignment : detail.assignments) {
        insertAssignmentQuery.addBindValue(taskId);
        insertAssignmentQuery.addBindValue(assignment.absentTeacher);
        insertAssignmentQuery.addBindValue(assignment.day);
        insertAssignmentQuery.addBindValue(assignment.period);
        insertAssignmentQuery.addBindValue(assignment.className);
        insertAssignmentQuery.addBindValue(assignment.subject);
        insertAssignmentQuery.addBindValue(assignment.substituteTeacher);
        insertAssignmentQuery.addBindValue(assignment.substituteSubject);
        if (!insertAssignmentQuery.exec()) {
            if (error) {
                *error = lastErrorText(insertAssignmentQuery);
            }
            m_db.rollback();
            return false;
        }
        insertAssignmentQuery.finish();
    }

    if (!m_db.commit()) {
        if (error) {
            *error = lastErrorText(m_db);
        }
        return false;
    }

    if (savedTaskId) {
        *savedTaskId = taskId;
    }
    return true;
}

bool DataStore::deleteTask(qint64 taskId, QString *error)
{
    if (!m_db.transaction()) {
        if (error) {
            *error = lastErrorText(m_db);
        }
        return false;
    }

    QSqlQuery deleteTaskQuery(m_db);
    deleteTaskQuery.prepare(QStringLiteral("DELETE FROM tasks WHERE id = ?"));
    deleteTaskQuery.addBindValue(taskId);
    if (!deleteTaskQuery.exec()) {
        if (error) {
            *error = lastErrorText(deleteTaskQuery);
        }
        m_db.rollback();
        return false;
    }

    QSqlQuery resetChildQuery(m_db);
    resetChildQuery.prepare(QStringLiteral("UPDATE tasks SET base_task_id = NULL WHERE base_task_id = ?"));
    resetChildQuery.addBindValue(taskId);
    if (!resetChildQuery.exec()) {
        if (error) {
            *error = lastErrorText(resetChildQuery);
        }
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        if (error) {
            *error = lastErrorText(m_db);
        }
        return false;
    }
    return true;
}

bool DataStore::backupDatabase(const QString &outputPath, QString *error) const
{
    const QString sourcePath = QFileInfo(m_dbPath).absoluteFilePath();
    const QString targetPath = QFileInfo(outputPath).absoluteFilePath();
    if (sourcePath.compare(targetPath, Qt::CaseInsensitive) == 0) {
        if (error) {
            *error = QStringLiteral("备份文件不能覆盖当前数据库。");
        }
        return false;
    }

    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
        if (error) {
            *error = QStringLiteral("无法覆盖已有备份文件。");
        }
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("VACUUM INTO ?"));
    query.addBindValue(targetPath);
    if (query.exec()) {
        return true;
    }
    if (error) {
        *error = lastErrorText(query);
    }
    return false;
}

bool DataStore::restoreDatabase(const QString &backupPath, QString *error)
{
    const QString sourcePath = QFileInfo(backupPath).absoluteFilePath();
    const QString targetPath = QFileInfo(m_dbPath).absoluteFilePath();
    if (!QFileInfo::exists(sourcePath)) {
        if (error) {
            *error = QStringLiteral("备份文件不存在。");
        }
        return false;
    }
    if (sourcePath.compare(targetPath, Qt::CaseInsensitive) == 0) {
        if (error) {
            *error = QStringLiteral("请选择当前数据库之外的备份文件。");
        }
        return false;
    }

    const QString validationConnection =
        QStringLiteral("restore_validation_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString validationError;
    bool backupValid = false;
    {
        QSqlDatabase backupDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), validationConnection);
        backupDb.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        backupDb.setDatabaseName(sourcePath);
        if (!backupDb.open()) {
            validationError = backupDb.lastError().text();
        } else {
            QSqlQuery integrityQuery(backupDb);
            if (!integrityQuery.exec(QStringLiteral("PRAGMA integrity_check"))
                || !integrityQuery.next()
                || integrityQuery.value(0).toString().compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
                validationError = QStringLiteral("备份数据库完整性校验失败。");
            } else {
                QSet<QString> tables;
                QSqlQuery tableQuery(backupDb);
                if (tableQuery.exec(
                        QStringLiteral(
                            "SELECT name FROM sqlite_master WHERE type = 'table' "
                            "AND name IN ('schedule', 'tasks', 'task_assignments')"))) {
                    while (tableQuery.next()) {
                        tables.insert(tableQuery.value(0).toString());
                    }
                }
                backupValid = tables.contains(QStringLiteral("schedule"))
                    && tables.contains(QStringLiteral("tasks"))
                    && tables.contains(QStringLiteral("task_assignments"));
                if (!backupValid) {
                    validationError = QStringLiteral("所选文件不是有效的代课数据备份。");
                }
            }
            backupDb.close();
        }
    }
    QSqlDatabase::removeDatabase(validationConnection);
    if (!backupValid) {
        if (error) {
            *error = validationError;
        }
        return false;
    }

    m_db.close();
    const QString rollbackPath = targetPath + QStringLiteral(".restore_rollback");
    QFile::remove(rollbackPath);
    const bool hadCurrentDatabase = QFileInfo::exists(targetPath);
    if (hadCurrentDatabase && !QFile::copy(targetPath, rollbackPath)) {
        m_db.open();
        if (error) {
            *error = QStringLiteral("无法创建恢复过程的安全副本。");
        }
        return false;
    }

    bool copied = false;
    QFile sourceFile(sourcePath);
    QSaveFile targetFile(targetPath);
    if (sourceFile.open(QIODevice::ReadOnly) && targetFile.open(QIODevice::WriteOnly)) {
        copied = true;
        while (!sourceFile.atEnd()) {
            const QByteArray chunk = sourceFile.read(1024 * 1024);
            if (chunk.isEmpty() && sourceFile.error() != QFileDevice::NoError) {
                copied = false;
                break;
            }
            if (targetFile.write(chunk) != chunk.size()) {
                copied = false;
                break;
            }
        }
        if (copied) {
            copied = targetFile.commit();
        } else {
            targetFile.cancelWriting();
        }
    }
    sourceFile.close();

    auto reopenDatabase = [this]() {
        if (!m_db.open()) {
            return false;
        }
        QSqlQuery pragmaQuery(m_db);
        return pragmaQuery.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    };

    if (!copied || !reopenDatabase()) {
        m_db.close();
        if (hadCurrentDatabase && QFileInfo::exists(rollbackPath)) {
            QFile::remove(targetPath);
            QFile::rename(rollbackPath, targetPath);
        }
        reopenDatabase();
        QFile::remove(rollbackPath);
        if (error) {
            *error = copied
                ? QStringLiteral("恢复后无法重新打开数据库，已回退到原数据。")
                : QStringLiteral("复制备份数据失败，原数据未被修改。");
        }
        return false;
    }

    QString migrationError;
    if (!ensureCoreTables(&migrationError)) {
        m_db.close();
        if (hadCurrentDatabase && QFileInfo::exists(rollbackPath)) {
            QFile::remove(targetPath);
            QFile::rename(rollbackPath, targetPath);
        }
        reopenDatabase();
        QFile::remove(rollbackPath);
        if (error) {
            *error = QStringLiteral("备份数据库升级失败，已回退到原数据：%1").arg(migrationError);
        }
        return false;
    }

    QFile::remove(rollbackPath);
    return true;
}

QString DataStore::databasePath() const
{
    return m_dbPath;
}

}  // namespace substitute
