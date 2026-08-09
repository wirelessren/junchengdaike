#pragma once

#include "datastore.h"
#include "types.h"

#include <QPair>
#include <QPoint>
#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QComboBox;
class QCheckBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QSplitter;
class QTableWidget;
class QTabWidget;
QT_END_NAMESPACE

namespace substitute {

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void buildTaskTab();
    void buildEditTab();
    void buildFreeTab();
    void buildSettingsTab();
    void buildAboutTab();

    void initializeDefaultFiles();
    bool loadFromPath(const QString &path);
    void loadFromDatabase();
    void applySchedule(const QVector<ScheduleEntry> &entries);
    void updateScheduleDimensions();
    void applyScheduleDimensions(QTableWidget *table);
    bool scheduleSlotExists(const QString &day, int period) const;
    void loadSettings();
    void loadTaskList();
    void filterTaskList(const QString &keyword);
    void updateTaskSortDisplay(int column, Qt::SortOrder order);
    void loadTask(qint64 taskId);
    void resetTaskFields();
    void applyTaskDates(const QString &startText, const QString &endText);
    void setTaskReason(const QString &reason, const QString &taskName = {});
    QString currentTaskReason() const;
    void setTaskEditSummaryText(const QString &text);
    void setFreeSummaryText(const QString &text);
    void syncStatusLabel();

    void refreshTeacherCombo(const QString &preferredTeacher = {});
    void setCurrentTeacher(const QString &teacher, bool force = false);
    void clearAbsentSelection();
    void updateClearSelectionButton();

    void renderFreeSchedule();
    void clearFreeSelection();
    void updateFreeTeacherList();
    void populateFreeTeacherTable(const QVector<QString> &teachers);
    void refreshFreeSubjectCombo(bool keepSelection = true);

    void renderAbsentSchedule(bool preserveSelection = true);
    void renderSubSchedule();
    void renderSchedule(QTableWidget *table, const QString &teacher, const QString &mode);
    void adjustScheduleTable(QTableWidget *table);
    void restoreAbsentSelection();

    QVector<ScheduleEntry> scheduleEntriesFor(
        const QString &teacher,
        const QString &day,
        int period) const;
    bool teacherHasClass(const QString &teacher, const QString &day, int period) const;
    std::optional<Assignment> assignmentFor(
        const QString &teacher,
        const QString &day,
        int period) const;
    QVector<Assignment> substituteAssignmentsFor(
        const QString &teacher,
        const QString &day,
        int period) const;
    bool isSubstituteBusy(
        const QString &teacher,
        const QString &day,
        int period,
        const QString &ignoreAbsentTeacher = {}) const;

    QVector<Assignment> effectiveAssignments() const;
    QVector<Assignment> stripInheritedAssignments(const QVector<Assignment> &rows) const;

    QVector<QPair<int, int>> selectedAbsentCells() const;
    QVector<QPair<QString, int>> selectedFreeSlots() const;
    QString commonAssignedTeacher() const;
    QString formatSubstituteAssignmentLine(const Assignment &assignment) const;
    QString formatFreeSlots(const QVector<QPair<QString, int>> &selectedSlots) const;
    QString buildDefaultExportName() const;
    QString buildDefaultFreeExportName() const;

    QVector<QString> availableSubjects() const;
    QString freeSubjectFilter() const;
    QVector<QString> computeFreeTeachers(const QVector<QPair<QString, int>> &selectedSlots) const;

    void refreshRecommendationsForSelection();
    void populateRecommendationTable(
        const QVector<QString> &candidates,
        const QString &selectedTeacher = {});
    void clearRecommendations();

    qint64 selectedTaskId() const;
    qint64 selectedBaseTaskId() const;
    void selectTaskInTable(qint64 taskId);

    void chooseScheduleFile();
    void chooseTemplateFile();
    void openScheduleFile();
    void openTemplateFile();
    void updateSettings();
    void backupData();
    void exportTaskData();
    void exportSubstituteStatistics();
    void restoreData();
    void onAbsentSelectionChanged();
    void onRecommendSelectionChanged();
    void onFreeSelectionChanged();
    void setAssignment();
    void refreshAssignmentTable();
    void editAssignmentFromRow(int row, int column);
    void deleteSelectedAssignment();
    bool removeAssignmentForCell(int row, int column, bool confirm = true);
    void copyFreeListSelection();
    void exportFreeTeachers();
    void exportAssignments();
    void newTask();
    void openSelectedTask();
    void saveTask();
    void deleteTask();

    QString m_baseDir;
    QString m_schedulePath;
    QString m_templatePath;
    DataStore m_store;

    QVector<ScheduleEntry> m_schedule;
    QStringList m_scheduleDays = kDefaultSchoolDays;
    QList<int> m_schedulePeriods = kPeriods;
    SubjectMap m_subjectMap;
    QVector<TaskSummary> m_tasks;
    QVector<Assignment> m_assignments;
    QVector<Assignment> m_inheritedAssignments;
    QVector<SlotSelection> m_selectedSlots;

    QString m_currentTeacher;
    QString m_selectedSubTeacher;
    qint64 m_currentTaskId = 0;
    qint64 m_baseTaskId = 0;
    bool m_updatingSelection = false;

    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_taskSearchEdit = nullptr;
    QTableWidget *m_taskTable = nullptr;
    QComboBox *m_baseTaskCombo = nullptr;
    QPushButton *m_newTaskButton = nullptr;
    QPushButton *m_openTaskButton = nullptr;
    QPushButton *m_deleteTaskButton = nullptr;

    QLineEdit *m_taskNameEdit = nullptr;
    QDateEdit *m_taskStartDate = nullptr;
    QDateEdit *m_taskEndDate = nullptr;
    QComboBox *m_taskReasonCombo = nullptr;
    QPushButton *m_saveTaskButton = nullptr;

    QLabel *m_summaryLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_teacherCombo = nullptr;
    QPushButton *m_clearSelectionButton = nullptr;
    QTableWidget *m_absentTable = nullptr;
    QTableWidget *m_recommendTable = nullptr;
    QPushButton *m_assignButton = nullptr;
    QTableWidget *m_subTable = nullptr;
    QTableWidget *m_assignmentTable = nullptr;
    QPushButton *m_deleteAssignmentButton = nullptr;
    QPushButton *m_exportAssignmentsButton = nullptr;

    QLabel *m_freeSummaryLabel = nullptr;
    QTableWidget *m_freeScheduleTable = nullptr;
    QComboBox *m_freeSubjectCombo = nullptr;
    QPushButton *m_freeExportButton = nullptr;
    QTableWidget *m_freeListTable = nullptr;

    QLineEdit *m_schedulePathEdit = nullptr;
    QPushButton *m_scheduleBrowseButton = nullptr;
    QLineEdit *m_templatePathEdit = nullptr;
    QPushButton *m_templateBrowseButton = nullptr;
    QPushButton *m_updateSettingsButton = nullptr;
    QDateEdit *m_statisticsStartDateEdit = nullptr;
    QDateEdit *m_statisticsEndDateEdit = nullptr;
    QCheckBox *m_statisticsAllCheckBox = nullptr;
    QPushButton *m_exportStatisticsButton = nullptr;
    QPushButton *m_backupDataButton = nullptr;
    QPushButton *m_exportTaskDataButton = nullptr;
    QPushButton *m_restoreDataButton = nullptr;
    QLabel *m_databaseLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
};

}  // namespace substitute
