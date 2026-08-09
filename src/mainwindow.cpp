#include "mainwindow.h"

#include "coreutils.h"
#include "excelhelper.h"
#include "uimetrics.h"
#include "ui_about_tab.h"
#include "ui_edit_tab.h"
#include "ui_free_tab.h"
#include "ui_main_window_shell.h"
#include "ui_settings_tab.h"
#include "ui_task_tab.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QCursor>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <QSet>

namespace substitute {

namespace {
template <typename Widget>
Widget *requiredChild(QObject *root, const char *name)
{
    Widget *child = root->findChild<Widget *>(QString::fromLatin1(name));
    Q_ASSERT(child);
    return child;
}

void configureDataTable(QTableWidget *table, int rowHeight = ui::kTableRowHeight)
{
    table->verticalHeader()->setDefaultSectionSize(rowHeight);
    table->verticalHeader()->setMinimumSectionSize(rowHeight);
}

void initializeScheduleTable(QTableWidget *table, bool selectable, bool multiSelect = false)
{
    table->setRowCount(kDefaultSchoolDays.size());
    table->setColumnCount(kPeriods.size());
    QStringList periodLabels;
    for (int period : kPeriods) {
        periodLabels.push_back(QString::number(period));
    }
    table->setHorizontalHeaderLabels(periodLabels);
    table->setVerticalHeaderLabels(kDefaultSchoolDays);
    table->setWordWrap(true);
    table->setAlternatingRowColors(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setTextElideMode(Qt::ElideNone);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QFont font = table->font();
    font.setPointSizeF(ui::kScheduleTableFontPointSize);
    table->setFont(font);
    table->horizontalHeader()->setFont(font);
    table->verticalHeader()->setFont(font);

    if (selectable) {
        table->setSelectionMode(
            multiSelect ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
    } else {
        table->setSelectionMode(QAbstractItemView::NoSelection);
    }

    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

int subjectOrderIndex(const QString &subject)
{
    const int index = kSubjectOrder.indexOf(subject);
    return index >= 0 ? index : kSubjectOrder.size() + 1;
}

int dayIndex(const QString &day)
{
    const int index = kDays.indexOf(day);
    return index >= 0 ? index : kDays.size() + 1;
}

int teacherInitialOrder(const QString &teacher)
{
    const QString initial = teacherInitial(teacher);
    if (initial.size() == 1) {
        const QChar ch = initial.front();
        if (ch >= QChar(u'A') && ch <= QChar(u'Z')) {
            return ch.unicode() - QChar(u'A').unicode();
        }
    }
    return 999;
}

QString nowForFileName()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm"));
}

QString selectAbsentTeacherPrompt()
{
    return QStringLiteral("请选择被代课老师");
}

QString selectAbsentSlotsPrompt()
{
    return QStringLiteral("请选择被代课老师的节次（Ctrl/Shift多选）");
}

QString selectTeacherAndSlotsPrompt()
{
    return QStringLiteral("请选择被代课老师并点击课表节次（Ctrl/Shift多选）");
}

QString databaseFileName()
{
    return QStringLiteral("教师课表.db");
}

QString scheduleFileName()
{
    return QStringLiteral("教师课表统计.xlsx");
}

QString defaultTemplateFileName()
{
    return QStringLiteral("通知单模版.xlsx");
}

QString alternateTemplateFileName()
{
    return QStringLiteral("设置通知单模版.xlsx");
}

QString baseDirFilePath(const QString &baseDir, const QString &fileName)
{
    return QDir(baseDir).filePath(fileName);
}

QString legacyMisencodedFileName(const QString &fileName)
{
    const QByteArray utf8 = fileName.toUtf8();
    return QString::fromLatin1(utf8.constData(), utf8.size());
}

QString resolveDatabasePath(const QString &baseDir)
{
    const QString correctPath = baseDirFilePath(baseDir, databaseFileName());
    const QString legacyPath = baseDirFilePath(baseDir, legacyMisencodedFileName(databaseFileName()));
    if (correctPath == legacyPath || QFileInfo::exists(correctPath) || !QFileInfo::exists(legacyPath)) {
        return correctPath;
    }
    if (QFile::rename(legacyPath, correctPath)) {
        return correctPath;
    }
    return legacyPath;
}

QString formatDatabaseLabelText(const QString &databasePath)
{
    return QStringLiteral("数据库文件: %1\n位置: %2")
        .arg(databaseFileName(), QDir::toNativeSeparators(QFileInfo(databasePath).absolutePath()));
}

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , m_baseDir(QApplication::applicationDirPath())
    , m_store(resolveDatabasePath(m_baseDir))
{
    setWindowTitle(QStringLiteral("均程代课管理"));
    setObjectName(QStringLiteral("mainWindow"));
    resize(ui::kInitialWindowWidth, ui::kInitialWindowHeight);

    buildUi();
    initializeDefaultFiles();
}

void MainWindow::setTaskEditSummaryText(const QString &text)
{
    if (m_summaryLabel) {
        m_summaryLabel->setText(text);
    }
    syncStatusLabel();
}

void MainWindow::setFreeSummaryText(const QString &text)
{
    if (m_freeSummaryLabel) {
        m_freeSummaryLabel->setText(text);
    }
    syncStatusLabel();
}

void MainWindow::syncStatusLabel()
{
    if (!m_statusLabel || !m_tabs) {
        return;
    }

    QString text;
    switch (m_tabs->currentIndex()) {
    case 1:
        text = m_summaryLabel ? m_summaryLabel->text() : QString();
        break;
    case 2:
        text = m_freeSummaryLabel ? m_freeSummaryLabel->text() : QString();
        break;
    default:
        break;
    }

    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.trimmed().isEmpty());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    adjustScheduleTable(m_absentTable);
    adjustScheduleTable(m_subTable);
    adjustScheduleTable(m_freeScheduleTable);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_absentTable && watched == m_absentTable->viewport()) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove) {
            if (!m_currentTeacher.isEmpty() && m_selectedSlots.isEmpty()) {
                QToolTip::showText(QCursor::pos(), selectAbsentSlotsPrompt(), m_absentTable->viewport());
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QModelIndex index = m_absentTable->indexAt(mouseEvent->position().toPoint());
            if (mouseEvent->button() == Qt::RightButton && index.isValid()) {
                if (removeAssignmentForCell(index.row(), index.column(), true)) {
                    return true;
                }
            }

            if (mouseEvent->button() == Qt::LeftButton && index.isValid()) {
                if (m_currentTeacher.isEmpty()) {
                    return true;
                }

                const Qt::KeyboardModifiers modifiers = mouseEvent->modifiers();
                if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::ShiftModifier)) {
                    return QWidget::eventFilter(watched, event);
                }

                if (index.row() >= m_scheduleDays.size() || index.column() >= m_schedulePeriods.size()) {
                    return true;
                }
                const QString day = m_scheduleDays.at(index.row());
                const int period = m_schedulePeriods.at(index.column());
                if (scheduleEntriesFor(m_currentTeacher, day, period).isEmpty()) {
                    const QString message =
                        substituteAssignmentsFor(m_currentTeacher, day, period).isEmpty()
                            ? QStringLiteral("仅可选有课节次")
                            : QStringLiteral("该节无原课，但已安排代课");
                    QToolTip::showText(QCursor::pos(), message, m_absentTable->viewport());
                    return true;
                }

                if (QItemSelectionModel *selectionModel = m_absentTable->selectionModel()) {
                    const QItemSelectionModel::SelectionFlag action =
                        selectionModel->isSelected(index) && selectionModel->selectedIndexes().size() <= 1
                            ? QItemSelectionModel::Deselect
                            : QItemSelectionModel::ClearAndSelect;
                    selectionModel->select(index, action);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                return true;
            }
        } else if (event->type() == QEvent::Resize) {
            adjustScheduleTable(m_absentTable);
        }
    }

    if (m_subTable && watched == m_subTable->viewport() && event->type() == QEvent::Resize) {
        adjustScheduleTable(m_subTable);
    }

    if (m_freeScheduleTable && watched == m_freeScheduleTable->viewport() && event->type() == QEvent::Resize) {
        adjustScheduleTable(m_freeScheduleTable);
    }

    return QWidget::eventFilter(watched, event);
}

void MainWindow::buildUi()
{
    Ui::MainWindowShell mainUi;
    mainUi.setupUi(this);

    m_tabs = mainUi.mainTabs;
    QFont tabFont = m_tabs->font();
    tabFont.setPointSizeF(tabFont.pointSizeF() + ui::kTabFontPointDelta);
    tabFont.setBold(true);
    m_tabs->setFont(tabFont);
    m_tabs->tabBar()->setFont(tabFont);

    buildTaskTab();
    buildEditTab();
    buildFreeTab();
    buildSettingsTab();
    buildAboutTab();

    m_statusLabel = mainUi.statusSummaryLabel;
    m_statusLabel->setProperty("role", QStringLiteral("summary"));

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) { syncStatusLabel(); });
    syncStatusLabel();
}

void MainWindow::buildTaskTab()
{
    auto *tab = requiredChild<QWidget>(this, "taskTabPage");
    Ui::TaskTab taskUi;
    taskUi.setupUi(tab);

    m_taskSearchEdit = taskUi.taskSearchEdit;
    connect(m_taskSearchEdit, &QLineEdit::textChanged, this, &MainWindow::filterTaskList);

    m_taskTable = taskUi.taskTable;
    m_taskTable->verticalHeader()->setVisible(false);
    auto *taskHeader = m_taskTable->horizontalHeader();
    taskHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    taskHeader->setSectionResizeMode(3, QHeaderView::Interactive);
    taskHeader->setSectionResizeMode(4, QHeaderView::Interactive);
    taskHeader->resizeSection(3, 150);
    taskHeader->resizeSection(4, 150);
    taskHeader->setStretchLastSection(true);
    taskHeader->setSectionsClickable(true);
    taskHeader->setHighlightSections(true);
    for (int column = 0; column < m_taskTable->columnCount(); ++column) {
        if (auto *headerItem = m_taskTable->horizontalHeaderItem(column)) {
            headerItem->setToolTip(QStringLiteral("点击表头按此列排序"));
        }
    }
    configureDataTable(m_taskTable, 34);
    connect(taskHeader, &QHeaderView::sortIndicatorChanged, this, &MainWindow::updateTaskSortDisplay);
    m_taskTable->setSortingEnabled(true);
    m_taskTable->sortItems(5, Qt::DescendingOrder);
    taskHeader->setSortIndicatorShown(false);
    updateTaskSortDisplay(5, Qt::DescendingOrder);
    connect(m_taskTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::openSelectedTask);

    m_baseTaskCombo = taskUi.baseTaskCombo;

    m_newTaskButton = taskUi.newTaskButton;
    m_openTaskButton = taskUi.openTaskButton;
    m_deleteTaskButton = taskUi.deleteTaskButton;
    connect(m_newTaskButton, &QPushButton::clicked, this, &MainWindow::newTask);
    connect(m_openTaskButton, &QPushButton::clicked, this, &MainWindow::openSelectedTask);
    connect(m_deleteTaskButton, &QPushButton::clicked, this, &MainWindow::deleteTask);

}

void MainWindow::filterTaskList(const QString &keyword)
{
    const QString normalized = keyword.trimmed();
    for (int row = 0; row < m_taskTable->rowCount(); ++row) {
        bool matches = normalized.isEmpty();
        for (int column = 0; column < m_taskTable->columnCount() && !matches; ++column) {
            if (const QTableWidgetItem *item = m_taskTable->item(row, column)) {
                matches = item->text().contains(normalized, Qt::CaseInsensitive);
            }
        }
        m_taskTable->setRowHidden(row, !matches);
    }
}

void MainWindow::updateTaskSortDisplay(int column, Qt::SortOrder order)
{
    static const QStringList kTaskHeaders = {
        QStringLiteral("任务ID"),
        QStringLiteral("任务名称"),
        QStringLiteral("基于任务"),
        QStringLiteral("代课起始日期"),
        QStringLiteral("代课结束日期"),
        QStringLiteral("更新时间"),
    };

    const QString activeArrow = order == Qt::AscendingOrder
        ? QStringLiteral("↑")
        : QStringLiteral("↓");
    for (int index = 0; index < kTaskHeaders.size(); ++index) {
        if (auto *headerItem = m_taskTable->horizontalHeaderItem(index)) {
            const QString arrow = index == column ? activeArrow : QStringLiteral("↕");
            headerItem->setText(QStringLiteral("%1 %2").arg(kTaskHeaders.at(index), arrow));
        }
    }

}

void MainWindow::buildEditTab()
{
    auto *tab = requiredChild<QWidget>(this, "editTabPage");
    Ui::EditTab editUi;
    editUi.setupUi(tab);

    if (auto *taskLayout = qobject_cast<QGridLayout *>(editUi.taskEditGroup->layout())) {
        taskLayout->setColumnStretch(1, 5);
        taskLayout->setColumnStretch(3, 2);
        taskLayout->setColumnStretch(5, 2);
        taskLayout->setColumnStretch(7, 1);
    }

    m_taskNameEdit = editUi.taskNameEdit;
    m_taskStartDate = editUi.taskStartDateEdit;
    m_taskEndDate = editUi.taskEndDateEdit;
    m_taskReasonCombo = editUi.taskReasonCombo;
    m_taskReasonCombo->clear();
    for (const QString &reason : kTaskReasons) {
        m_taskReasonCombo->addItem(reason, reason);
    }

    m_saveTaskButton = editUi.saveTaskButton;
    connect(m_saveTaskButton, &QPushButton::clicked, this, &MainWindow::saveTask);

    m_summaryLabel = editUi.taskSummaryLabel;
    setTaskEditSummaryText(selectTeacherAndSlotsPrompt());

    auto *contentSplitter = editUi.editContentSplitter;
    auto *splitter = editUi.editScheduleSplitter;

    m_searchEdit = editUi.teacherSearchEdit;
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { refreshTeacherCombo(); });

    m_teacherCombo = editUi.teacherCombo;
    connect(
        m_teacherCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int index) {
            if (index >= 0) {
                setCurrentTeacher(m_teacherCombo->currentText());
            }
        });

    m_clearSelectionButton = editUi.clearSelectionButton;
    connect(m_clearSelectionButton, &QPushButton::clicked, this, &MainWindow::clearAbsentSelection);

    m_absentTable = editUi.absentScheduleTable;
    initializeScheduleTable(m_absentTable, true, true);
    m_absentTable->setMouseTracking(true);
    m_absentTable->viewport()->setMouseTracking(true);
    m_absentTable->viewport()->setToolTip(selectAbsentSlotsPrompt());
    m_absentTable->viewport()->installEventFilter(this);
    connect(m_absentTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onAbsentSelectionChanged);

    m_recommendTable = editUi.recommendTable;
    m_recommendTable->horizontalHeader()->setStretchLastSection(true);
    m_recommendTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    configureDataTable(m_recommendTable, ui::kTableRowHeight);
    connect(m_recommendTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onRecommendSelectionChanged);

    m_assignButton = editUi.assignButton;
    connect(m_assignButton, &QPushButton::clicked, this, &MainWindow::setAssignment);

    m_subTable = editUi.subScheduleTable;
    initializeScheduleTable(m_subTable, false);
    m_subTable->viewport()->installEventFilter(this);

    splitter->setStretchFactor(0, 44);
    splitter->setStretchFactor(1, 12);
    splitter->setStretchFactor(2, 44);
    splitter->setSizes({ui::kEditSplitterSizes[0], ui::kEditSplitterSizes[1], ui::kEditSplitterSizes[2]});

    m_deleteAssignmentButton = editUi.deleteAssignmentButton;
    m_exportAssignmentsButton = editUi.exportAssignmentsButton;
    connect(m_deleteAssignmentButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedAssignment);
    connect(m_exportAssignmentsButton, &QPushButton::clicked, this, &MainWindow::exportAssignments);

    m_assignmentTable = editUi.assignmentTable;
    m_assignmentTable->horizontalHeader()->setStretchLastSection(true);
    configureDataTable(m_assignmentTable, ui::kTableRowHeight);
    connect(m_assignmentTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::editAssignmentFromRow);

    contentSplitter->setStretchFactor(0, 7);
    contentSplitter->setStretchFactor(1, 3);
    contentSplitter->setSizes({ui::kContentSplitterSizes[0], ui::kContentSplitterSizes[1]});

}

void MainWindow::buildFreeTab()
{
    auto *tab = requiredChild<QWidget>(this, "freeTabPage");
    Ui::FreeTab freeUi;
    freeUi.setupUi(tab);

    m_freeSummaryLabel = freeUi.freeSummaryLabel;
    m_freeSummaryLabel->hide();
    setFreeSummaryText(QStringLiteral("请选择节次（Ctrl/Shift多选）"));

    m_freeScheduleTable = freeUi.freeScheduleTable;
    initializeScheduleTable(m_freeScheduleTable, true, true);
    m_freeScheduleTable->viewport()->installEventFilter(this);
    connect(m_freeScheduleTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onFreeSelectionChanged);

    m_freeSubjectCombo = freeUi.freeSubjectCombo;
    connect(m_freeSubjectCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        updateFreeTeacherList();
    });

    m_freeExportButton = freeUi.freeExportButton;
    connect(m_freeExportButton, &QPushButton::clicked, this, &MainWindow::exportFreeTeachers);

    m_freeListTable = freeUi.freeListTable;
    configureDataTable(m_freeListTable, ui::kTableRowHeight);
    auto *copyShortcut = new QShortcut(QKeySequence::Copy, m_freeListTable);
    connect(copyShortcut, &QShortcut::activated, this, &MainWindow::copyFreeListSelection);

}

void MainWindow::buildSettingsTab()
{
    auto *tab = requiredChild<QWidget>(this, "settingsTabPage");
    Ui::SettingsTab settingsUi;
    settingsUi.setupUi(tab);

    m_schedulePathEdit = settingsUi.schedulePathEdit;
    m_scheduleBrowseButton = settingsUi.scheduleBrowseButton;
    connect(m_scheduleBrowseButton, &QPushButton::clicked, this, &MainWindow::chooseScheduleFile);
    connect(settingsUi.scheduleOpenButton, &QPushButton::clicked, this, &MainWindow::openScheduleFile);

    m_templatePathEdit = settingsUi.templatePathEdit;
    m_templateBrowseButton = settingsUi.templateBrowseButton;
    connect(m_templateBrowseButton, &QPushButton::clicked, this, &MainWindow::chooseTemplateFile);
    connect(settingsUi.templateOpenButton, &QPushButton::clicked, this, &MainWindow::openTemplateFile);

    m_updateSettingsButton = settingsUi.updateSettingsButton;
    connect(m_updateSettingsButton, &QPushButton::clicked, this, &MainWindow::updateSettings);

    m_statisticsStartDateEdit = settingsUi.statisticsStartDateEdit;
    m_statisticsEndDateEdit = settingsUi.statisticsEndDateEdit;
    m_statisticsAllCheckBox = settingsUi.statisticsAllCheckBox;
    m_exportStatisticsButton = settingsUi.exportStatisticsButton;
    const QDate today = QDate::currentDate();
    m_statisticsStartDateEdit->setDate(QDate(today.year(), today.month(), 1));
    m_statisticsEndDateEdit->setDate(today);
    connect(m_statisticsAllCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_statisticsStartDateEdit->setEnabled(!checked);
        m_statisticsEndDateEdit->setEnabled(!checked);
    });
    connect(
        m_exportStatisticsButton,
        &QPushButton::clicked,
        this,
        &MainWindow::exportSubstituteStatistics);

    m_backupDataButton = settingsUi.backupDataButton;
    m_exportTaskDataButton = settingsUi.exportTaskDataButton;
    m_restoreDataButton = settingsUi.restoreDataButton;
    connect(m_backupDataButton, &QPushButton::clicked, this, &MainWindow::backupData);
    connect(m_exportTaskDataButton, &QPushButton::clicked, this, &MainWindow::exportTaskData);
    connect(m_restoreDataButton, &QPushButton::clicked, this, &MainWindow::restoreData);

    m_databaseLabel = settingsUi.databaseLabel;
}

void MainWindow::buildAboutTab()
{
    auto *tab = requiredChild<QWidget>(this, "aboutTabPage");
    Ui::AboutTab aboutUi;
    aboutUi.setupUi(tab);
}

void MainWindow::initializeDefaultFiles()
{
    QString error;
    m_store.ensureCoreTables(&error);

    m_schedulePath = baseDirFilePath(m_baseDir, scheduleFileName());
    m_templatePath = baseDirFilePath(m_baseDir, defaultTemplateFileName());

    if (m_databaseLabel) {
        m_databaseLabel->setText(formatDatabaseLabelText(m_store.databasePath()));
        m_databaseLabel->setToolTip(QDir::toNativeSeparators(m_store.databasePath()));
    }

    renderFreeSchedule();
    loadSettings();
    loadTaskList();

    if (QFileInfo::exists(m_schedulePath)) {
        loadFromPath(m_schedulePath);
    } else {
        loadFromDatabase();
    }
}

bool MainWindow::loadFromPath(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("读取失败"), QStringLiteral("课表文件不存在。"));
        return false;
    }

    QString error;
    QString warning;
    const QVector<ScheduleEntry> entries = ExcelHelper::readSchedule(path, &error, &warning);
    if (entries.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("读取失败"),
            error.isEmpty() ? QStringLiteral("课表中没有可用数据。") : error);
        return false;
    }

    if (!m_store.importSchedule(entries, &error)) {
        QMessageBox::warning(this, QStringLiteral("读取失败"), error);
        return false;
    }

    m_schedulePath = path;
    applySchedule(entries);
    loadTaskList();
    loadSettings();
    if (!warning.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("课表已导入"),
            QStringLiteral("成功导入 %1 条课表记录。\n\n%2").arg(entries.size()).arg(warning));
    }
    return true;
}

void MainWindow::loadFromDatabase()
{
    QString error;
    const QVector<ScheduleEntry> entries = m_store.loadSchedule(&error);
    if (!entries.isEmpty()) {
        applySchedule(entries);
    } else if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("读取数据库课表失败: %1").arg(error));
    }
}

void MainWindow::updateScheduleDimensions()
{
    QSet<QString> days;
    QSet<int> periods;
    for (const ScheduleEntry &entry : m_schedule) {
        if (!entry.day.trimmed().isEmpty()) {
            days.insert(entry.day.trimmed());
        }
        if (entry.period > 0) {
            periods.insert(entry.period);
        }
    }

    m_scheduleDays.clear();
    for (const QString &day : kDays) {
        if (days.remove(day)) {
            m_scheduleDays.push_back(day);
        }
    }
    QStringList extraDays = days.values();
    std::sort(extraDays.begin(), extraDays.end());
    m_scheduleDays.append(extraDays);
    if (m_scheduleDays.isEmpty()) {
        m_scheduleDays = kDefaultSchoolDays;
    }

    m_schedulePeriods = periods.values();
    std::sort(m_schedulePeriods.begin(), m_schedulePeriods.end());
    if (m_schedulePeriods.isEmpty()) {
        m_schedulePeriods = kPeriods;
    }

    applyScheduleDimensions(m_absentTable);
    applyScheduleDimensions(m_subTable);
    applyScheduleDimensions(m_freeScheduleTable);
}

void MainWindow::applyScheduleDimensions(QTableWidget *table)
{
    if (!table) {
        return;
    }
    table->clearContents();
    table->setRowCount(m_scheduleDays.size());
    table->setColumnCount(m_schedulePeriods.size());
    table->setVerticalHeaderLabels(m_scheduleDays);

    QStringList periodLabels;
    periodLabels.reserve(m_schedulePeriods.size());
    for (int period : m_schedulePeriods) {
        periodLabels.push_back(QString::number(period));
    }
    table->setHorizontalHeaderLabels(periodLabels);

    if (m_schedulePeriods.size() <= 8) {
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else {
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table->horizontalHeader()->setMinimumSectionSize(72);
        table->horizontalHeader()->setDefaultSectionSize(86);
        for (int column = 0; column < table->columnCount(); ++column) {
            table->setColumnWidth(column, 86);
        }
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    table->setVerticalScrollBarPolicy(
        m_scheduleDays.size() <= 7 ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    adjustScheduleTable(table);
}

bool MainWindow::scheduleSlotExists(const QString &day, int period) const
{
    return std::any_of(m_schedule.cbegin(), m_schedule.cend(), [&](const ScheduleEntry &entry) {
        return entry.day == day && entry.period == period;
    });
}

void MainWindow::applySchedule(const QVector<ScheduleEntry> &entries)
{
    m_schedule = entries;
    updateScheduleDimensions();
    m_subjectMap = m_store.buildSubjectMap(m_schedule);
    m_assignments.clear();
    m_inheritedAssignments.clear();
    m_selectedSlots.clear();
    m_selectedSubTeacher.clear();
    m_currentTaskId = 0;
    m_baseTaskId = 0;
    updateClearSelectionButton();

    if (m_schedulePathEdit) {
        m_schedulePathEdit->setText(m_schedulePath);
    }
    if (m_searchEdit) {
        QSignalBlocker blocker(m_searchEdit);
        m_searchEdit->clear();
    }

    renderFreeSchedule();
    refreshFreeSubjectCombo(false);
    clearFreeSelection();
    refreshTeacherCombo();
    refreshAssignmentTable();
    renderAbsentSchedule(false);
    renderSubSchedule();
}

void MainWindow::loadSettings()
{
    QString error;
    QString templatePath = m_store.getSetting(QStringLiteral("notice_template_path"), &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("读取设置失败: %1").arg(error));
    }

    QString invalidTemplateMessage;
    if (!templatePath.isEmpty()) {
        QString validationError;
        if (!ExcelHelper::validateWorkbook(templatePath, &validationError)) {
            invalidTemplateMessage = QStringLiteral("原通知单模板无效：%1").arg(validationError);
            templatePath.clear();
        }
    }

    if (templatePath.isEmpty()) {
        const QStringList candidates = {
            baseDirFilePath(m_baseDir, defaultTemplateFileName()),
            baseDirFilePath(m_baseDir, alternateTemplateFileName()),
        };
        for (const QString &candidate : candidates) {
            QString validationError;
            if (ExcelHelper::validateWorkbook(candidate, &validationError)) {
                templatePath = candidate;
                break;
            }
        }
    }

    if (!templatePath.isEmpty()) {
        m_templatePath = templatePath;
        m_templatePathEdit->setText(templatePath);
        QString saveError;
        m_store.setSetting(QStringLiteral("notice_template_path"), templatePath, &saveError);
    } else {
        m_templatePath.clear();
        m_templatePathEdit->clear();
    }
    if (!invalidTemplateMessage.isEmpty()) {
        const QString suffix = templatePath.isEmpty()
            ? QStringLiteral("\n请在设置中重新选择有效模板。")
            : QStringLiteral("\n已自动切换到默认模板。");
        QMessageBox::warning(this, QStringLiteral("通知单模板提示"), invalidTemplateMessage + suffix);
    }
}

void MainWindow::loadTaskList()
{
    QString error;
    m_tasks = m_store.loadTasks(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("读取任务失败: %1").arg(error));
        return;
    }

    QHash<qint64, QString> idToName;
    for (const TaskSummary &task : m_tasks) {
        idToName.insert(task.id, task.name);
    }
    // Populate rows in the database order first; sorting while inserting can
    // move partially-created rows and make the table contents inconsistent.
    m_taskTable->setSortingEnabled(false);
    m_taskTable->setRowCount(m_tasks.size());
    for (int row = 0; row < m_tasks.size(); ++row) {
        const TaskSummary &task = m_tasks.at(row);
        auto *idItem = new QTableWidgetItem();
        idItem->setData(Qt::DisplayRole, task.id);
        idItem->setData(Qt::UserRole, task.id);
        idItem->setTextAlignment(Qt::AlignCenter);
        auto *nameItem = new QTableWidgetItem(task.name);
        auto *baseTaskItem = new QTableWidgetItem(
            task.baseTaskId > 0 ? idToName.value(task.baseTaskId, QStringLiteral("—")) : QStringLiteral("—"));
        auto *startDateItem = new QTableWidgetItem(task.startDate);
        auto *endDateItem = new QTableWidgetItem(task.endDate);
        auto *updatedItem = new QTableWidgetItem(task.updatedAt);
        const bool isLatest = !m_tasks.isEmpty() && task.id == m_tasks.first().id;
        if (isLatest) {
            updatedItem->setText(QStringLiteral("%1    最新").arg(task.updatedAt));
            QFont latestFont = updatedItem->font();
            latestFont.setBold(true);
            updatedItem->setFont(latestFont);
            updatedItem->setForeground(QColor(QStringLiteral("#b45f29")));
            updatedItem->setToolTip(QStringLiteral("最近更新的任务"));
        }
        for (QTableWidgetItem *item : {idItem, baseTaskItem, startDateItem, endDateItem, updatedItem}) {
            item->setTextAlignment(Qt::AlignCenter);
        }
        m_taskTable->setItem(row, 0, idItem);
        m_taskTable->setItem(row, 1, nameItem);
        m_taskTable->setItem(row, 2, baseTaskItem);
        m_taskTable->setItem(row, 3, startDateItem);
        m_taskTable->setItem(row, 4, endDateItem);
        m_taskTable->setItem(row, 5, updatedItem);
    }
    m_taskTable->setSortingEnabled(true);
    m_taskTable->sortItems(5, Qt::DescendingOrder);
    m_taskTable->horizontalHeader()->setSortIndicatorShown(false);
    updateTaskSortDisplay(5, Qt::DescendingOrder);
    filterTaskList(m_taskSearchEdit ? m_taskSearchEdit->text() : QString());

    {
        QSignalBlocker blocker(m_baseTaskCombo);
        const QVariant currentValue = m_baseTaskCombo->currentData();
        m_baseTaskCombo->clear();
        m_baseTaskCombo->addItem(QStringLiteral("不基于任务"), QVariant());
        for (const TaskSummary &task : m_tasks) {
            m_baseTaskCombo->addItem(task.name, task.id);
        }
        const int index = m_baseTaskCombo->findData(currentValue);
        m_baseTaskCombo->setCurrentIndex(index >= 0 ? index : 0);
    }

    if (m_currentTaskId > 0) {
        selectTaskInTable(m_currentTaskId);
    } else {
        resetTaskFields();
    }
}

qint64 MainWindow::selectedTaskId() const
{
    const int row = m_taskTable->currentRow();
    if (row < 0) {
        return 0;
    }
    if (QTableWidgetItem *item = m_taskTable->item(row, 0)) {
        return item->data(Qt::UserRole).toLongLong();
    }
    return 0;
}

qint64 MainWindow::selectedBaseTaskId() const
{
    const QVariant data = m_baseTaskCombo->currentData();
    return data.isValid() ? data.toLongLong() : 0;
}

void MainWindow::selectTaskInTable(qint64 taskId)
{
    for (int row = 0; row < m_taskTable->rowCount(); ++row) {
        if (QTableWidgetItem *item = m_taskTable->item(row, 0)) {
            if (item->data(Qt::UserRole).toLongLong() == taskId) {
                m_taskTable->setCurrentCell(row, 0);
                break;
            }
        }
    }
}

void MainWindow::resetTaskFields()
{
    const QDate today = QDate::currentDate();
    m_taskNameEdit->clear();
    m_taskStartDate->setDate(today);
    m_taskEndDate->setDate(today);
    setTaskReason(QStringLiteral("请假"));
}

void MainWindow::applyTaskDates(const QString &startText, const QString &endText)
{
    QDate startDate = QDate::fromString(startText, QStringLiteral("yyyy-MM-dd"));
    QDate endDate = QDate::fromString(endText, QStringLiteral("yyyy-MM-dd"));
    if (!startDate.isValid()) {
        startDate = QDate::currentDate();
    }
    if (!endDate.isValid()) {
        endDate = startDate;
    }
    m_taskStartDate->setDate(startDate);
    m_taskEndDate->setDate(endDate);
}

void MainWindow::setTaskReason(const QString &reason, const QString &taskName)
{
    const QString normalized = normalizeTaskReason(reason, taskName);
    int index = m_taskReasonCombo->findData(normalized);
    if (index < 0) {
        index = m_taskReasonCombo->findText(normalized);
    }
    m_taskReasonCombo->setCurrentIndex(index >= 0 ? index : 0);
}

QString MainWindow::currentTaskReason() const
{
    return normalizeTaskReason(m_taskReasonCombo->currentData().toString(), m_taskNameEdit->text());
}

void MainWindow::refreshTeacherCombo(const QString &preferredTeacher)
{
    struct TeacherRecord {
        QString name;
        QString initials;
    };

    QVector<TeacherRecord> teachers;
    QSet<QString> seen;
    const QString keyword = m_searchEdit->text().trimmed().toUpper();
    for (const ScheduleEntry &entry : m_schedule) {
        if (entry.teacher.isEmpty() || seen.contains(entry.teacher)) {
            continue;
        }
        const QString initials = teacherInitials(entry.teacher);
        if (!keyword.isEmpty()
            && !entry.teacher.toUpper().contains(keyword)
            && !initials.contains(keyword)) {
            continue;
        }
        seen.insert(entry.teacher);
        teachers.push_back({entry.teacher, initials});
    }

    std::sort(teachers.begin(), teachers.end(), [](const TeacherRecord &left, const TeacherRecord &right) {
        const int leftOrder = teacherInitialOrder(left.name);
        const int rightOrder = teacherInitialOrder(right.name);
        if (leftOrder != rightOrder) {
            return leftOrder < rightOrder;
        }
        return left.name.localeAwareCompare(right.name) < 0;
    });

    QStringList teacherNames;
    teacherNames.reserve(teachers.size());
    for (const TeacherRecord &record : teachers) {
        teacherNames.push_back(record.name);
    }

    {
        QSignalBlocker blocker(m_teacherCombo);
        m_teacherCombo->clear();
        m_teacherCombo->addItems(teacherNames);
    }

    if (teacherNames.isEmpty()) {
        m_currentTeacher.clear();
        m_selectedSlots.clear();
        m_selectedSubTeacher.clear();
        updateClearSelectionButton();
        clearRecommendations();
        renderAbsentSchedule(false);
        renderSubSchedule();
        setTaskEditSummaryText(QStringLiteral("没有匹配的老师"));
        return;
    }

    QString target = preferredTeacher.isEmpty() ? m_currentTeacher : preferredTeacher;
    if (!teacherNames.contains(target)) {
        target = teacherNames.first();
    }
    {
        QSignalBlocker blocker(m_teacherCombo);
        m_teacherCombo->setCurrentIndex(teacherNames.indexOf(target));
    }
    setCurrentTeacher(target, true);
}

void MainWindow::setCurrentTeacher(const QString &teacher, bool force)
{
    const QString normalized = teacher.trimmed();
    if (!force && normalized == m_currentTeacher) {
        return;
    }

    m_currentTeacher = normalized;
    m_selectedSlots.clear();
    m_selectedSubTeacher.clear();
    updateClearSelectionButton();
    clearRecommendations();
    renderAbsentSchedule(false);
    renderSubSchedule();

    if (m_currentTeacher.isEmpty()) {
        setTaskEditSummaryText(selectAbsentTeacherPrompt());
    } else {
        setTaskEditSummaryText(selectAbsentSlotsPrompt());
    }
}

void MainWindow::clearAbsentSelection()
{
    m_selectedSlots.clear();
    m_selectedSubTeacher.clear();
    m_updatingSelection = true;
    if (m_absentTable->selectionModel()) {
        m_absentTable->selectionModel()->clearSelection();
    }
    m_updatingSelection = false;
    clearRecommendations();
    renderSubSchedule();
    updateClearSelectionButton();
    setTaskEditSummaryText(
        m_currentTeacher.isEmpty() ? selectAbsentTeacherPrompt() : selectAbsentSlotsPrompt());
}

void MainWindow::updateClearSelectionButton()
{
    m_clearSelectionButton->setEnabled(!m_selectedSlots.isEmpty());
}

void MainWindow::renderFreeSchedule()
{
    const QColor unavailableColor(238, 240, 242);
    for (int row = 0; row < m_scheduleDays.size(); ++row) {
        for (int column = 0; column < m_schedulePeriods.size(); ++column) {
            auto *item = new QTableWidgetItem();
            item->setTextAlignment(Qt::AlignCenter);
            if (!scheduleSlotExists(m_scheduleDays.at(row), m_schedulePeriods.at(column))) {
                item->setFlags(Qt::NoItemFlags);
                item->setBackground(unavailableColor);
                item->setToolTip(QStringLiteral("该天没有此节次"));
            }
            m_freeScheduleTable->setItem(row, column, item);
        }
    }
    adjustScheduleTable(m_freeScheduleTable);
}

void MainWindow::clearFreeSelection()
{
    if (m_freeScheduleTable->selectionModel()) {
        m_freeScheduleTable->selectionModel()->clearSelection();
    }
    populateFreeTeacherTable({});
    m_freeExportButton->setEnabled(false);
    setFreeSummaryText(QStringLiteral("请选择节次（Ctrl/Shift多选）"));
}

QVector<QString> MainWindow::availableSubjects() const
{
    QSet<QString> subjects;
    for (auto it = m_subjectMap.cbegin(); it != m_subjectMap.cend(); ++it) {
        if (!it.value().isEmpty()) {
            subjects.insert(it.value());
        }
    }
    QVector<QString> result = subjects.values().toVector();
    std::sort(result.begin(), result.end(), [](const QString &left, const QString &right) {
        const int leftIndex = subjectOrderIndex(left);
        const int rightIndex = subjectOrderIndex(right);
        return leftIndex == rightIndex ? left < right : leftIndex < rightIndex;
    });
    return result;
}

QString MainWindow::freeSubjectFilter() const
{
    const QString subject = m_freeSubjectCombo->currentText().trimmed();
    return subject == kFreeSubjectAll ? QString() : subject;
}

void MainWindow::refreshFreeSubjectCombo(bool keepSelection)
{
    const QString current = keepSelection ? m_freeSubjectCombo->currentText() : kFreeSubjectAll;
    QSignalBlocker blocker(m_freeSubjectCombo);
    m_freeSubjectCombo->clear();
    m_freeSubjectCombo->addItem(kFreeSubjectAll);
    for (const QString &subject : availableSubjects()) {
        m_freeSubjectCombo->addItem(subject);
    }
    const int index = m_freeSubjectCombo->findText(current);
    m_freeSubjectCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void MainWindow::populateFreeTeacherTable(const QVector<QString> &teachers)
{
    m_freeListTable->setRowCount(teachers.size());
    for (int row = 0; row < teachers.size(); ++row) {
        auto *item = new QTableWidgetItem(teachers.at(row));
        item->setTextAlignment(Qt::AlignCenter);
        m_freeListTable->setItem(row, 0, item);
    }
}

QVector<QPair<QString, int>> MainWindow::selectedFreeSlots() const
{
    QVector<QPair<QString, int>> selectedSlots;
    if (!m_freeScheduleTable->selectionModel()) {
        return selectedSlots;
    }

    QSet<QString> seen;
    for (const QModelIndex &index : m_freeScheduleTable->selectionModel()->selectedIndexes()) {
        if (!index.isValid()) {
            continue;
        }
        const QString key = QStringLiteral("%1|%2").arg(index.row()).arg(index.column());
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        if (index.row() >= m_scheduleDays.size() || index.column() >= m_schedulePeriods.size()) {
            continue;
        }
        const QString day = m_scheduleDays.at(index.row());
        const int period = m_schedulePeriods.at(index.column());
        if (!scheduleSlotExists(day, period)) {
            continue;
        }
        selectedSlots.push_back({day, period});
    }
    std::sort(selectedSlots.begin(), selectedSlots.end(), [](const auto &left, const auto &right) {
        const int leftDay = dayIndex(left.first);
        const int rightDay = dayIndex(right.first);
        return leftDay == rightDay ? left.second < right.second : leftDay < rightDay;
    });
    return selectedSlots;
}

QVector<QString> MainWindow::computeFreeTeachers(const QVector<QPair<QString, int>> &selectedSlots) const
{
    if (selectedSlots.isEmpty()) {
        return {};
    }

    QSet<QString> busyTeachers;
    for (const auto &selectedSlot : selectedSlots) {
        for (const ScheduleEntry &entry : m_schedule) {
            if (entry.day == selectedSlot.first && entry.period == selectedSlot.second) {
                busyTeachers.insert(entry.teacher);
            }
        }
    }

    QVector<QString> teachers = m_subjectMap.keys().toVector();
    std::sort(teachers.begin(), teachers.end(), [this](const QString &left, const QString &right) {
        const int leftIndex = subjectOrderIndex(m_subjectMap.value(left));
        const int rightIndex = subjectOrderIndex(m_subjectMap.value(right));
        return leftIndex == rightIndex ? left < right : leftIndex < rightIndex;
    });

    QVector<QString> freeTeachers;
    const QString subjectFilter = freeSubjectFilter();
    for (const QString &teacher : teachers) {
        if (busyTeachers.contains(teacher)) {
            continue;
        }
        if (!subjectFilter.isEmpty() && m_subjectMap.value(teacher) != subjectFilter) {
            continue;
        }
        freeTeachers.push_back(teacher);
    }
    return freeTeachers;
}

QString MainWindow::formatFreeSlots(const QVector<QPair<QString, int>> &selectedSlots) const
{
    QStringList parts;
    for (const auto &selectedSlot : selectedSlots) {
        parts << QStringLiteral("%1第%2节").arg(selectedSlot.first).arg(selectedSlot.second);
    }
    return parts.join(QStringLiteral("、"));
}

void MainWindow::updateFreeTeacherList()
{
    const QVector<QPair<QString, int>> selectedSlots = selectedFreeSlots();
    if (selectedSlots.isEmpty()) {
        populateFreeTeacherTable({});
        m_freeExportButton->setEnabled(false);
        setFreeSummaryText(QStringLiteral("请选择节次（Ctrl/Shift多选）"));
        return;
    }

    const QVector<QString> teachers = computeFreeTeachers(selectedSlots);
    populateFreeTeacherTable(teachers);
    m_freeExportButton->setEnabled(!teachers.isEmpty());

    const QString subject = freeSubjectFilter();
    setFreeSummaryText(
        subject.isEmpty()
            ? QStringLiteral("已选%1节，无课%2人").arg(selectedSlots.size()).arg(teachers.size())
            : QStringLiteral("已选%1节，无课%2人（%3）").arg(selectedSlots.size()).arg(teachers.size(), 0, 10).arg(subject));
}

QVector<ScheduleEntry> MainWindow::scheduleEntriesFor(
    const QString &teacher,
    const QString &day,
    int period) const
{
    QVector<ScheduleEntry> result;
    for (const ScheduleEntry &entry : m_schedule) {
        if (entry.teacher == teacher && entry.day == day && entry.period == period) {
            result.push_back(entry);
        }
    }
    return result;
}

bool MainWindow::teacherHasClass(const QString &teacher, const QString &day, int period) const
{
    return std::any_of(m_schedule.cbegin(), m_schedule.cend(), [&](const ScheduleEntry &entry) {
        return entry.teacher == teacher && entry.day == day && entry.period == period;
    });
}

std::optional<Assignment> MainWindow::assignmentFor(
    const QString &teacher,
    const QString &day,
    int period) const
{
    for (const Assignment &assignment : m_assignments) {
        if (assignment.absentTeacher == teacher && assignment.day == day && assignment.period == period) {
            return assignment;
        }
    }
    for (const Assignment &assignment : m_inheritedAssignments) {
        if (assignment.absentTeacher == teacher && assignment.day == day && assignment.period == period) {
            return assignment;
        }
    }
    return std::nullopt;
}

QVector<Assignment> MainWindow::substituteAssignmentsFor(
    const QString &teacher,
    const QString &day,
    int period) const
{
    QVector<Assignment> rows;
    for (const Assignment &assignment : effectiveAssignments()) {
        if (assignment.substituteTeacher == teacher && assignment.day == day && assignment.period == period) {
            rows.push_back(assignment);
        }
    }
    return rows;
}

bool MainWindow::isSubstituteBusy(
    const QString &teacher,
    const QString &day,
    int period,
    const QString &ignoreAbsentTeacher) const
{
    for (const Assignment &assignment : effectiveAssignments()) {
        if (assignment.substituteTeacher == teacher && assignment.day == day && assignment.period == period) {
            if (!ignoreAbsentTeacher.isEmpty() && assignment.absentTeacher == ignoreAbsentTeacher) {
                continue;
            }
            return true;
        }
    }
    return false;
}

QVector<Assignment> MainWindow::effectiveAssignments() const
{
    return mergeAssignments(m_inheritedAssignments, m_assignments);
}

QVector<Assignment> MainWindow::stripInheritedAssignments(const QVector<Assignment> &rows) const
{
    if (m_inheritedAssignments.isEmpty()) {
        return rows;
    }

    QHash<QString, Assignment> inheritedMap;
    for (const Assignment &assignment : m_inheritedAssignments) {
        inheritedMap.insert(assignmentSlotKey(assignment), assignment);
    }

    QVector<Assignment> result;
    for (const Assignment &assignment : rows) {
        const QString key = assignmentSlotKey(assignment);
        if (inheritedMap.contains(key) && assignmentEquals(inheritedMap.value(key), assignment)) {
            continue;
        }
        result.push_back(assignment);
    }
    return result;
}

QVector<QPair<int, int>> MainWindow::selectedAbsentCells() const
{
    QVector<QPair<int, int>> cells;
    if (!m_absentTable->selectionModel()) {
        return cells;
    }

    QSet<QString> seen;
    for (const QModelIndex &index : m_absentTable->selectionModel()->selectedIndexes()) {
        const QString key = QStringLiteral("%1|%2").arg(index.row()).arg(index.column());
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        cells.push_back({index.row(), index.column()});
    }
    std::sort(cells.begin(), cells.end());
    return cells;
}

QString MainWindow::formatSubstituteAssignmentLine(const Assignment &assignment) const
{
    QStringList parts;
    if (!assignment.absentTeacher.isEmpty()) {
        parts << assignment.absentTeacher;
    }
    if (!assignment.className.isEmpty()) {
        parts << assignment.className;
    }
    return parts.isEmpty()
        ? QStringLiteral("代课")
        : QStringLiteral("代课:%1").arg(parts.join(QLatin1Char(' ')));
}

void MainWindow::renderSchedule(QTableWidget *table, const QString &teacher, const QString &mode)
{
    const QColor absentColor(255, 214, 165);
    const QColor subColor(189, 224, 254);
    const QColor mixedColor(207, 238, 214);
    const QColor unavailableColor(238, 240, 242);

    for (int row = 0; row < m_scheduleDays.size(); ++row) {
        for (int column = 0; column < m_schedulePeriods.size(); ++column) {
            const QString day = m_scheduleDays.at(row);
            const int period = m_schedulePeriods.at(column);
            QStringList lines;
            for (const ScheduleEntry &entry : scheduleEntriesFor(teacher, day, period)) {
                lines << QStringLiteral("%1 %2").arg(entry.className, entry.subject);
            }

            QColor highlight;
            if (mode == QLatin1String("absent") && !teacher.isEmpty()) {
                const auto assignment = assignmentFor(teacher, day, period);
                if (assignment.has_value()) {
                    lines << QStringLiteral("代:%1").arg(assignment->substituteTeacher);
                }
                const QVector<Assignment> substituteRows = substituteAssignmentsFor(teacher, day, period);
                for (const Assignment &rowData : substituteRows) {
                    lines << formatSubstituteAssignmentLine(rowData);
                }
                if (assignment.has_value() && !substituteRows.isEmpty()) {
                    highlight = mixedColor;
                } else if (assignment.has_value()) {
                    highlight = absentColor;
                } else if (!substituteRows.isEmpty()) {
                    highlight = subColor;
                }
            } else if (mode == QLatin1String("sub") && !teacher.isEmpty()) {
                const QVector<Assignment> substituteRows = substituteAssignmentsFor(teacher, day, period);
                for (const Assignment &rowData : substituteRows) {
                    lines << formatSubstituteAssignmentLine(rowData);
                }
                if (!substituteRows.isEmpty()) {
                    highlight = subColor;
                }
            }

            auto *item = new QTableWidgetItem(lines.join(QLatin1Char('\n')));
            item->setTextAlignment(Qt::AlignCenter);
            if (!scheduleSlotExists(day, period)) {
                item->setFlags(Qt::NoItemFlags);
                item->setBackground(unavailableColor);
                item->setToolTip(QStringLiteral("该天没有此节次"));
            } else if (highlight.isValid()) {
                item->setBackground(highlight);
            }
            table->setItem(row, column, item);
        }
    }
    adjustScheduleTable(table);
}

void MainWindow::adjustScheduleTable(QTableWidget *table)
{
    if (!table || table->rowCount() <= 0 || table->columnCount() <= 0) {
        return;
    }

    const int viewportHeight = table->viewport()->height();
    const int viewportWidth = table->viewport()->width();
    if (viewportHeight <= 0 || viewportWidth <= 0) {
        return;
    }

    const int baseRowHeight = std::max(32, viewportHeight / table->rowCount());
    const int rowRemainder = viewportHeight % table->rowCount();
    for (int row = 0; row < table->rowCount(); ++row) {
        table->setRowHeight(row, baseRowHeight + (row < rowRemainder ? 1 : 0));
    }

    qreal bestFontSize = 5.8;
    for (qreal fontSize = 8.6; fontSize >= 5.8; fontSize -= 0.2) {
        QFont candidate = table->font();
        candidate.setPointSizeF(fontSize);
        QFontMetricsF metrics(candidate);
        bool fits = true;

        for (int row = 0; row < table->rowCount() && fits; ++row) {
            for (int column = 0; column < table->columnCount(); ++column) {
                QTableWidgetItem *item = table->item(row, column);
                if (!item || item->text().trimmed().isEmpty()) {
                    continue;
                }

                const qreal usableCellWidth = std::max(42.0, qreal(table->columnWidth(column)) - 8.0);
                const qreal usableCellHeight = std::max(22.0, qreal(table->rowHeight(row)) - 6.0);
                const QRectF textRect = metrics.boundingRect(
                    QRectF(0.0, 0.0, usableCellWidth, 4096.0),
                    Qt::AlignCenter | Qt::TextWordWrap,
                    item->text());
                if (textRect.height() > usableCellHeight) {
                    fits = false;
                    break;
                }
            }
        }

        if (fits) {
            bestFontSize = fontSize;
            break;
        }
    }

    QFont bodyFont = table->font();
    bodyFont.setPointSizeF(bestFontSize);
    table->setFont(bodyFont);

    QFont headerFont = bodyFont;
    headerFont.setPointSizeF(std::max(7.2, bestFontSize + 0.3));
    headerFont.setBold(true);
    table->horizontalHeader()->setFont(headerFont);
    table->verticalHeader()->setFont(headerFont);
}

void MainWindow::restoreAbsentSelection()
{
    if (!m_absentTable->selectionModel()) {
        return;
    }
    m_updatingSelection = true;
    m_absentTable->selectionModel()->clearSelection();
    for (const SlotSelection &slot : m_selectedSlots) {
        const int row = m_scheduleDays.indexOf(slot.day);
        const int column = m_schedulePeriods.indexOf(slot.period);
        if (row >= 0 && column >= 0) {
            const QModelIndex index = m_absentTable->model()->index(row, column);
            m_absentTable->selectionModel()->select(index, QItemSelectionModel::Select);
        }
    }
    m_updatingSelection = false;
}

void MainWindow::renderAbsentSchedule(bool preserveSelection)
{
    renderSchedule(m_absentTable, m_currentTeacher, QStringLiteral("absent"));
    if (preserveSelection) {
        restoreAbsentSelection();
    } else if (m_absentTable->selectionModel()) {
        m_updatingSelection = true;
        m_absentTable->selectionModel()->clearSelection();
        m_updatingSelection = false;
    }
}

void MainWindow::renderSubSchedule()
{
    renderSchedule(m_subTable, m_selectedSubTeacher, QStringLiteral("sub"));
}

void MainWindow::onAbsentSelectionChanged()
{
    if (m_updatingSelection || m_currentTeacher.isEmpty()) {
        return;
    }

    const QVector<QPair<int, int>> cells = selectedAbsentCells();
    if (cells.isEmpty()) {
        m_selectedSlots.clear();
        m_selectedSubTeacher.clear();
        clearRecommendations();
        renderSubSchedule();
        updateClearSelectionButton();
        setTaskEditSummaryText(selectAbsentSlotsPrompt());
        return;
    }

    QVector<QPair<int, int>> invalidCells;
    QVector<SlotSelection> validSlots;
    for (const auto &cell : cells) {
        if (cell.first >= m_scheduleDays.size() || cell.second >= m_schedulePeriods.size()) {
            invalidCells.push_back(cell);
            continue;
        }
        const QString day = m_scheduleDays.at(cell.first);
        const int period = m_schedulePeriods.at(cell.second);
        const QVector<ScheduleEntry> entries = scheduleEntriesFor(m_currentTeacher, day, period);
        if (entries.isEmpty()) {
            invalidCells.push_back(cell);
            continue;
        }
        const ScheduleEntry &entry = entries.first();
        SlotSelection slot;
        slot.absentTeacher = m_currentTeacher;
        slot.day = day;
        slot.period = period;
        slot.className = entry.className;
        slot.subject = entry.subject;
        validSlots.push_back(slot);
    }

    if (!invalidCells.isEmpty() && m_absentTable->selectionModel()) {
        m_updatingSelection = true;
        for (const auto &cell : invalidCells) {
            const QModelIndex index = m_absentTable->model()->index(cell.first, cell.second);
            m_absentTable->selectionModel()->select(index, QItemSelectionModel::Deselect);
        }
        m_updatingSelection = false;
    }

    m_selectedSlots = validSlots;
    updateClearSelectionButton();
    if (m_selectedSlots.isEmpty()) {
        clearRecommendations();
        renderSubSchedule();
        setTaskEditSummaryText(QStringLiteral("请选择有课的节次（Ctrl/Shift多选）"));
        return;
    }

    refreshRecommendationsForSelection();
}

QString MainWindow::commonAssignedTeacher() const
{
    QSet<QString> teachers;
    bool hasUnassigned = false;
    for (const SlotSelection &slot : m_selectedSlots) {
        const auto assignment = assignmentFor(slot.absentTeacher, slot.day, slot.period);
        if (!assignment.has_value()) {
            hasUnassigned = true;
            continue;
        }
        teachers.insert(assignment->substituteTeacher);
    }
    return (!hasUnassigned && teachers.size() == 1) ? *teachers.cbegin() : QString();
}

void MainWindow::clearRecommendations()
{
    m_recommendTable->setRowCount(0);
    m_selectedSubTeacher.clear();
}

void MainWindow::populateRecommendationTable(
    const QVector<QString> &candidates,
    const QString &selectedTeacher)
{
    m_recommendTable->setRowCount(candidates.size());
    QSignalBlocker blocker(m_recommendTable);
    for (int row = 0; row < candidates.size(); ++row) {
        auto *item = new QTableWidgetItem(candidates.at(row));
        item->setTextAlignment(Qt::AlignCenter);
        m_recommendTable->setItem(row, 0, item);
    }

    m_selectedSubTeacher.clear();
    if (!selectedTeacher.isEmpty()) {
        for (int row = 0; row < candidates.size(); ++row) {
            if (candidates.at(row) == selectedTeacher) {
                m_recommendTable->setCurrentCell(row, 0);
                m_selectedSubTeacher = selectedTeacher;
                break;
            }
        }
    }
    renderSubSchedule();
}

void MainWindow::refreshRecommendationsForSelection()
{
    if (m_currentTeacher.isEmpty() || m_selectedSlots.isEmpty()) {
        clearRecommendations();
        renderSubSchedule();
        return;
    }

    QSet<QString> subjects;
    for (const SlotSelection &slot : m_selectedSlots) {
        subjects.insert(slot.subject);
    }
    if (subjects.size() != 1) {
        setTaskEditSummaryText(QStringLiteral("所选节次科目不一致"));
        clearRecommendations();
        renderSubSchedule();
        return;
    }

    const QString subject = *subjects.cbegin();
    QVector<QString> candidates;
    for (auto it = m_subjectMap.cbegin(); it != m_subjectMap.cend(); ++it) {
        if (it.key() == m_currentTeacher || it.value() != subject) {
            continue;
        }
        bool conflict = false;
        for (const SlotSelection &slot : m_selectedSlots) {
            if (teacherHasClass(it.key(), slot.day, slot.period)
                || isSubstituteBusy(it.key(), slot.day, slot.period, m_currentTeacher)) {
                conflict = true;
                break;
            }
        }
        if (!conflict) {
            candidates.push_back(it.key());
        }
    }
    std::sort(candidates.begin(), candidates.end());

    setTaskEditSummaryText(QStringLiteral("已选%1节 - %2").arg(m_selectedSlots.size()).arg(subject));
    populateRecommendationTable(candidates, commonAssignedTeacher());
}

void MainWindow::onRecommendSelectionChanged()
{
    const auto selectedRows = m_recommendTable->selectionModel()
        ? m_recommendTable->selectionModel()->selectedRows(0)
        : QModelIndexList();
    if (selectedRows.isEmpty()) {
        m_selectedSubTeacher.clear();
        renderSubSchedule();
        return;
    }
    if (QTableWidgetItem *item = m_recommendTable->item(selectedRows.first().row(), 0)) {
        m_selectedSubTeacher = item->text().trimmed();
    } else {
        m_selectedSubTeacher.clear();
    }
    renderSubSchedule();
}

void MainWindow::setAssignment()
{
    if (m_selectedSlots.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择被代课节次。"));
        return;
    }
    if (m_currentTeacher.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择被代课老师。"));
        return;
    }
    if (m_selectedSubTeacher.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择代课老师。"));
        return;
    }

    const QString subject = m_selectedSlots.first().subject;
    for (const SlotSelection &slot : m_selectedSlots) {
        if (slot.subject != subject) {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("所选节次科目不一致。"));
            return;
        }
    }
    if (m_subjectMap.value(m_selectedSubTeacher) != subject) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("仅支持同科目代课。"));
        return;
    }

    for (const SlotSelection &slot : m_selectedSlots) {
        if (teacherHasClass(m_selectedSubTeacher, slot.day, slot.period)) {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("该老师该节已有课程冲突。"));
            return;
        }
        if (isSubstituteBusy(m_selectedSubTeacher, slot.day, slot.period, m_currentTeacher)) {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("该老师该节已被安排代课。"));
            return;
        }
    }

    for (const SlotSelection &slot : m_selectedSlots) {
        Assignment assignment;
        assignment.absentTeacher = m_currentTeacher;
        assignment.day = slot.day;
        assignment.period = slot.period;
        assignment.className = slot.className;
        assignment.subject = slot.subject;
        assignment.substituteTeacher = m_selectedSubTeacher;
        assignment.substituteSubject = m_subjectMap.value(m_selectedSubTeacher);

        bool replaced = false;
        for (Assignment &existing : m_assignments) {
            if (existing.absentTeacher == assignment.absentTeacher
                && existing.day == assignment.day
                && existing.period == assignment.period) {
                existing = assignment;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            m_assignments.push_back(assignment);
        }
    }

    refreshAssignmentTable();
    renderAbsentSchedule(true);
    refreshRecommendationsForSelection();
}

void MainWindow::refreshAssignmentTable()
{
    m_assignmentTable->setRowCount(m_assignments.size());
    for (int row = 0; row < m_assignments.size(); ++row) {
        const Assignment &assignment = m_assignments.at(row);
        m_assignmentTable->setItem(row, 0, new QTableWidgetItem(assignment.absentTeacher));
        m_assignmentTable->setItem(row, 1, new QTableWidgetItem(assignment.day));
        m_assignmentTable->setItem(row, 2, new QTableWidgetItem(QString::number(assignment.period)));
        m_assignmentTable->setItem(row, 3, new QTableWidgetItem(assignment.className));
        m_assignmentTable->setItem(row, 4, new QTableWidgetItem(assignment.subject));
        m_assignmentTable->setItem(row, 5, new QTableWidgetItem(assignment.substituteTeacher));
    }
}

void MainWindow::editAssignmentFromRow(int row, int)
{
    if (row < 0 || row >= m_assignments.size()) {
        return;
    }
    const Assignment &assignment = m_assignments.at(row);

    if (!m_searchEdit->text().isEmpty()) {
        m_searchEdit->clear();
    }
    refreshTeacherCombo(assignment.absentTeacher);
    m_selectedSlots = {{
        assignment.absentTeacher,
        assignment.day,
        assignment.period,
        assignment.className,
        assignment.subject,
    }};
    m_selectedSubTeacher.clear();
    updateClearSelectionButton();
    renderAbsentSchedule(true);
    refreshRecommendationsForSelection();
}

void MainWindow::deleteSelectedAssignment()
{
    const int row = m_assignmentTable->currentRow();
    if (row < 0 || row >= m_assignments.size()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请选择要删除的代课安排。"));
        return;
    }
    const Assignment assignment = m_assignments.takeAt(row);
    refreshAssignmentTable();
    if (assignment.absentTeacher == m_currentTeacher) {
        renderAbsentSchedule(true);
        refreshRecommendationsForSelection();
    }
}

bool MainWindow::removeAssignmentForCell(int row, int column, bool confirm)
{
    if (m_currentTeacher.isEmpty() || row < 0 || column < 0) {
        return false;
    }

    if (row >= m_scheduleDays.size() || column >= m_schedulePeriods.size()) {
        return false;
    }
    const QString day = m_scheduleDays.at(row);
    const int period = m_schedulePeriods.at(column);
    for (int index = 0; index < m_assignments.size(); ++index) {
        const Assignment &assignment = m_assignments.at(index);
        if (assignment.absentTeacher == m_currentTeacher && assignment.day == day && assignment.period == period) {
            if (confirm) {
                const auto answer = QMessageBox::question(
                    this,
                    QStringLiteral("确认取消"),
                    QStringLiteral("确认取消 %1 %2 第%3节 的代课安排吗？\n代课老师：%4")
                        .arg(m_currentTeacher, day)
                        .arg(period)
                        .arg(assignment.substituteTeacher));
                if (answer != QMessageBox::Yes) {
                    return false;
                }
            }
            m_assignments.removeAt(index);
            refreshAssignmentTable();
            renderAbsentSchedule(true);
            refreshRecommendationsForSelection();
            return true;
        }
    }
    return false;
}

void MainWindow::loadTask(qint64 taskId)
{
    QString error;
    const auto detailOpt = m_store.loadTask(taskId, &error);
    if (!detailOpt.has_value()) {
        QMessageBox::warning(
            this,
            QStringLiteral("提示"),
            error.isEmpty() ? QStringLiteral("无法读取任务。") : QStringLiteral("读取任务失败: %1").arg(error));
        return;
    }

    const TaskDetail detail = *detailOpt;
    m_currentTaskId = detail.summary.id;
    m_baseTaskId = detail.summary.baseTaskId;
    {
        QSignalBlocker blocker(m_baseTaskCombo);
        const int index = m_baseTaskCombo->findData(m_baseTaskId);
        m_baseTaskCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    m_taskNameEdit->setText(detail.summary.name);
    applyTaskDates(detail.summary.startDate, detail.summary.endDate);
    setTaskReason(detail.summary.reason, detail.summary.name);

    QString inheritedError;
    m_inheritedAssignments = m_store.loadInheritedAssignments(m_baseTaskId, &inheritedError);
    if (!inheritedError.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("读取基任务失败: %1").arg(inheritedError));
        return;
    }

    m_assignments = stripInheritedAssignments(detail.assignments);
    refreshAssignmentTable();
    clearAbsentSelection();
    renderAbsentSchedule(false);
    renderSubSchedule();
}

void MainWindow::chooseScheduleFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择课表文件"),
        m_baseDir,
        QStringLiteral("Excel (*.xlsx *.xls)"));
    if (!file.isEmpty()) {
        loadFromPath(file);
    }
}

void MainWindow::chooseTemplateFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择通知单模板"),
        m_baseDir,
        QStringLiteral("Excel (*.xlsx *.xls)"));
    if (file.isEmpty()) {
        return;
    }

    QString error;
    if (!ExcelHelper::validateWorkbook(file, &error)) {
        QMessageBox::warning(this, QStringLiteral("提示"), error);
        return;
    }

    m_templatePath = file;
    m_templatePathEdit->setText(file);
    m_store.setSetting(QStringLiteral("notice_template_path"), file, &error);
}

void MainWindow::openScheduleFile()
{
    const QString path = m_schedulePathEdit->text().trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择有效的课表文件。"));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()))) {
        QMessageBox::warning(
            this,
            QStringLiteral("打开失败"),
            QStringLiteral("无法调用系统中的 Excel 程序打开课表文件。"));
    }
}

void MainWindow::openTemplateFile()
{
    const QString path = m_templatePathEdit->text().trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择有效的通知单模板。"));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()))) {
        QMessageBox::warning(
            this,
            QStringLiteral("打开失败"),
            QStringLiteral("无法调用系统中的 Excel 程序打开通知单模板。"));
    }
}

void MainWindow::updateSettings()
{
    const QString schedulePath = m_schedulePathEdit->text().trimmed();
    const QString templatePath = m_templatePathEdit->text().trimmed();

    if (schedulePath.isEmpty() || !QFileInfo::exists(schedulePath)) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择有效的课表文件。"));
        return;
    }
    if (templatePath.isEmpty() || !QFileInfo::exists(templatePath)) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择有效的通知单模板。"));
        return;
    }

    QString error;
    if (!ExcelHelper::validateWorkbook(templatePath, &error)) {
        QMessageBox::warning(this, QStringLiteral("提示"), error);
        return;
    }
    if (!loadFromPath(schedulePath)) {
        return;
    }
    if (!m_store.setSetting(QStringLiteral("notice_template_path"), templatePath, &error)) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("保存模板设置失败: %1").arg(error));
        return;
    }

    m_templatePath = templatePath;
    m_templatePathEdit->setText(templatePath);
    QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("设置已更新。"));
}

void MainWindow::backupData()
{
    QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("备份全部数据"),
        m_baseDir + QLatin1Char('/')
            + QStringLiteral("代课数据备份_%1.db").arg(nowForFileName()),
        QStringLiteral("SQLite 数据库 (*.db)"));
    if (outputPath.isEmpty()) {
        return;
    }
    if (QFileInfo(outputPath).suffix().isEmpty()) {
        outputPath += QStringLiteral(".db");
    }

    QString error;
    if (!m_store.backupDatabase(outputPath, &error)) {
        QMessageBox::warning(
            this,
            QStringLiteral("备份失败"),
            error.isEmpty() ? QStringLiteral("无法创建数据备份。") : error);
        return;
    }
    QMessageBox::information(
        this,
        QStringLiteral("备份完成"),
        QStringLiteral("全部数据已备份到：\n%1").arg(QDir::toNativeSeparators(outputPath)));
}

void MainWindow::exportTaskData()
{
    QString error;
    const QVector<TaskSummary> summaries = m_store.loadTasks(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("读取任务失败：%1").arg(error));
        return;
    }
    if (summaries.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("没有可导出的代课任务。"));
        return;
    }

    QVector<TaskDetail> tasks;
    tasks.reserve(summaries.size());
    for (const TaskSummary &summary : summaries) {
        error.clear();
        const auto detailOpt = m_store.loadTask(summary.id, &error);
        if (!detailOpt.has_value()) {
            QMessageBox::warning(
                this,
                QStringLiteral("导出失败"),
                error.isEmpty()
                    ? QStringLiteral("无法读取任务“%1”。").arg(summary.name)
                    : QStringLiteral("读取任务“%1”失败：%2").arg(summary.name, error));
            return;
        }

        TaskDetail detail = *detailOpt;
        detail.summary.updatedAt = summary.updatedAt;
        error.clear();
        const QVector<Assignment> inherited =
            m_store.loadInheritedAssignments(detail.summary.baseTaskId, &error);
        if (!error.isEmpty()) {
            QMessageBox::warning(
                this,
                QStringLiteral("导出失败"),
                QStringLiteral("读取任务“%1”的继承安排失败：%2").arg(summary.name, error));
            return;
        }
        detail.assignments = mergeAssignments(inherited, detail.assignments);
        tasks.push_back(detail);
    }

    QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出代课数据"),
        m_baseDir + QLatin1Char('/')
            + QStringLiteral("代课数据_%1.xlsx").arg(nowForFileName()),
        QStringLiteral("Excel 工作簿 (*.xlsx)"));
    if (outputPath.isEmpty()) {
        return;
    }
    if (QFileInfo(outputPath).suffix().isEmpty()) {
        outputPath += QStringLiteral(".xlsx");
    }

    error.clear();
    if (!ExcelHelper::exportTaskData(outputPath, tasks, &error)) {
        QMessageBox::warning(
            this,
            QStringLiteral("导出失败"),
            error.isEmpty() ? QStringLiteral("无法导出代课数据。") : error);
        return;
    }
    QMessageBox::information(
        this,
        QStringLiteral("导出完成"),
        QStringLiteral("代课数据已导出到：\n%1").arg(QDir::toNativeSeparators(outputPath)));
}

void MainWindow::exportSubstituteStatistics()
{
    const bool exportAll = m_statisticsAllCheckBox->isChecked();
    const QDate rangeStart = m_statisticsStartDateEdit->date();
    const QDate rangeEnd = m_statisticsEndDateEdit->date();
    if (!exportAll && rangeStart > rangeEnd) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("起始日期不能晚于结束日期。"));
        return;
    }

    QString error;
    const QVector<TaskSummary> summaries = m_store.loadTasks(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("读取任务失败：%1").arg(error));
        return;
    }
    if (summaries.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("没有可导出的代课任务。"));
        return;
    }

    QVector<TaskDetail> tasks;
    int invalidDateTasks = 0;
    for (const TaskSummary &summary : summaries) {
        if (!exportAll) {
            const QDate taskStart = QDate::fromString(summary.startDate, QStringLiteral("yyyy-MM-dd"));
            const QDate taskEnd = QDate::fromString(summary.endDate, QStringLiteral("yyyy-MM-dd"));
            if (!taskStart.isValid() || !taskEnd.isValid()) {
                ++invalidDateTasks;
                continue;
            }
            if (taskEnd < rangeStart || taskStart > rangeEnd) {
                continue;
            }
        }

        error.clear();
        const auto detailOpt = m_store.loadTask(summary.id, &error);
        if (!detailOpt.has_value()) {
            QMessageBox::warning(
                this,
                QStringLiteral("导出失败"),
                error.isEmpty()
                    ? QStringLiteral("无法读取任务“%1”。").arg(summary.name)
                    : QStringLiteral("读取任务“%1”失败：%2").arg(summary.name, error));
            return;
        }
        TaskDetail detail = *detailOpt;
        detail.summary.updatedAt = summary.updatedAt;
        error.clear();
        const QVector<Assignment> inherited =
            m_store.loadInheritedAssignments(detail.summary.baseTaskId, &error);
        if (!error.isEmpty()) {
            QMessageBox::warning(
                this,
                QStringLiteral("导出失败"),
                QStringLiteral("读取任务“%1”的继承安排失败：%2").arg(summary.name, error));
            return;
        }
        detail.assignments = mergeAssignments(inherited, detail.assignments);
        if (!detail.assignments.isEmpty()) {
            tasks.push_back(detail);
        }
    }

    if (tasks.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("提示"),
            exportAll
                ? QStringLiteral("当前没有可导出的代课安排。")
                : QStringLiteral("所选时间段内没有可导出的代课安排。"));
        return;
    }

    const QString rangeText = exportAll
        ? QStringLiteral("全部任务")
        : QStringLiteral("%1 至 %2")
              .arg(rangeStart.toString(QStringLiteral("yyyy-MM-dd")),
                   rangeEnd.toString(QStringLiteral("yyyy-MM-dd")));
    const QString fileRange = exportAll
        ? QStringLiteral("全部")
        : QStringLiteral("%1_%2")
              .arg(rangeStart.toString(QStringLiteral("yyyy-MM-dd")),
                   rangeEnd.toString(QStringLiteral("yyyy-MM-dd")));
    QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出代课统计"),
        m_baseDir + QLatin1Char('/')
            + QStringLiteral("代课统计_%1_%2.xlsx").arg(fileRange, nowForFileName()),
        QStringLiteral("Excel 工作簿 (*.xlsx)"));
    if (outputPath.isEmpty()) {
        return;
    }
    if (QFileInfo(outputPath).suffix().isEmpty()) {
        outputPath += QStringLiteral(".xlsx");
    }

    error.clear();
    if (!ExcelHelper::exportSubstituteStatistics(outputPath, tasks, rangeText, &error)) {
        QMessageBox::warning(
            this,
            QStringLiteral("导出失败"),
            error.isEmpty() ? QStringLiteral("无法导出代课统计。") : error);
        return;
    }

    QString message = QStringLiteral("代课统计已导出到：\n%1").arg(QDir::toNativeSeparators(outputPath));
    if (invalidDateTasks > 0) {
        message += QStringLiteral("\n\n另有 %1 个任务因日期无效未纳入统计。").arg(invalidDateTasks);
    }
    QMessageBox::information(this, QStringLiteral("导出完成"), message);
}

void MainWindow::restoreData()
{
    const QString backupPath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择数据备份"),
        m_baseDir,
        QStringLiteral("SQLite 数据库 (*.db);;所有文件 (*)"));
    if (backupPath.isEmpty()) {
        return;
    }

    const auto answer = QMessageBox::warning(
        this,
        QStringLiteral("确认恢复数据"),
        QStringLiteral(
            "恢复操作将使用所选备份覆盖当前课表、任务、代课安排和设置。\n\n"
            "备份文件：\n%1\n\n"
            "程序会先自动备份当前数据。确定继续吗？")
            .arg(QDir::toNativeSeparators(backupPath)),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    const QString databaseDir = QFileInfo(m_store.databasePath()).absolutePath();
    const QString safetyBackupPath = QDir(databaseDir).filePath(
        QStringLiteral("恢复前自动备份_%1.db").arg(nowForFileName()));
    QString error;
    if (!m_store.backupDatabase(safetyBackupPath, &error)) {
        QMessageBox::warning(
            this,
            QStringLiteral("恢复已取消"),
            QStringLiteral("无法在恢复前备份当前数据：%1").arg(error));
        return;
    }

    error.clear();
    if (!m_store.restoreDatabase(backupPath, &error)) {
        QMessageBox::warning(
            this,
            QStringLiteral("恢复失败"),
            QStringLiteral("数据恢复失败：%1\n\n当前数据未被修改。")
                .arg(error.isEmpty() ? QStringLiteral("未知错误") : error));
        return;
    }

    error.clear();
    const QVector<ScheduleEntry> restoredSchedule = m_store.loadSchedule(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("恢复完成"),
            QStringLiteral("数据已恢复，但重新加载课表失败：%1\n请重新启动软件。").arg(error));
        return;
    }

    applySchedule(restoredSchedule);
    loadSettings();
    loadTaskList();
    m_tabs->setCurrentIndex(0);
    QMessageBox::information(
        this,
        QStringLiteral("恢复完成"),
        QStringLiteral(
            "备份数据已恢复，课表和任务列表已刷新。\n\n"
            "恢复前的当前数据已自动备份到：\n%1")
            .arg(QDir::toNativeSeparators(safetyBackupPath)));
}

void MainWindow::onFreeSelectionChanged()
{
    updateFreeTeacherList();
}

void MainWindow::copyFreeListSelection()
{
    if (!m_freeListTable->selectionModel()) {
        return;
    }

    QStringList names;
    const QModelIndexList rows = m_freeListTable->selectionModel()->selectedRows(0);
    for (const QModelIndex &index : rows) {
        if (QTableWidgetItem *item = m_freeListTable->item(index.row(), 0)) {
            const QString text = item->text().trimmed();
            if (!text.isEmpty()) {
                names << text;
            }
        }
    }

    if (!names.isEmpty()) {
        QApplication::clipboard()->setText(names.join(QLatin1Char(' ')));
    }
}

QString MainWindow::buildDefaultExportName() const
{
    QStringList names;
    QSet<QString> seen;
    for (const Assignment &assignment : m_assignments) {
        if (!assignment.absentTeacher.isEmpty() && !seen.contains(assignment.absentTeacher)) {
            seen.insert(assignment.absentTeacher);
            names << assignment.absentTeacher;
        }
    }

    QString prefix;
    if (names.isEmpty()) {
        prefix = QStringLiteral("请假代课");
    } else if (names.size() == 1) {
        prefix = names.first() + QStringLiteral("请假代课");
    } else {
        prefix = names.first() + QStringLiteral("等请假代课");
    }
    return safeFileComponent(prefix) + nowForFileName() + QStringLiteral(".xlsx");
}

QString MainWindow::buildDefaultFreeExportName() const
{
    return QStringLiteral("无课名单%1.xlsx").arg(nowForFileName());
}

void MainWindow::exportFreeTeachers()
{
    const QVector<QPair<QString, int>> selectedSlots = selectedFreeSlots();
    if (selectedSlots.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择节次。"));
        return;
    }

    const QVector<QString> teachers = computeFreeTeachers(selectedSlots);
    if (teachers.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("所选节次没有符合条件的无课老师。"));
        return;
    }

    const QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出无课名单"),
        m_baseDir + QLatin1Char('/') + buildDefaultFreeExportName(),
        QStringLiteral("Excel (*.xlsx *.xls)"));
    if (outputPath.isEmpty()) {
        return;
    }

    QString error;
    if (!ExcelHelper::exportFreeTeachers(outputPath, teachers, m_subjectMap, formatFreeSlots(selectedSlots), &error)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        return;
    }
    QMessageBox::information(this, QStringLiteral("导出完成"), QStringLiteral("已导出到: %1").arg(outputPath));
}

void MainWindow::exportAssignments()
{
    if (m_assignments.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("没有可导出的代课安排。"));
        return;
    }
    if (m_templatePath.isEmpty() || !QFileInfo::exists(m_templatePath)) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先在设置中选择通知单模板。"));
        return;
    }

    const QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出代课单"),
        m_baseDir + QLatin1Char('/') + buildDefaultExportName(),
        QStringLiteral("Excel (*.xlsx *.xls)"));
    if (outputPath.isEmpty()) {
        return;
    }

    QString error;
    if (!ExcelHelper::exportNotices(
            m_templatePath,
            outputPath,
            m_assignments,
            currentTaskReason(),
            m_taskStartDate->date().toString(QStringLiteral("yyyy-MM-dd")),
            m_taskEndDate->date().toString(QStringLiteral("yyyy-MM-dd")),
            &error)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        return;
    }
    QMessageBox::information(this, QStringLiteral("导出完成"), QStringLiteral("已导出到: %1").arg(outputPath));
}

void MainWindow::newTask()
{
    m_currentTaskId = 0;
    m_baseTaskId = selectedBaseTaskId();
    m_selectedSlots.clear();
    m_selectedSubTeacher.clear();
    updateClearSelectionButton();

    if (m_baseTaskId > 0) {
        QString error;
        const auto baseDetail = m_store.loadTask(m_baseTaskId, &error);
        m_inheritedAssignments = m_store.loadInheritedAssignments(m_baseTaskId, &error);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("读取基任务失败: %1").arg(error));
            return;
        }
        if (baseDetail.has_value()) {
            m_taskNameEdit->clear();
            applyTaskDates(baseDetail->summary.startDate, baseDetail->summary.endDate);
            setTaskReason(baseDetail->summary.reason, baseDetail->summary.name);
            setTaskEditSummaryText(
                QStringLiteral("已基于“%1”新建任务，课表已显示原任务代课，请设置名称")
                    .arg(baseDetail->summary.name));
        }
    } else {
        m_inheritedAssignments.clear();
        resetTaskFields();
        setTaskEditSummaryText(selectTeacherAndSlotsPrompt());
    }

    m_assignments.clear();
    refreshAssignmentTable();
    clearRecommendations();
    renderAbsentSchedule(false);
    renderSubSchedule();
    m_tabs->setCurrentIndex(1);
}

void MainWindow::openSelectedTask()
{
    const qint64 taskId = selectedTaskId();
    if (taskId <= 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请选择要打开的任务。"));
        return;
    }
    loadTask(taskId);
    m_tabs->setCurrentIndex(1);
}

void MainWindow::saveTask()
{
    const QString name = m_taskNameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请输入代课任务名称。"));
        return;
    }
    if (m_taskStartDate->date() > m_taskEndDate->date()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("起始日期不能晚于结束日期。"));
        return;
    }

    TaskDetail detail;
    detail.summary.name = name;
    detail.summary.startDate = m_taskStartDate->date().toString(QStringLiteral("yyyy-MM-dd"));
    detail.summary.endDate = m_taskEndDate->date().toString(QStringLiteral("yyyy-MM-dd"));
    detail.summary.reason = currentTaskReason();
    detail.summary.baseTaskId = m_baseTaskId;
    detail.assignments = m_assignments;

    QString error;
    qint64 savedId = 0;
    if (!m_store.saveTask(detail, m_currentTaskId, &savedId, &error)) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("保存任务失败: %1").arg(error));
        return;
    }

    m_currentTaskId = savedId;
    loadTaskList();
    selectTaskInTable(savedId);
    QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("代课任务已保存。"));
}

void MainWindow::deleteTask()
{
    const qint64 taskId = selectedTaskId() > 0 ? selectedTaskId() : m_currentTaskId;
    if (taskId <= 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请选择要删除的任务。"));
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确定删除当前代课任务及其安排吗？"));
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_store.deleteTask(taskId, &error)) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("删除任务失败: %1").arg(error));
        return;
    }

    if (m_currentTaskId == taskId) {
        m_currentTaskId = 0;
        m_baseTaskId = 0;
        m_assignments.clear();
        m_inheritedAssignments.clear();
        refreshAssignmentTable();
        clearRecommendations();
        renderAbsentSchedule(false);
        renderSubSchedule();
        resetTaskFields();
    }
    loadTaskList();
}

}  // namespace substitute
