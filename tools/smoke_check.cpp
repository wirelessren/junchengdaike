#include "datastore.h"
#include "excelhelper.h"
#include "types.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <xlnt/xlnt.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

using namespace substitute;

std::string toStdString(const QString &text)
{
    return text.toUtf8().toStdString();
}

QString fromStdString(const std::string &text)
{
    return QString::fromUtf8(text.c_str()).trimmed();
}

void requireCondition(bool condition, const QString &message)
{
    if (!condition) {
        throw std::runtime_error(toStdString(message));
    }
}

std::vector<std::uint8_t> readBinaryFile(const QString &path)
{
    QFile file(path);
    requireCondition(file.open(QIODevice::ReadOnly), QStringLiteral("无法打开文件: %1").arg(path));

    const QByteArray content = file.readAll();
    return std::vector<std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(content.constData()),
        reinterpret_cast<const std::uint8_t *>(content.constData()) + content.size());
}

void loadWorkbook(xlnt::workbook &workbook, const QString &path)
{
    workbook.load(readBinaryFile(path));
}

void saveWorkbook(const xlnt::workbook &workbook, const QString &path)
{
    std::vector<std::uint8_t> content;
    workbook.save(content);
    QFile file(path);
    requireCondition(file.open(QIODevice::WriteOnly), QStringLiteral("无法创建文件: %1").arg(path));
    const qint64 size = static_cast<qint64>(content.size());
    requireCondition(
        file.write(reinterpret_cast<const char *>(content.data()), size) == size,
        QStringLiteral("写入文件失败: %1").arg(path));
}

QString cellText(xlnt::worksheet &sheet, const char *reference)
{
    const auto cell = sheet.cell(reference);
    if (!cell.has_value()) {
        return {};
    }
    return fromStdString(cell.to_string());
}

QVector<QString> sortedTeachers(const SubjectMap &subjectMap)
{
    QVector<QString> teachers = subjectMap.keys().toVector();
    std::sort(teachers.begin(), teachers.end(), [&](const QString &left, const QString &right) {
        const QString leftSubject = subjectMap.value(left);
        const QString rightSubject = subjectMap.value(right);
        if (leftSubject != rightSubject) {
            return leftSubject < rightSubject;
        }
        return left < right;
    });
    return teachers;
}

QString findSchedulePath(const QDir &rootDir)
{
    const QStringList candidates = {
        QStringLiteral("教师课表统计.xlsx"),
        QStringLiteral("教师源课表.xlsx"),
    };

    for (const QString &name : candidates) {
        const QString fullPath = rootDir.filePath(name);
        if (QFileInfo::exists(fullPath)) {
            return fullPath;
        }
    }

    throw std::runtime_error(toStdString(QStringLiteral("未找到课表样例文件。")));
}

QVector<QString> pickFreeTeachers(
    const QVector<ScheduleEntry> &entries,
    const SubjectMap &subjectMap,
    QString *slotText)
{
    const QVector<QString> teachers = sortedTeachers(subjectMap);
    requireCondition(!teachers.isEmpty(), QStringLiteral("课表没有可用老师数据。"));

    for (const QString &day : kDays) {
        for (const int period : kPeriods) {
            QSet<QString> busyTeachers;
            for (const ScheduleEntry &entry : entries) {
                if (entry.day == day && entry.period == period) {
                    busyTeachers.insert(entry.teacher);
                }
            }

            QVector<QString> freeTeachers;
            for (const QString &teacher : teachers) {
                if (!busyTeachers.contains(teacher)) {
                    freeTeachers.push_back(teacher);
                }
            }

            if (!freeTeachers.isEmpty()) {
                if (slotText) {
                    *slotText = QStringLiteral("%1第%2节").arg(day).arg(period);
                }
                if (freeTeachers.size() > 5) {
                    freeTeachers.resize(5);
                }
                return freeTeachers;
            }
        }
    }

    if (slotText) {
        *slotText = QStringLiteral("周一第1节");
    }
    return teachers.mid(0, qMin(teachers.size(), qsizetype(5)));
}

Assignment buildSampleAssignment(const QVector<ScheduleEntry> &entries, const SubjectMap &subjectMap)
{
    requireCondition(!entries.isEmpty(), QStringLiteral("没有可用于通知单导出的课表数据。"));

    const ScheduleEntry &base = entries.first();
    QString substituteTeacher;
    for (const QString &teacher : sortedTeachers(subjectMap)) {
        if (teacher != base.teacher) {
            substituteTeacher = teacher;
            break;
        }
    }

    requireCondition(!substituteTeacher.isEmpty(), QStringLiteral("至少需要两位老师才能生成通知单校验数据。"));

    Assignment assignment;
    assignment.absentTeacher = base.teacher;
    assignment.day = base.day;
    assignment.period = base.period;
    assignment.className = base.className;
    assignment.subject = base.subject;
    assignment.substituteTeacher = substituteTeacher;
    assignment.substituteSubject = subjectMap.value(substituteTeacher, base.subject);
    return assignment;
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTextStream out(stdout);
    QTextStream err(stderr);

    try {
        const QString projectRoot = argc > 1
            ? QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()
            : QDir::currentPath();
        const QString outputRoot = argc > 2
            ? QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath()
            : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("smoke-output"));

        const QDir rootDir(projectRoot);
        requireCondition(rootDir.exists(), QStringLiteral("项目目录不存在: %1").arg(projectRoot));

        const QString schedulePath = findSchedulePath(rootDir);
        const QString templatePath = rootDir.filePath(QStringLiteral("通知单模版.xlsx"));
        requireCondition(QFileInfo::exists(templatePath), QStringLiteral("通知单模板不存在: %1").arg(templatePath));

        QDir().mkpath(outputRoot);
        const QString databasePath = QDir(outputRoot).filePath(QStringLiteral("smoke.db"));
        const QString databaseBackupPath = QDir(outputRoot).filePath(QStringLiteral("smoke_backup.db"));
        const QString freeExportPath = QDir(outputRoot).filePath(QStringLiteral("free_teachers_smoke.xlsx"));
        const QString freeExportPathXls = QDir(outputRoot).filePath(QStringLiteral("free_teachers_smoke.xls"));
        const QString noticeExportPath = QDir(outputRoot).filePath(QStringLiteral("notices_smoke.xlsx"));
        const QString noticeExportPathXls = QDir(outputRoot).filePath(QStringLiteral("notices_smoke.xls"));
        const QString taskDataExportPath = QDir(outputRoot).filePath(QStringLiteral("task_data_smoke.xlsx"));
        const QString statisticsExportPath = QDir(outputRoot).filePath(QStringLiteral("substitute_statistics_smoke.xlsx"));
        const QString robustSchedulePath = QDir(outputRoot).filePath(QStringLiteral("robust_schedule_smoke.xlsx"));
        const QString invalidTemplatePath = QDir(outputRoot).filePath(QStringLiteral("invalid_template_smoke.xlsx"));

        QFile::remove(databasePath);
        QFile::remove(databaseBackupPath);
        QFile::remove(freeExportPath);
        QFile::remove(freeExportPathXls);
        QFile::remove(noticeExportPath);
        QFile::remove(noticeExportPathXls);
        QFile::remove(taskDataExportPath);
        QFile::remove(statisticsExportPath);
        QFile::remove(robustSchedulePath);
        QFile::remove(invalidTemplatePath);

        QString error;
        const bool templateValid = ExcelHelper::validateWorkbook(templatePath, &error);
        requireCondition(templateValid, QStringLiteral("模板校验失败: %1").arg(error));

        xlnt::workbook invalidTemplateWorkbook;
        xlnt::worksheet invalidTemplateSheet = invalidTemplateWorkbook.active_sheet();
        invalidTemplateSheet.cell("A1").value(toStdString(QStringLiteral("错误模板")));
        invalidTemplateSheet.cell("D17").value(toStdString(QStringLiteral("占位")));
        saveWorkbook(invalidTemplateWorkbook, invalidTemplatePath);
        error.clear();
        requireCondition(
            !ExcelHelper::validateWorkbook(invalidTemplatePath, &error) && !error.isEmpty(),
            QStringLiteral("无效通知单模板未被拒绝。"));

        xlnt::workbook robustScheduleWorkbook;
        xlnt::worksheet coverSheet = robustScheduleWorkbook.active_sheet();
        coverSheet.title(toStdString(QStringLiteral("说明")));
        coverSheet.cell("A1").value(toStdString(QStringLiteral("课表导入说明页")));
        xlnt::worksheet robustDataSheet = robustScheduleWorkbook.create_sheet();
        robustDataSheet.title(toStdString(QStringLiteral("课表数据")));
        const QStringList robustHeaders = {
            QStringLiteral("课程名称"),
            QStringLiteral("教师姓名"),
            QStringLiteral("星期几"),
            QStringLiteral("第几节"),
            QStringLiteral("教学班"),
        };
        for (int columnIndex = 0; columnIndex < robustHeaders.size(); ++columnIndex) {
            robustDataSheet.cell(xlnt::column_t(columnIndex + 1), 3)
                .value(toStdString(robustHeaders.at(columnIndex)));
        }
        const QStringList validScheduleRow = {
            QStringLiteral("数学"),
            QStringLiteral("测试教师"),
            QStringLiteral("星期一"),
            QStringLiteral("第2节"),
            QStringLiteral("测试班"),
        };
        for (int rowIndex : {4, 5}) {
            for (int columnIndex = 0; columnIndex < validScheduleRow.size(); ++columnIndex) {
                robustDataSheet.cell(xlnt::column_t(columnIndex + 1), static_cast<xlnt::row_t>(rowIndex))
                    .value(toStdString(validScheduleRow.at(columnIndex)));
            }
        }
        for (int columnIndex = 0; columnIndex < validScheduleRow.size(); ++columnIndex) {
            QString value = validScheduleRow.at(columnIndex);
            if (columnIndex == 3) {
                value = QStringLiteral("第31节");
            }
            robustDataSheet.cell(xlnt::column_t(columnIndex + 1), 6).value(toStdString(value));
        }
        const QStringList extendedScheduleRow = {
            QStringLiteral("物理"),
            QStringLiteral("周末教师"),
            QStringLiteral("星期六"),
            QStringLiteral("第10节"),
            QStringLiteral("周末班"),
        };
        for (int columnIndex = 0; columnIndex < extendedScheduleRow.size(); ++columnIndex) {
            robustDataSheet.cell(xlnt::column_t(columnIndex + 1), 7)
                .value(toStdString(extendedScheduleRow.at(columnIndex)));
        }
        saveWorkbook(robustScheduleWorkbook, robustSchedulePath);
        QString robustWarning;
        error.clear();
        const QVector<ScheduleEntry> robustSchedule =
            ExcelHelper::readSchedule(robustSchedulePath, &error, &robustWarning);
        requireCondition(
            error.isEmpty() && robustSchedule.size() == 2
                && robustSchedule.last().day == QStringLiteral("周六")
                && robustSchedule.last().period == 10,
            QStringLiteral("健壮课表导入校验失败: %1").arg(error));
        requireCondition(
            robustWarning.contains(QStringLiteral("第 3 行"))
                && robustWarning.contains(QStringLiteral("重复"))
                && robustWarning.contains(QStringLiteral("跳过")),
            QStringLiteral("课表导入警告信息不完整: %1").arg(robustWarning));

        const QVector<ScheduleEntry> schedule = ExcelHelper::readSchedule(schedulePath, &error);
        requireCondition(
            !schedule.isEmpty(),
            QStringLiteral("课表导入失败: %1").arg(error.isEmpty() ? QStringLiteral("未读取到数据") : error));

        DataStore store(databasePath);
        const bool coreTablesReady = store.ensureCoreTables(&error);
        requireCondition(coreTablesReady, QStringLiteral("建表失败: %1").arg(error));
        const bool importOk = store.importSchedule(schedule, &error);
        requireCondition(importOk, QStringLiteral("课表写库失败: %1").arg(error));

        error.clear();
        const QVector<ScheduleEntry> imported = store.loadSchedule(&error);
        requireCondition(error.isEmpty(), QStringLiteral("课表回读失败: %1").arg(error));
        requireCondition(
            imported.size() == schedule.size(),
            QStringLiteral("课表回读条数不一致: 读取 %1，写入 %2").arg(imported.size()).arg(schedule.size()));

        const SubjectMap subjectMap = store.buildSubjectMap(imported);
        requireCondition(!subjectMap.isEmpty(), QStringLiteral("未能构建老师学科映射。"));

        QString slotText;
        const QVector<QString> freeTeachers = pickFreeTeachers(imported, subjectMap, &slotText);
        requireCondition(!freeTeachers.isEmpty(), QStringLiteral("未找到可导出的无课老师。"));
        const bool freeExportOk =
            ExcelHelper::exportFreeTeachers(freeExportPath, freeTeachers, subjectMap, slotText, &error);
        requireCondition(freeExportOk, QStringLiteral("无课名单导出失败: %1").arg(error));
        const bool freeExportXlsOk =
            ExcelHelper::exportFreeTeachers(freeExportPathXls, freeTeachers, subjectMap, slotText, &error);
        requireCondition(freeExportXlsOk, QStringLiteral("无课名单 xls 导出失败: %1").arg(error));

        xlnt::workbook freeWorkbook;
        loadWorkbook(freeWorkbook, freeExportPath);
        xlnt::worksheet freeSheet = freeWorkbook.active_sheet();
        requireCondition(
            cellText(freeSheet, "A1") == QStringLiteral("无课老师名单"),
            QStringLiteral("无课名单导出结果校验失败。"));
        QFile freeXlsFile(freeExportPathXls);
        requireCondition(freeXlsFile.open(QIODevice::ReadOnly), QStringLiteral("无法读取无课名单 xls 文件。"));
        const QString freeXlsText = QString::fromUtf8(freeXlsFile.readAll());
        requireCondition(
            freeXlsText.contains(QStringLiteral("无课老师名单"))
                && freeXlsText.contains(QStringLiteral("progid=\"Excel.Sheet\"")),
            QStringLiteral("无课名单 xls 内容校验失败。"));

        const QVector<Assignment> assignments = {buildSampleAssignment(imported, subjectMap)};
        TaskDetail sampleTask;
        sampleTask.summary.name = QStringLiteral("冒烟校验代课任务");
        sampleTask.summary.startDate = QStringLiteral("2026-03-30");
        sampleTask.summary.endDate = QStringLiteral("2026-03-31");
        sampleTask.summary.reason = QStringLiteral("请假");
        sampleTask.assignments = assignments;
        qint64 savedTaskId = 0;
        const bool taskSaved = store.saveTask(sampleTask, 0, &savedTaskId, &error);
        requireCondition(taskSaved && savedTaskId > 0, QStringLiteral("测试任务保存失败: %1").arg(error));

        const bool restoreProbeSaved =
            store.setSetting(QStringLiteral("restore_probe"), QStringLiteral("backup-value"), &error);
        requireCondition(restoreProbeSaved, QStringLiteral("恢复校验标记保存失败: %1").arg(error));
        const bool backupOk = store.backupDatabase(databaseBackupPath, &error);
        requireCondition(backupOk, QStringLiteral("数据库备份失败: %1").arg(error));
        requireCondition(
            QFileInfo(databaseBackupPath).exists() && QFileInfo(databaseBackupPath).size() > 0,
            QStringLiteral("数据库备份文件无效。"));

        const bool restoreProbeChanged =
            store.setSetting(QStringLiteral("restore_probe"), QStringLiteral("changed-value"), &error);
        requireCondition(restoreProbeChanged, QStringLiteral("恢复校验标记修改失败: %1").arg(error));
        const bool restoreOk = store.restoreDatabase(databaseBackupPath, &error);
        requireCondition(restoreOk, QStringLiteral("数据库恢复失败: %1").arg(error));
        const QString restoredProbe = store.getSetting(QStringLiteral("restore_probe"), &error);
        requireCondition(
            error.isEmpty() && restoredProbe == QStringLiteral("backup-value"),
            QStringLiteral("数据库恢复内容校验失败: %1").arg(error));

        const auto savedTask = store.loadTask(savedTaskId, &error);
        requireCondition(savedTask.has_value(), QStringLiteral("测试任务回读失败: %1").arg(error));
        QVector<TaskDetail> taskData = {*savedTask};
        const bool taskDataExportOk =
            ExcelHelper::exportTaskData(taskDataExportPath, taskData, &error);
        requireCondition(taskDataExportOk, QStringLiteral("代课数据导出失败: %1").arg(error));

        xlnt::workbook taskDataWorkbook;
        loadWorkbook(taskDataWorkbook, taskDataExportPath);
        requireCondition(taskDataWorkbook.sheet_count() == 2, QStringLiteral("代课数据工作表数量异常。"));
        xlnt::worksheet taskListSheet = taskDataWorkbook.sheet_by_title(toStdString(QStringLiteral("任务列表")));
        xlnt::worksheet taskDetailSheet = taskDataWorkbook.sheet_by_title(toStdString(QStringLiteral("代课明细")));
        requireCondition(cellText(taskListSheet, "B2") == sampleTask.summary.name, QStringLiteral("任务列表导出校验失败。"));
        requireCondition(
            cellText(taskDetailSheet, "C2") == assignments.first().absentTeacher,
            QStringLiteral("代课明细导出校验失败。"));

        const bool statisticsExportOk = ExcelHelper::exportSubstituteStatistics(
            statisticsExportPath,
            taskData,
            QStringLiteral("全部任务"),
            &error);
        requireCondition(statisticsExportOk, QStringLiteral("代课统计导出失败: %1").arg(error));
        xlnt::workbook statisticsWorkbook;
        loadWorkbook(statisticsWorkbook, statisticsExportPath);
        requireCondition(statisticsWorkbook.sheet_count() == 2, QStringLiteral("代课统计工作表数量异常。"));
        xlnt::worksheet statisticsSheet =
            statisticsWorkbook.sheet_by_title(toStdString(QStringLiteral("代课统计")));
        xlnt::worksheet statisticsDetailSheet =
            statisticsWorkbook.sheet_by_title(toStdString(QStringLiteral("代课明细")));
        requireCondition(
            cellText(statisticsSheet, "B5") == assignments.first().substituteTeacher,
            QStringLiteral("代课教师统计结果校验失败。"));
        requireCondition(
            cellText(statisticsDetailSheet, "K2") == assignments.first().substituteTeacher,
            QStringLiteral("代课统计明细校验失败。"));

        const bool noticeExportOk = ExcelHelper::exportNotices(
            templatePath,
            noticeExportPath,
            assignments,
            QStringLiteral("公差"),
            QStringLiteral("2026-03-30"),
            QStringLiteral("2026-03-31"),
            &error);
        requireCondition(noticeExportOk, QStringLiteral("通知单导出失败: %1").arg(error));
        const bool noticeExportXlsOk = ExcelHelper::exportNotices(
            templatePath,
            noticeExportPathXls,
            assignments,
            QStringLiteral("公差"),
            QStringLiteral("2026-03-30"),
            QStringLiteral("2026-03-31"),
            &error);
        requireCondition(noticeExportXlsOk, QStringLiteral("通知单 xls 导出失败: %1").arg(error));

        xlnt::workbook noticeWorkbook;
        loadWorkbook(noticeWorkbook, noticeExportPath);
        requireCondition(
            noticeWorkbook.sheet_count() == 1,
            QStringLiteral("通知单工作簿工作表数量异常。"));
        xlnt::worksheet firstSheet = noticeWorkbook.active_sheet();
        requireCondition(
            cellText(firstSheet, "A1").contains(QStringLiteral("老师调代课通知")),
            QStringLiteral("通知单标题校验失败。"));
        requireCondition(
            cellText(firstSheet, "A10").contains(QStringLiteral("老师调代课通知")),
            QStringLiteral("通知单第二块标题校验失败。"));
        requireCondition(
            cellText(firstSheet, "B6").startsWith(QStringLiteral("代课时间:")),
            QStringLiteral("通知单首块时间行校验失败。"));
        requireCondition(
            cellText(firstSheet, "B15").startsWith(QStringLiteral("代课时间:")),
            QStringLiteral("通知单第二块时间行校验失败。"));
        QFile noticeXlsFile(noticeExportPathXls);
        requireCondition(noticeXlsFile.open(QIODevice::ReadOnly), QStringLiteral("无法读取通知单 xls 文件。"));
        const QString noticeXlsText = QString::fromUtf8(noticeXlsFile.readAll());
        requireCondition(
            noticeXlsText.contains(QStringLiteral("老师调代课通知"))
                && noticeXlsText.contains(QStringLiteral("progid=\"Excel.Sheet\"")),
            QStringLiteral("通知单 xls 内容校验失败。"));

        out << "Smoke validation passed.\n";
        out << "Schedule entries: " << schedule.size() << '\n';
        out << "Imported entries: " << imported.size() << '\n';
        out << "Free teachers exported: " << freeTeachers.size() << '\n';
        out << "Template: " << templatePath << '\n';
        out << "Free export: " << freeExportPath << '\n';
        out << "Free export xls: " << freeExportPathXls << '\n';
        out << "Notice export: " << noticeExportPath << '\n';
        out << "Notice export xls: " << noticeExportPathXls << '\n';
        out << "Database backup: " << databaseBackupPath << '\n';
        out << "Task data export: " << taskDataExportPath << '\n';
        out << "Substitute statistics: " << statisticsExportPath << '\n';
        return 0;
    } catch (const std::exception &ex) {
        err << "Smoke validation failed: " << QString::fromLocal8Bit(ex.what()) << '\n';
        return 1;
    }
}
