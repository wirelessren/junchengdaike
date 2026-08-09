#include "excelhelper.h"

#include "coreutils.h"

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSet>
#include <QXmlStreamWriter>

#include <freexl.h>
#include <xlnt/xlnt.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <stdexcept>
#include <vector>

namespace substitute {

namespace {

constexpr int kHeaderDataRows = 2;
constexpr int kNoticeTemplateRows = 17;
constexpr int kNoticeTemplateColumns = 4;
const auto kSpreadsheetNs = QStringLiteral("urn:schemas-microsoft-com:office:spreadsheet");
const auto kOfficeNs = QStringLiteral("urn:schemas-microsoft-com:office:office");
const auto kExcelNs = QStringLiteral("urn:schemas-microsoft-com:office:excel");
const auto kHtmlNs = QStringLiteral("http://www.w3.org/TR/REC-html40");

const QStringList kNoticeHeaderLabels = {
    QStringLiteral("调代课班级"),
    QStringLiteral("调代课科目"),
    QStringLiteral("原上课时间"),
    QStringLiteral("现上课时间"),
};

struct SnapshotCell {
    bool hasValue = false;
    QString value;
    bool hasFormula = false;
    QString formula;
    bool hasStyle = false;
    xlnt::font font;
    xlnt::fill fill;
    xlnt::border border;
    xlnt::alignment alignment;
    xlnt::number_format numberFormat;
    xlnt::protection protection;
};

struct TemplateSnapshot {
    int height = 0;
    int width = 0;
    QVector<QVector<SnapshotCell>> cells;
    QMap<int, xlnt::row_properties> rowProperties;
    QVector<xlnt::range_reference> merges;
};

struct NoticeItem {
    enum class Kind {
        Absent,
        Substitute,
    };

    Kind kind = Kind::Absent;
    QString absentTeacher;
    QString substituteTeacher;
    QVector<Assignment> rows;
};

struct NoticeBlock {
    NoticeItem::Kind kind = NoticeItem::Kind::Absent;
    xlnt::row_t titleRow = 0;
    xlnt::row_t blankRow = 0;
    xlnt::row_t textRow = 0;
    xlnt::row_t headerRow = 0;
    QVector<xlnt::row_t> dataRows;
    xlnt::row_t timeRow = 0;
    xlnt::row_t arrangeRow = 0;
};

struct NoticeTemplateRows {
    int title = 0;
    int blank = 0;
    int text = 0;
    int header = 0;
    int data = 0;
    int time = 0;
    int arrange = 0;
};

void applyTemplateRowStyle(xlnt::worksheet &sheet, xlnt::row_t targetRow, int sourceRow, int maxColumn);

std::string toStdString(const QString &text)
{
    return text.toUtf8().toStdString();
}

bool isXlsPath(const QString &path)
{
    return QFileInfo(path).suffix().compare(QStringLiteral("xls"), Qt::CaseInsensitive) == 0;
}

[[maybe_unused]] bool isXlsxPath(const QString &path)
{
    return QFileInfo(path).suffix().compare(QStringLiteral("xlsx"), Qt::CaseInsensitive) == 0;
}

QString fromStdString(const std::string &text)
{
    return QString::fromUtf8(text.c_str()).trimmed();
}

QString freexlResultMessage(int code)
{
    switch (code) {
    case FREEXL_OK:
        return QStringLiteral("成功");
    case FREEXL_FILE_NOT_FOUND:
        return QStringLiteral("文件不存在或无法读取");
    case FREEXL_INVALID_HANDLE:
    case FREEXL_NULL_HANDLE:
        return QStringLiteral("无效的 Excel 句柄");
    case FREEXL_BIFF_ILLEGAL_SHEET_INDEX:
    case FREEXL_XLSX_ILLEGAL_SHEET_INDEX:
        return QStringLiteral("工作表索引无效");
    case FREEXL_INVALID_CFBF_HEADER:
    case FREEXL_CFBF_INVALID_SIGNATURE:
        return QStringLiteral("不是有效的 xls 文件");
    default:
        return QStringLiteral("错误码 %1").arg(code);
    }
}

std::vector<std::uint8_t> readBinaryFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(toStdString(file.errorString()));
    }

    const QByteArray content = file.readAll();
    return std::vector<std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(content.constData()),
        reinterpret_cast<const std::uint8_t *>(content.constData()) + content.size());
}

void writeBinaryFile(const QString &path, const std::vector<std::uint8_t> &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        throw std::runtime_error(toStdString(file.errorString()));
    }

    const auto bytesToWrite = static_cast<qint64>(content.size());
    const qint64 bytesWritten = file.write(
        reinterpret_cast<const char *>(content.data()),
        bytesToWrite);
    if (bytesWritten != bytesToWrite) {
        throw std::runtime_error(toStdString(file.errorString()));
    }
}

void loadWorkbook(xlnt::workbook &workbook, const QString &path)
{
    workbook.load(readBinaryFile(path));
}

void saveWorkbook(const xlnt::workbook &workbook, const QString &path)
{
    std::vector<std::uint8_t> content;
    workbook.save(content);
    writeBinaryFile(path, content);
}

xlnt::column_t column(int index)
{
    return xlnt::column_t(static_cast<xlnt::column_t::index_t>(index));
}

xlnt::row_t row(int index)
{
    return static_cast<xlnt::row_t>(index);
}

QString cellText(xlnt::worksheet &sheet, xlnt::row_t row, int columnIndex)
{
    const auto cell = sheet.cell(column(columnIndex), row);
    if (!cell.has_value()) {
        return {};
    }
    return fromStdString(cell.to_string());
}

void setCellValue(xlnt::worksheet &sheet, xlnt::row_t row, int columnIndex, const QString &value)
{
    sheet.cell(column(columnIndex), row).value(toStdString(value));
}

void setCellValue(xlnt::worksheet &sheet, xlnt::row_t row, int columnIndex, int value)
{
    sheet.cell(column(columnIndex), row).value(value);
}

void clearCellValue(xlnt::worksheet &sheet, xlnt::row_t row, int columnIndex)
{
    sheet.cell(column(columnIndex), row).clear_value();
}

void clearRowValues(xlnt::worksheet &sheet, xlnt::row_t row, int maxColumn = kNoticeTemplateColumns)
{
    for (int columnIndex = 1; columnIndex <= maxColumn; ++columnIndex) {
        clearCellValue(sheet, row, columnIndex);
    }
}

int dayOrder(const QString &day)
{
    const int index = kDays.indexOf(day);
    return index >= 0 ? index : 99;
}

QVector<Assignment> sortedAssignments(QVector<Assignment> rows)
{
    std::sort(rows.begin(), rows.end(), [](const Assignment &left, const Assignment &right) {
        const int leftDay = dayOrder(left.day);
        const int rightDay = dayOrder(right.day);
        if (leftDay != rightDay) {
            return leftDay < rightDay;
        }
        if (left.period != right.period) {
            return left.period < right.period;
        }
        return left.className < right.className;
    });
    return rows;
}

QString buildNoticeReasonText(const QString &absentTeacher, const QString &reason)
{
    return QStringLiteral("因%1老师%2，需要调代课，具体调整情况如下：").arg(absentTeacher, reason);
}

[[maybe_unused]] TemplateSnapshot snapshotTemplateBlock(xlnt::worksheet &sheet)
{
    TemplateSnapshot snapshot;
    snapshot.height = qMax(static_cast<int>(sheet.highest_row()), kNoticeTemplateRows);
    snapshot.width = qMax(static_cast<int>(sheet.highest_column().index), kNoticeTemplateColumns);
    snapshot.cells.reserve(snapshot.height);

    for (int rowIndex = 1; rowIndex <= snapshot.height; ++rowIndex) {
        try {
            if (sheet.has_row_properties(row(rowIndex))) {
                snapshot.rowProperties.insert(rowIndex, sheet.row_properties(row(rowIndex)));
            }
        } catch (const std::exception &ex) {
            throw std::runtime_error(
                QStringLiteral("读取模板行样式失败: 第%1行, %2")
                    .arg(rowIndex)
                    .arg(QString::fromLocal8Bit(ex.what()))
                    .toStdString());
        }

        QVector<SnapshotCell> rowCells;
        rowCells.reserve(snapshot.width);
        for (int columnIndex = 1; columnIndex <= snapshot.width; ++columnIndex) {
            SnapshotCell cellSnapshot;
            const auto sourceCell = sheet.cell(column(columnIndex), row(rowIndex));
            if (sourceCell.has_formula()) {
                cellSnapshot.hasFormula = true;
                cellSnapshot.formula = fromStdString(sourceCell.formula());
            } else if (sourceCell.has_value()) {
                cellSnapshot.hasValue = true;
                cellSnapshot.value = fromStdString(sourceCell.to_string());
            }
            rowCells.push_back(std::move(cellSnapshot));
        }
        snapshot.cells.push_back(std::move(rowCells));
    }

    for (const auto &mergeRange : sheet.merged_ranges()) {
        snapshot.merges.push_back(mergeRange);
    }
    return snapshot;
}

void applySnapshotCellValue(xlnt::cell targetCell, const SnapshotCell &snapshotCell)
{
    if (snapshotCell.hasFormula) {
        targetCell.formula(toStdString(snapshotCell.formula));
    } else {
        targetCell.clear_formula();
        if (snapshotCell.hasValue) {
            targetCell.value(toStdString(snapshotCell.value));
        } else {
            targetCell.clear_value();
        }
    }
}

[[maybe_unused]] void appendTemplateBlock(xlnt::worksheet &sheet, const TemplateSnapshot &snapshot, xlnt::row_t startRow)
{
    const int rowOffset = static_cast<int>(startRow) - 1;
    try {
        for (int rowIndex = 1; rowIndex <= snapshot.height; ++rowIndex) {
            const xlnt::row_t targetRow = row(rowOffset + rowIndex);
            for (int columnIndex = 1; columnIndex <= snapshot.width; ++columnIndex) {
                applySnapshotCellValue(
                    sheet.cell(column(columnIndex), targetRow),
                    snapshot.cells.at(rowIndex - 1).at(columnIndex - 1));
            }
        }
    } catch (const std::exception &ex) {
        throw std::runtime_error(std::string("复制模板块内容失败: ") + ex.what());
    }
}

void applySnapshotRowStyle(
    xlnt::worksheet &sheet,
    const TemplateSnapshot &snapshot,
    xlnt::row_t targetRow,
    int sourceRow,
    int maxColumn = kNoticeTemplateColumns)
{
    if (sourceRow < 1 || sourceRow > snapshot.height) {
        return;
    }

    if (snapshot.rowProperties.contains(sourceRow)) {
        sheet.row_properties(targetRow) = snapshot.rowProperties.value(sourceRow);
    }
    applyTemplateRowStyle(sheet, targetRow, sourceRow, qMin(maxColumn, snapshot.width));
}

[[maybe_unused]] NoticeTemplateRows absentTemplateRows()
{
    return {1, 2, 3, 4, 5, 7, 8};
}

[[maybe_unused]] NoticeTemplateRows subTemplateRows()
{
    return {10, 11, 12, 13, 14, 16, 17};
}

[[maybe_unused]] QVector<NoticeBlock> findNoticeBlocks(xlnt::worksheet &sheet)
{
    QVector<NoticeBlock> blocks;
    const int maxRow = static_cast<int>(sheet.highest_row());
    int rowIndex = 1;
    while (rowIndex <= maxRow) {
        const QString title = cellText(sheet, row(rowIndex), 1).trimmed();
        if (!title.endsWith(QStringLiteral("老师调代课通知"))) {
            ++rowIndex;
            continue;
        }

        int textRow = 0;
        int headerRow = 0;
        int timeRow = 0;
        for (int scanRow = rowIndex + 1; scanRow <= maxRow; ++scanRow) {
            const QString columnA = cellText(sheet, row(scanRow), 1).trimmed();
            const QString columnB = cellText(sheet, row(scanRow), 2).trimmed();
            if (textRow == 0 && columnA.contains(QStringLiteral("需要调代课"))) {
                textRow = scanRow;
            } else if (textRow != 0 && headerRow == 0 && columnA == kNoticeHeaderLabels.at(0)) {
                headerRow = scanRow;
            } else if (headerRow != 0 && columnB.startsWith(QStringLiteral("代课时间:"))) {
                timeRow = scanRow;
                break;
            }
        }

        if (textRow == 0 || headerRow == 0 || timeRow == 0) {
            ++rowIndex;
            continue;
        }

        QVector<xlnt::row_t> dataRows;
        NoticeItem::Kind kind = NoticeItem::Kind::Absent;
        for (int dataRow = headerRow + 1; dataRow < timeRow; ++dataRow) {
            dataRows.push_back(row(dataRow));
            const QString originalTime = cellText(sheet, row(dataRow), 3).trimmed();
            const QString newTime = cellText(sheet, row(dataRow), 4).trimmed();
            if (!newTime.isEmpty() && originalTime.isEmpty()) {
                kind = NoticeItem::Kind::Substitute;
            }
        }

        blocks.push_back(
            {kind,
             row(rowIndex),
             row(rowIndex + 1),
             row(textRow),
             row(headerRow),
             dataRows,
             row(timeRow),
             row(timeRow + 1)});
        rowIndex = timeRow + 2;
    }
    return blocks;
}

xlnt::border::border_property cloneBorderProperty(
    const xlnt::optional<xlnt::border::border_property> &source,
    xlnt::border_style style)
{
    xlnt::border::border_property property;
    property.style(style);
    if (source.is_set()) {
        const auto color = source.get().color();
        if (color.is_set()) {
            property.color(color.get());
        }
    }
    return property;
}

void normalizeDataBlockBorders(
    xlnt::worksheet &sheet,
    xlnt::row_t dataStart,
    int totalRows,
    int maxColumn = kNoticeTemplateColumns)
{
    if (totalRows <= 0) {
        return;
    }

    for (int index = 0; index < totalRows; ++index) {
        const xlnt::row_t currentRow = row(static_cast<int>(dataStart) + index);
        const bool lastRow = index == totalRows - 1;
        for (int columnIndex = 1; columnIndex <= maxColumn; ++columnIndex) {
            auto currentCell = sheet.cell(column(columnIndex), currentRow);
            xlnt::border currentBorder = currentCell.border();
            currentBorder.side(
                xlnt::border_side::bottom,
                cloneBorderProperty(
                    currentBorder.side(xlnt::border_side::bottom),
                    lastRow ? xlnt::border_style::thick : xlnt::border_style::thin));
            currentCell.border(currentBorder);
        }
    }
}

bool hasMergedRange(xlnt::worksheet &sheet, const xlnt::range_reference &targetRange)
{
    for (const auto &existingRange : sheet.merged_ranges()) {
        if (existingRange == targetRange) {
            return true;
        }
    }
    return false;
}

void ensureMergedRange(xlnt::worksheet &sheet, const xlnt::range_reference &targetRange)
{
    if (!hasMergedRange(sheet, targetRange)) {
        sheet.merge_cells(targetRange);
    }
}

void formatNoticeBlock(
    xlnt::worksheet &sheet,
    xlnt::row_t startRow,
    int dataCount,
    NoticeItem::Kind kind)
{
    const int totalRows = qMax(1, dataCount);
    const xlnt::row_t titleRow = startRow;
    const xlnt::row_t blankRow = row(static_cast<int>(startRow) + 1);
    const xlnt::row_t textRow = row(static_cast<int>(startRow) + 2);
    const xlnt::row_t headerRow = row(static_cast<int>(startRow) + 3);
    const xlnt::row_t dataStart = row(static_cast<int>(startRow) + 4);
    const xlnt::row_t timeRow = row(static_cast<int>(dataStart) + totalRows);
    const xlnt::row_t arrangeRow = row(static_cast<int>(timeRow) + 1);

    applyTemplateRowStyle(sheet, titleRow, 1, kNoticeTemplateColumns);
    applyTemplateRowStyle(sheet, blankRow, 2, kNoticeTemplateColumns);
    applyTemplateRowStyle(
        sheet,
        textRow,
        kind == NoticeItem::Kind::Substitute ? 12 : 3,
        kNoticeTemplateColumns);
    applyTemplateRowStyle(sheet, headerRow, 4, kNoticeTemplateColumns);
    for (int index = 0; index < totalRows; ++index) {
        applyTemplateRowStyle(sheet, row(static_cast<int>(dataStart) + index), 5, kNoticeTemplateColumns);
    }
    normalizeDataBlockBorders(sheet, dataStart, totalRows);
    applyTemplateRowStyle(sheet, timeRow, 7, kNoticeTemplateColumns);
    applyTemplateRowStyle(sheet, arrangeRow, 8, kNoticeTemplateColumns);

    sheet.row_properties(titleRow).height = 23.25;
    sheet.row_properties(titleRow).custom_height = true;
    sheet.row_properties(headerRow).height = 15.75;
    sheet.row_properties(headerRow).custom_height = true;
    for (int index = 0; index < totalRows; ++index) {
        xlnt::row_t currentDataRow = row(static_cast<int>(dataStart) + index);
        sheet.row_properties(currentDataRow).height = 15.75;
        sheet.row_properties(currentDataRow).custom_height = true;
    }

    for (int columnIndex = 1; columnIndex <= kNoticeHeaderLabels.size(); ++columnIndex) {
        setCellValue(sheet, headerRow, columnIndex, kNoticeHeaderLabels.at(columnIndex - 1));
    }

    ensureMergedRange(
        sheet,
        xlnt::range_reference(column(1), titleRow, column(kNoticeTemplateColumns), titleRow));
    ensureMergedRange(
        sheet,
        xlnt::range_reference(column(1), textRow, column(kNoticeTemplateColumns), textRow));
}

void initializeNoticeSheet(xlnt::worksheet &sheet)
{
    sheet.title(toStdString(QStringLiteral("调代课通知")));
    sheet.column_properties(column(1)).width = 15.7142857142857;
    sheet.column_properties(column(1)).custom_width = true;
    sheet.column_properties(column(2)).width = 31.7142857142857;
    sheet.column_properties(column(2)).custom_width = true;
    sheet.column_properties(column(3)).width = 15.7142857142857;
    sheet.column_properties(column(3)).custom_width = true;
    sheet.column_properties(column(4)).width = 13.0;
    sheet.column_properties(column(4)).custom_width = true;
}

int noticeBlockHeight(NoticeItem::Kind kind, int dataCount)
{
    const int totalRows = qMax(1, dataCount);
    return totalRows + (kind == NoticeItem::Kind::Absent ? 7 : 6);
}

void writeNoticeBlock(
    xlnt::worksheet &sheet,
    xlnt::row_t startRow,
    NoticeItem::Kind kind,
    const QString &absentTeacher,
    const QString &substituteTeacher,
    QVector<Assignment> rows,
    const QString &reason,
    const QString &startDate,
    const QString &endDate)
{
    rows = sortedAssignments(std::move(rows));

    const int totalRows = qMax(1, rows.size());
    const xlnt::row_t titleRow = startRow;
    const xlnt::row_t textRow = row(static_cast<int>(startRow) + 2);
    const xlnt::row_t dataStart = row(static_cast<int>(startRow) + 4);
    const xlnt::row_t timeRow = row(static_cast<int>(dataStart) + totalRows);
    const xlnt::row_t arrangeRow = row(static_cast<int>(timeRow) + 1);
    const xlnt::row_t trailingBlankRow = row(static_cast<int>(arrangeRow) + 1);

    const QString titleTeacher =
        kind == NoticeItem::Kind::Substitute ? substituteTeacher : absentTeacher;
    setCellValue(sheet, titleRow, 1, QStringLiteral("%1 老师调代课通知").arg(titleTeacher));
    setCellValue(sheet, textRow, 1, buildNoticeReasonText(absentTeacher, reason));

    for (int index = 0; index < totalRows; ++index) {
        const xlnt::row_t targetRow = row(static_cast<int>(dataStart) + index);
        if (index >= rows.size()) {
            clearRowValues(sheet, targetRow);
            continue;
        }

        const Assignment &assignment = rows.at(index);
        const QString timeText = QStringLiteral("%1第%2节").arg(assignment.day).arg(assignment.period);
        setCellValue(sheet, targetRow, 1, assignment.className);
        setCellValue(sheet, targetRow, 2, assignment.subject);
        if (kind == NoticeItem::Kind::Substitute) {
            clearCellValue(sheet, targetRow, 3);
            setCellValue(sheet, targetRow, 4, timeText);
        } else {
            setCellValue(sheet, targetRow, 3, timeText);
            clearCellValue(sheet, targetRow, 4);
        }
    }

    setCellValue(sheet, timeRow, 2, QStringLiteral("代课时间:%1～%2").arg(startDate, endDate));
    setCellValue(sheet, timeRow, 4, QStringLiteral("高一年级"));
    setCellValue(sheet, arrangeRow, 4, QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));

    if (kind == NoticeItem::Kind::Absent) {
        sheet.row_properties(trailingBlankRow).height = 30.0;
        sheet.row_properties(trailingBlankRow).custom_height = true;
    }

    formatNoticeBlock(sheet, startRow, rows.size(), kind);
}

QVector<NoticeItem> buildNoticeItems(const QVector<Assignment> &assignments)
{
    QMap<QString, QVector<Assignment>> absentMap;
    QMap<QString, QVector<Assignment>> pairMap;
    for (const Assignment &assignment : assignments) {
        absentMap[assignment.absentTeacher].push_back(assignment);
        pairMap[assignment.absentTeacher + QStringLiteral("|") + assignment.substituteTeacher].push_back(assignment);
    }

    QVector<NoticeItem> items;
    for (auto it = absentMap.cbegin(); it != absentMap.cend(); ++it) {
        items.push_back({NoticeItem::Kind::Absent, it.key(), {}, it.value()});
    }
    for (auto it = pairMap.cbegin(); it != pairMap.cend(); ++it) {
        const QStringList parts = it.key().split(QStringLiteral("|"));
        if (parts.size() != 2) {
            continue;
        }
        items.push_back({NoticeItem::Kind::Substitute, parts.at(0), parts.at(1), it.value()});
    }
    return items;
}

QString freexlCellText(const FreeXL_CellValue &value)
{
    switch (value.type) {
    case FREEXL_CELL_INT:
        return QString::number(value.value.int_value);
    case FREEXL_CELL_DOUBLE:
        return QString::number(value.value.double_value, 'g', 15);
    case FREEXL_CELL_TEXT:
    case FREEXL_CELL_SST_TEXT:
    case FREEXL_CELL_DATE:
    case FREEXL_CELL_DATETIME:
    case FREEXL_CELL_TIME:
        return value.value.text_value ? QString::fromUtf8(value.value.text_value).trimmed() : QString();
    case FREEXL_CELL_NULL:
    default:
        return {};
    }
}

bool freexlReadCell(const void *handle, unsigned int rowIndex, unsigned short columnIndex, QString *text, QString *error)
{
    FreeXL_CellValue value;
    const int rc = freexl_get_cell_value(handle, rowIndex, columnIndex, &value);
    if (rc != FREEXL_OK) {
        if (error) {
            *error = QStringLiteral("读取 xls 单元格失败(%1,%2): %3")
                .arg(rowIndex + 1)
                .arg(columnIndex + 1)
                .arg(freexlResultMessage(rc));
        }
        return false;
    }
    if (text) {
        *text = freexlCellText(value);
    }
    return true;
}

struct ScheduleColumnMapping {
    int headerRow = 0;
    std::array<int, 5> columns = {-1, -1, -1, -1, -1};
    int recognizedCount = 0;

    bool complete() const
    {
        return std::all_of(columns.cbegin(), columns.cend(), [](int value) { return value > 0; });
    }
};

QString normalizedHeader(QString text)
{
    text = text.trimmed().toLower();
    static const QRegularExpression spacing(QStringLiteral("[\\s　:：]+"));
    text.remove(spacing);
    return text;
}

int scheduleFieldForHeader(const QString &text)
{
    static const QHash<QString, int> aliases = {
        {QStringLiteral("科目"), 0},
        {QStringLiteral("学科"), 0},
        {QStringLiteral("课程"), 0},
        {QStringLiteral("课程名称"), 0},
        {QStringLiteral("任教学科"), 0},
        {QStringLiteral("姓名"), 1},
        {QStringLiteral("教师"), 1},
        {QStringLiteral("老师"), 1},
        {QStringLiteral("教师姓名"), 1},
        {QStringLiteral("任课教师"), 1},
        {QStringLiteral("周几"), 2},
        {QStringLiteral("星期"), 2},
        {QStringLiteral("星期几"), 2},
        {QStringLiteral("上课日"), 2},
        {QStringLiteral("节次"), 3},
        {QStringLiteral("课次"), 3},
        {QStringLiteral("课节"), 3},
        {QStringLiteral("第几节"), 3},
        {QStringLiteral("节"), 3},
        {QStringLiteral("班级"), 4},
        {QStringLiteral("班级名称"), 4},
        {QStringLiteral("行政班"), 4},
        {QStringLiteral("教学班"), 4},
        {QStringLiteral("上课班级"), 4},
    };
    return aliases.value(normalizedHeader(text), -1);
}

ScheduleColumnMapping detectScheduleColumns(
    int rowCount,
    int columnCount,
    const std::function<QString(int, int)> &readCell)
{
    ScheduleColumnMapping best;
    const int scanRows = qMin(rowCount, 10);
    const int scanColumns = qMin(columnCount, 40);
    for (int rowIndex = 1; rowIndex <= scanRows; ++rowIndex) {
        ScheduleColumnMapping current;
        current.headerRow = rowIndex;
        for (int columnIndex = 1; columnIndex <= scanColumns; ++columnIndex) {
            const int field = scheduleFieldForHeader(readCell(rowIndex, columnIndex));
            if (field >= 0 && current.columns.at(field) < 0) {
                current.columns[field] = columnIndex;
                ++current.recognizedCount;
            }
        }
        if (current.recognizedCount > best.recognizedCount) {
            best = current;
        }
        if (current.complete()) {
            return current;
        }
    }
    return best;
}

QString missingScheduleHeaders(const ScheduleColumnMapping &mapping)
{
    static const QStringList names = {
        QStringLiteral("科目"),
        QStringLiteral("姓名"),
        QStringLiteral("周几"),
        QStringLiteral("节次"),
        QStringLiteral("班级"),
    };
    QStringList missing;
    for (int index = 0; index < static_cast<int>(mapping.columns.size()); ++index) {
        if (mapping.columns.at(index) <= 0) {
            missing.push_back(names.at(index));
        }
    }
    return missing.join(QStringLiteral("、"));
}

bool isMissingValue(const QString &value)
{
    const QString text = value.trimmed();
    return text.isEmpty()
        || text.compare(QStringLiteral("nan"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0;
}

QVector<ScheduleEntry> parseScheduleRows(
    int rowCount,
    const ScheduleColumnMapping &mapping,
    const std::function<QString(int, int)> &readCell,
    const QString &sourceName,
    QString *error,
    QString *warning)
{
    QVector<ScheduleEntry> rows;
    QSet<QString> seen;
    int invalidRows = 0;
    int duplicateRows = 0;
    for (int rowIndex = mapping.headerRow + 1; rowIndex <= rowCount; ++rowIndex) {
        const QString subject = readCell(rowIndex, mapping.columns.at(0)).trimmed();
        const QString teacher = readCell(rowIndex, mapping.columns.at(1)).trimmed();
        const QString rawDay = readCell(rowIndex, mapping.columns.at(2)).trimmed();
        const QString rawPeriod = readCell(rowIndex, mapping.columns.at(3)).trimmed();
        const QString className = readCell(rowIndex, mapping.columns.at(4)).trimmed();

        if (subject.isEmpty() && teacher.isEmpty() && rawDay.isEmpty()
            && rawPeriod.isEmpty() && className.isEmpty()) {
            continue;
        }

        ScheduleEntry entry;
        entry.subject = subject;
        entry.teacher = teacher;
        entry.day = normalizeDay(rawDay);
        entry.period = normalizePeriod(rawPeriod);
        entry.className = className;
        if (isMissingValue(entry.subject)
            || isMissingValue(entry.teacher)
            || isMissingValue(entry.day)
            || isMissingValue(entry.className)
            || !kDays.contains(entry.day)
            || entry.period <= 0
            || entry.period > 30) {
            ++invalidRows;
            continue;
        }

        const QString key = QStringLiteral("%1\x1f%2\x1f%3\x1f%4\x1f%5")
            .arg(entry.subject, entry.teacher, entry.day, QString::number(entry.period), entry.className);
        if (seen.contains(key)) {
            ++duplicateRows;
            continue;
        }
        seen.insert(key);
        rows.push_back(entry);
    }

    if (rows.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "工作表“%1”中没有可用课表数据。请检查教师、科目、班级、周几和节次，"
                "目前支持周一至周日、第1至30节。")
                         .arg(sourceName);
        }
        return rows;
    }

    QStringList warnings;
    if (mapping.headerRow > 1) {
        warnings << QStringLiteral("已自动识别第 %1 行为表头").arg(mapping.headerRow);
    }
    if (invalidRows > 0) {
        warnings << QStringLiteral("跳过 %1 行格式不完整或超出周一至周日、第1至30节范围的数据").arg(invalidRows);
    }
    if (duplicateRows > 0) {
        warnings << QStringLiteral("忽略 %1 行重复数据").arg(duplicateRows);
    }
    if (warning && !warnings.isEmpty()) {
        *warning = warnings.join(QStringLiteral("；")) + QStringLiteral("。");
    }
    return rows;
}

bool validateNoticeTemplateCells(
    int rowCount,
    int columnCount,
    const std::function<QString(int, int)> &readCell,
    QString *error)
{
    if (rowCount < kNoticeTemplateRows || columnCount < kNoticeTemplateColumns) {
        if (error) {
            *error = QStringLiteral("模板格式不正确，至少需要 17 行 4 列内容。");
        }
        return false;
    }

    for (int headerRow : {4, 13}) {
        for (int columnIndex = 1; columnIndex <= kNoticeHeaderLabels.size(); ++columnIndex) {
            if (normalizedHeader(readCell(headerRow, columnIndex))
                != normalizedHeader(kNoticeHeaderLabels.at(columnIndex - 1))) {
                if (error) {
                    *error = QStringLiteral("模板第 %1 行表头不正确，应依次为：%2。")
                                 .arg(headerRow)
                                 .arg(kNoticeHeaderLabels.join(QStringLiteral("、")));
                }
                return false;
            }
        }
    }

    if (!readCell(1, 1).contains(QStringLiteral("调代课通知"))
        || !readCell(10, 1).contains(QStringLiteral("调代课通知"))) {
        if (error) {
            *error = QStringLiteral("模板第 1 行和第 10 行必须包含“调代课通知”标题。");
        }
        return false;
    }
    if (!readCell(3, 1).contains(QStringLiteral("需要调代课"))
        || !readCell(12, 1).contains(QStringLiteral("需要调代课"))) {
        if (error) {
            *error = QStringLiteral("模板第 3 行和第 12 行缺少调代课说明文字。");
        }
        return false;
    }
    if (!readCell(7, 2).contains(QStringLiteral("代课时间"))
        || !readCell(16, 2).contains(QStringLiteral("代课时间"))) {
        if (error) {
            *error = QStringLiteral("模板第 7 行和第 16 行缺少“代课时间”字段。");
        }
        return false;
    }
    return true;
}

bool validateNoticeTemplateXls(const QString &path, QString *error)
{
    const void *handle = nullptr;
    const QByteArray nativePath = QFile::encodeName(path);
    const int openResult = freexl_open(nativePath.constData(), &handle);
    if (openResult != FREEXL_OK || handle == nullptr) {
        if (error) {
            *error = QStringLiteral("无法读取 xls 模板: %1").arg(freexlResultMessage(openResult));
        }
        return false;
    }
    const auto closeHandle = qScopeGuard([&handle]() { freexl_close(handle); });
    unsigned int worksheetCount = 0;
    if (freexl_get_worksheets_count(handle, &worksheetCount) != FREEXL_OK || worksheetCount == 0
        || freexl_select_active_worksheet(handle, 0) != FREEXL_OK) {
        if (error) {
            *error = QStringLiteral("xls 模板中没有可用工作表。");
        }
        return false;
    }
    unsigned int lastRow = 0;
    unsigned short lastColumn = 0;
    if (freexl_worksheet_dimensions(handle, &lastRow, &lastColumn) != FREEXL_OK) {
        if (error) {
            *error = QStringLiteral("无法读取 xls 模板范围。");
        }
        return false;
    }
    bool readFailed = false;
    auto readCell = [&](int rowIndex, int columnIndex) {
        QString text;
        if (!freexlReadCell(
                handle,
                static_cast<unsigned int>(rowIndex - 1),
                static_cast<unsigned short>(columnIndex - 1),
                &text,
                error)) {
            readFailed = true;
        }
        return text;
    };
    const bool valid = validateNoticeTemplateCells(
        static_cast<int>(lastRow) + 1,
        static_cast<int>(lastColumn) + 1,
        readCell,
        error);
    return valid && !readFailed;
}

QVector<ScheduleEntry> readScheduleXls(const QString &path, QString *error, QString *warning)
{
    QVector<ScheduleEntry> rows;
    const void *handle = nullptr;
    const QByteArray nativePath = QFile::encodeName(path);
    const int openResult = freexl_open(nativePath.constData(), &handle);
    if (openResult != FREEXL_OK || handle == nullptr) {
        if (error) {
            *error = QStringLiteral("读取 xls 课表失败: %1").arg(freexlResultMessage(openResult));
        }
        return rows;
    }

    const auto closeHandle = qScopeGuard([&handle]() {
        if (handle != nullptr) {
            freexl_close(handle);
        }
    });

    unsigned int worksheetCount = 0;
    if (freexl_get_worksheets_count(handle, &worksheetCount) != FREEXL_OK || worksheetCount == 0) {
        if (error) {
            *error = QStringLiteral("xls 课表中没有工作表。");
        }
        return {};
    }
    int selectedSheet = -1;
    int fallbackSheet = -1;
    ScheduleColumnMapping selectedMapping;
    ScheduleColumnMapping bestPartial;
    bool readFailed = false;
    for (unsigned int sheetIndex = 0; sheetIndex < worksheetCount; ++sheetIndex) {
        if (freexl_select_active_worksheet(handle, static_cast<unsigned short>(sheetIndex)) != FREEXL_OK) {
            continue;
        }
        unsigned int lastRow = 0;
        unsigned short lastColumn = 0;
        if (freexl_worksheet_dimensions(handle, &lastRow, &lastColumn) != FREEXL_OK) {
            continue;
        }
        const int rowCount = static_cast<int>(lastRow) + 1;
        const int columnCount = static_cast<int>(lastColumn) + 1;
        if (fallbackSheet < 0 && rowCount >= 2 && columnCount >= 5) {
            fallbackSheet = static_cast<int>(sheetIndex);
        }
        auto readCell = [&](int rowIndex, int columnIndex) {
            QString text;
            if (!freexlReadCell(
                    handle,
                    static_cast<unsigned int>(rowIndex - 1),
                    static_cast<unsigned short>(columnIndex - 1),
                    &text,
                    error)) {
                readFailed = true;
            }
            return text;
        };
        const ScheduleColumnMapping mapping = detectScheduleColumns(rowCount, columnCount, readCell);
        if (readFailed) {
            return {};
        }
        if (mapping.complete()) {
            selectedSheet = static_cast<int>(sheetIndex);
            selectedMapping = mapping;
            break;
        }
        if (mapping.recognizedCount > bestPartial.recognizedCount) {
            bestPartial = mapping;
        }
    }

    if (selectedSheet < 0 && bestPartial.recognizedCount > 0) {
        if (error) {
            *error = QStringLiteral("课表表头不完整，缺少：%1。").arg(missingScheduleHeaders(bestPartial));
        }
        return {};
    }
    if (selectedSheet < 0 && fallbackSheet >= 0) {
        selectedSheet = fallbackSheet;
        selectedMapping.headerRow = 1;
        selectedMapping.columns = {1, 2, 3, 4, 5};
        selectedMapping.recognizedCount = 5;
    }
    if (selectedSheet < 0
        || freexl_select_active_worksheet(handle, static_cast<unsigned short>(selectedSheet)) != FREEXL_OK) {
        if (error) {
            *error = QStringLiteral("xls 课表中没有可识别的数据工作表。");
        }
        return {};
    }

    unsigned int lastRow = 0;
    unsigned short lastColumn = 0;
    if (freexl_worksheet_dimensions(handle, &lastRow, &lastColumn) != FREEXL_OK) {
        if (error) {
            *error = QStringLiteral("无法读取 xls 课表范围。");
        }
        return {};
    }
    const char *sheetNameText = nullptr;
    freexl_get_worksheet_name(handle, static_cast<unsigned short>(selectedSheet), &sheetNameText);
    const QString sheetName = sheetNameText ? QString::fromUtf8(sheetNameText) : QString::number(selectedSheet + 1);
    auto readCell = [&](int rowIndex, int columnIndex) {
        QString text;
        if (!freexlReadCell(
                handle,
                static_cast<unsigned int>(rowIndex - 1),
                static_cast<unsigned short>(columnIndex - 1),
                &text,
                error)) {
            readFailed = true;
        }
        return text;
    };
    rows = parseScheduleRows(
        static_cast<int>(lastRow) + 1,
        selectedMapping,
        readCell,
        sheetName,
        error,
        warning);
    return readFailed ? QVector<ScheduleEntry>() : rows;
}

QString xmlNumber(double value)
{
    QString text = QString::number(value, 'f', 2);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text;
}

void writeSpreadsheetWorkbookStart(QXmlStreamWriter &xml)
{
    xml.setAutoFormatting(true);
    xml.writeStartDocument(QStringLiteral("1.0"));
    xml.writeProcessingInstruction(QStringLiteral("mso-application"), QStringLiteral("progid=\"Excel.Sheet\""));
    xml.writeStartElement(QStringLiteral("Workbook"));
    xml.writeDefaultNamespace(kSpreadsheetNs);
    xml.writeNamespace(kOfficeNs, QStringLiteral("o"));
    xml.writeNamespace(kExcelNs, QStringLiteral("x"));
    xml.writeNamespace(kSpreadsheetNs, QStringLiteral("ss"));
    xml.writeNamespace(kHtmlNs, QStringLiteral("html"));
}

void writeStyleBorder(QXmlStreamWriter &xml, const QString &position, const QString &lineStyle, int weight)
{
    xml.writeEmptyElement(QStringLiteral("Border"));
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Position"), position);
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("LineStyle"), lineStyle);
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Weight"), QString::number(weight));
}

void writeSpreadsheetStyles(QXmlStreamWriter &xml)
{
    const auto writeStyleStart = [&xml](const QString &id) {
        xml.writeStartElement(QStringLiteral("Style"));
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("ID"), id);
    };
    const auto writeFont = [&xml](const QString &name, double size, bool bold, const QString &color = {}) {
        xml.writeEmptyElement(QStringLiteral("Font"));
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("FontName"), name);
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Size"), xmlNumber(size));
        if (bold) {
            xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Bold"), QStringLiteral("1"));
        }
        if (!color.isEmpty()) {
            xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Color"), color);
        }
    };
    const auto writeAlignment =
        [&xml](const QString &horizontal = {}, const QString &vertical = {}, bool wrap = false) {
            xml.writeEmptyElement(QStringLiteral("Alignment"));
            if (!horizontal.isEmpty()) {
                xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Horizontal"), horizontal);
            }
            if (!vertical.isEmpty()) {
                xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Vertical"), vertical);
            }
            if (wrap) {
                xml.writeAttribute(kSpreadsheetNs, QStringLiteral("WrapText"), QStringLiteral("1"));
            }
        };
    const auto writeInterior = [&xml](const QString &color) {
        xml.writeEmptyElement(QStringLiteral("Interior"));
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Color"), color);
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Pattern"), QStringLiteral("Solid"));
    };

    xml.writeStartElement(QStringLiteral("Styles"));

    writeStyleStart(QStringLiteral("Default"));
    xml.writeEndElement();

    writeStyleStart(QStringLiteral("free_title"));
    writeAlignment(QStringLiteral("Center"), QStringLiteral("Center"));
    writeFont(QStringLiteral("Microsoft YaHei UI"), 16.0, true, QStringLiteral("#FFFFFF"));
    writeInterior(QStringLiteral("#D97B3F"));
    xml.writeStartElement(QStringLiteral("Borders"));
    writeStyleBorder(xml, QStringLiteral("Left"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Right"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Top"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Bottom"), QStringLiteral("Continuous"), 2);
    xml.writeEndElement();
    xml.writeEndElement();

    writeStyleStart(QStringLiteral("free_subtitle"));
    writeAlignment(QStringLiteral("Center"), QStringLiteral("Center"));
    writeFont(QStringLiteral("Microsoft YaHei UI"), 10.5, false, QStringLiteral("#69432A"));
    writeInterior(QStringLiteral("#F7E4D0"));
    xml.writeStartElement(QStringLiteral("Borders"));
    writeStyleBorder(xml, QStringLiteral("Left"), QStringLiteral("Continuous"), 1);
    writeStyleBorder(xml, QStringLiteral("Right"), QStringLiteral("Continuous"), 1);
    writeStyleBorder(xml, QStringLiteral("Top"), QStringLiteral("Continuous"), 1);
    writeStyleBorder(xml, QStringLiteral("Bottom"), QStringLiteral("Continuous"), 1);
    xml.writeEndElement();
    xml.writeEndElement();

    writeStyleStart(QStringLiteral("free_header"));
    writeAlignment(QStringLiteral("Center"), QStringLiteral("Center"));
    writeFont(QStringLiteral("Microsoft YaHei UI"), 11.0, true, QStringLiteral("#FFFFFF"));
    writeInterior(QStringLiteral("#C96D31"));
    xml.writeStartElement(QStringLiteral("Borders"));
    writeStyleBorder(xml, QStringLiteral("Left"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Right"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Top"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Bottom"), QStringLiteral("Continuous"), 2);
    xml.writeEndElement();
    xml.writeEndElement();

    for (const QString &styleId : {QStringLiteral("free_data_white"), QStringLiteral("free_data_striped")}) {
        writeStyleStart(styleId);
        writeAlignment(QStringLiteral("Center"), QStringLiteral("Center"), true);
        writeFont(QStringLiteral("Microsoft YaHei UI"), 10.5, false, QStringLiteral("#20303F"));
        writeInterior(styleId.endsWith(QStringLiteral("striped")) ? QStringLiteral("#F9F3EC") : QStringLiteral("#FFFFFF"));
        xml.writeStartElement(QStringLiteral("Borders"));
        writeStyleBorder(xml, QStringLiteral("Left"), QStringLiteral("Continuous"), 1);
        writeStyleBorder(xml, QStringLiteral("Right"), QStringLiteral("Continuous"), 1);
        writeStyleBorder(xml, QStringLiteral("Top"), QStringLiteral("Continuous"), 1);
        writeStyleBorder(xml, QStringLiteral("Bottom"), QStringLiteral("Continuous"), 1);
        xml.writeEndElement();
        xml.writeEndElement();
    }

    writeStyleStart(QStringLiteral("notice_title"));
    writeAlignment(QStringLiteral("Center"));
    writeFont(QStringLiteral("SimSun"), 18.0, false);
    xml.writeEndElement();

    writeStyleStart(QStringLiteral("notice_text"));
    writeAlignment({}, {}, true);
    writeFont(QStringLiteral("SimSun"), 12.0, false);
    xml.writeEndElement();

    writeStyleStart(QStringLiteral("notice_sub_text"));
    writeAlignment({}, {}, true);
    writeFont(QStringLiteral("Arial"), 12.0, false);
    xml.writeEndElement();

    writeStyleStart(QStringLiteral("notice_header_first"));
    writeAlignment(QStringLiteral("Center"), QStringLiteral("Center"));
    writeFont(QStringLiteral("Arial"), 12.0, false);
    xml.writeStartElement(QStringLiteral("Borders"));
    writeStyleBorder(xml, QStringLiteral("Left"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Right"), QStringLiteral("Continuous"), 1);
    writeStyleBorder(xml, QStringLiteral("Top"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Bottom"), QStringLiteral("Continuous"), 1);
    xml.writeEndElement();
    xml.writeEndElement();

    writeStyleStart(QStringLiteral("notice_header_mid"));
    writeAlignment(QStringLiteral("Center"), QStringLiteral("Center"));
    writeFont(QStringLiteral("Arial"), 12.0, false);
    xml.writeStartElement(QStringLiteral("Borders"));
    writeStyleBorder(xml, QStringLiteral("Left"), QStringLiteral("Continuous"), 1);
    writeStyleBorder(xml, QStringLiteral("Right"), QStringLiteral("Continuous"), 1);
    writeStyleBorder(xml, QStringLiteral("Top"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Bottom"), QStringLiteral("Continuous"), 1);
    xml.writeEndElement();
    xml.writeEndElement();

    writeStyleStart(QStringLiteral("notice_header_last"));
    writeAlignment(QStringLiteral("Center"), QStringLiteral("Center"));
    writeFont(QStringLiteral("Arial"), 12.0, false);
    xml.writeStartElement(QStringLiteral("Borders"));
    writeStyleBorder(xml, QStringLiteral("Left"), QStringLiteral("Continuous"), 1);
    writeStyleBorder(xml, QStringLiteral("Right"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Top"), QStringLiteral("Continuous"), 2);
    writeStyleBorder(xml, QStringLiteral("Bottom"), QStringLiteral("Continuous"), 1);
    xml.writeEndElement();
    xml.writeEndElement();

    for (const auto &id : {QStringLiteral("notice_data_first_thin"),
                           QStringLiteral("notice_data_mid_thin"),
                           QStringLiteral("notice_data_last_thin"),
                           QStringLiteral("notice_data_first_thick"),
                           QStringLiteral("notice_data_mid_thick"),
                           QStringLiteral("notice_data_last_thick")}) {
        writeStyleStart(id);
        writeAlignment(QStringLiteral("Center"), QStringLiteral("Center"));
        writeFont(QStringLiteral("Arial"), 12.0, false);
        xml.writeStartElement(QStringLiteral("Borders"));
        const bool first = id.contains(QStringLiteral("first"));
        const bool last = id.contains(QStringLiteral("last"));
        const bool thickBottom = id.endsWith(QStringLiteral("thick"));
        writeStyleBorder(xml, QStringLiteral("Left"), QStringLiteral("Continuous"), first ? 2 : 1);
        writeStyleBorder(xml, QStringLiteral("Right"), QStringLiteral("Continuous"), last ? 2 : 1);
        writeStyleBorder(xml, QStringLiteral("Top"), QStringLiteral("Continuous"), 1);
        writeStyleBorder(xml, QStringLiteral("Bottom"), QStringLiteral("Continuous"), thickBottom ? 2 : 1);
        xml.writeEndElement();
        xml.writeEndElement();
    }

    writeStyleStart(QStringLiteral("notice_time_text"));
    writeFont(QStringLiteral("SimSun"), 12.0, false);
    xml.writeEndElement();

    writeStyleStart(QStringLiteral("notice_time_right"));
    writeAlignment(QStringLiteral("Center"));
    writeFont(QStringLiteral("Arial"), 12.0, false);
    xml.writeEndElement();

    writeStyleStart(QStringLiteral("notice_arrange"));
    writeAlignment(QStringLiteral("Center"));
    writeFont(QStringLiteral("SimSun"), 12.0, false);
    xml.writeEndElement();

    xml.writeEndElement();
}

void writeXmlCell(
    QXmlStreamWriter &xml,
    const QString &value,
    const QString &styleId = {},
    int mergeAcross = 0,
    const QString &type = QStringLiteral("String"))
{
    xml.writeStartElement(QStringLiteral("Cell"));
    if (!styleId.isEmpty()) {
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("StyleID"), styleId);
    }
    if (mergeAcross > 0) {
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("MergeAcross"), QString::number(mergeAcross));
    }
    if (!value.isNull()) {
        xml.writeStartElement(QStringLiteral("Data"));
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Type"), type);
        xml.writeCharacters(value);
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

bool exportFreeTeachersXlsXml(
    const QString &outputPath,
    const QVector<QString> &teachers,
    const SubjectMap &subjectMap,
    const QString &slotText,
    QString *error)
{
    QSaveFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法写入 xls 文件: %1").arg(file.errorString());
        }
        return false;
    }

    QXmlStreamWriter xml(&file);
    writeSpreadsheetWorkbookStart(xml);
    writeSpreadsheetStyles(xml);

    xml.writeStartElement(QStringLiteral("Worksheet"));
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Name"), QStringLiteral("无课名单"));
    xml.writeStartElement(QStringLiteral("Table"));
    for (const double width : {60.0, 120.0, 100.0, 180.0}) {
        xml.writeEmptyElement(QStringLiteral("Column"));
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Width"), xmlNumber(width));
    }

    xml.writeStartElement(QStringLiteral("Row"));
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Height"), xmlNumber(26.0));
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("AutoFitHeight"), QStringLiteral("0"));
    writeXmlCell(xml, QStringLiteral("无课老师名单"), QStringLiteral("free_title"), 3);
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Row"));
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Height"), xmlNumber(20.0));
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("AutoFitHeight"), QStringLiteral("0"));
    writeXmlCell(xml, QStringLiteral("无课节次：%1").arg(slotText), QStringLiteral("free_subtitle"), 3);
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Row"));
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Row"));
    for (const QString &header : {QStringLiteral("ID"), QStringLiteral("姓名"), QStringLiteral("科目"), QStringLiteral("无课节次")}) {
        writeXmlCell(xml, header, QStringLiteral("free_header"));
    }
    xml.writeEndElement();

    for (int index = 0; index < teachers.size(); ++index) {
        xml.writeStartElement(QStringLiteral("Row"));
        const QString styleId = (index % 2 == 0) ? QStringLiteral("free_data_white") : QStringLiteral("free_data_striped");
        writeXmlCell(xml, QString::number(index + 1), styleId, 0, QStringLiteral("Number"));
        writeXmlCell(xml, teachers.at(index), styleId);
        writeXmlCell(xml, subjectMap.value(teachers.at(index)), styleId);
        writeXmlCell(xml, slotText, styleId);
        xml.writeEndElement();
    }

    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();

    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("保存 xls 文件失败: %1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

void writeNoticeBlockXlsXml(
    QXmlStreamWriter &xml,
    NoticeItem::Kind kind,
    const QString &absentTeacher,
    const QString &substituteTeacher,
    QVector<Assignment> rows,
    const QString &reason,
    const QString &startDate,
    const QString &endDate)
{
    rows = sortedAssignments(std::move(rows));
    const int totalRows = qMax(1, rows.size());
    const QString titleTeacher = kind == NoticeItem::Kind::Substitute ? substituteTeacher : absentTeacher;
    const QString textStyle = kind == NoticeItem::Kind::Substitute
        ? QStringLiteral("notice_sub_text")
        : QStringLiteral("notice_text");

    xml.writeStartElement(QStringLiteral("Row"));
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Height"), xmlNumber(23.25));
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("AutoFitHeight"), QStringLiteral("0"));
    writeXmlCell(xml, QStringLiteral("%1 老师调代课通知").arg(titleTeacher), QStringLiteral("notice_title"), 3);
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Row"));
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Row"));
    writeXmlCell(xml, buildNoticeReasonText(absentTeacher, reason), textStyle, 3);
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Row"));
    writeXmlCell(xml, kNoticeHeaderLabels.at(0), QStringLiteral("notice_header_first"));
    writeXmlCell(xml, kNoticeHeaderLabels.at(1), QStringLiteral("notice_header_mid"));
    writeXmlCell(xml, kNoticeHeaderLabels.at(2), QStringLiteral("notice_header_mid"));
    writeXmlCell(xml, kNoticeHeaderLabels.at(3), QStringLiteral("notice_header_last"));
    xml.writeEndElement();

    for (int index = 0; index < totalRows; ++index) {
        xml.writeStartElement(QStringLiteral("Row"));
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Height"), xmlNumber(15.75));
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("AutoFitHeight"), QStringLiteral("0"));
        const bool lastRow = index == totalRows - 1;
        const QString firstStyle = lastRow ? QStringLiteral("notice_data_first_thick")
                                           : QStringLiteral("notice_data_first_thin");
        const QString midStyle = lastRow ? QStringLiteral("notice_data_mid_thick")
                                         : QStringLiteral("notice_data_mid_thin");
        const QString lastStyle = lastRow ? QStringLiteral("notice_data_last_thick")
                                          : QStringLiteral("notice_data_last_thin");
        if (index < rows.size()) {
            const Assignment &assignment = rows.at(index);
            const QString timeText = QStringLiteral("%1第%2节").arg(assignment.day).arg(assignment.period);
            writeXmlCell(xml, assignment.className, firstStyle);
            writeXmlCell(xml, assignment.subject, midStyle);
            if (kind == NoticeItem::Kind::Substitute) {
                writeXmlCell(xml, QString(), midStyle);
                writeXmlCell(xml, timeText, lastStyle);
            } else {
                writeXmlCell(xml, timeText, midStyle);
                writeXmlCell(xml, QString(), lastStyle);
            }
        } else {
            writeXmlCell(xml, QString(), firstStyle);
            writeXmlCell(xml, QString(), midStyle);
            writeXmlCell(xml, QString(), midStyle);
            writeXmlCell(xml, QString(), lastStyle);
        }
        xml.writeEndElement();
    }

    xml.writeStartElement(QStringLiteral("Row"));
    writeXmlCell(xml, QString());
    writeXmlCell(xml, QStringLiteral("代课时间:%1～%2").arg(startDate, endDate), QStringLiteral("notice_time_text"));
    writeXmlCell(xml, QString());
    writeXmlCell(xml, QStringLiteral("高一年级"), QStringLiteral("notice_time_right"));
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Row"));
    writeXmlCell(xml, QString());
    writeXmlCell(xml, QString());
    writeXmlCell(xml, QString());
    writeXmlCell(xml, QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")), QStringLiteral("notice_arrange"));
    xml.writeEndElement();

    if (kind == NoticeItem::Kind::Absent) {
        xml.writeStartElement(QStringLiteral("Row"));
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Height"), xmlNumber(30.0));
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("AutoFitHeight"), QStringLiteral("0"));
        xml.writeEndElement();
    }
}

bool exportNoticesXlsXml(
    const QString &outputPath,
    const QVector<NoticeItem> &items,
    const QString &reason,
    const QString &startDate,
    const QString &endDate,
    QString *error)
{
    QSaveFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法写入 xls 文件: %1").arg(file.errorString());
        }
        return false;
    }

    QXmlStreamWriter xml(&file);
    writeSpreadsheetWorkbookStart(xml);
    writeSpreadsheetStyles(xml);

    xml.writeStartElement(QStringLiteral("Worksheet"));
    xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Name"), QStringLiteral("调代课通知"));
    xml.writeStartElement(QStringLiteral("Table"));
    for (const double width : {118.0, 238.0, 118.0, 98.0}) {
        xml.writeEmptyElement(QStringLiteral("Column"));
        xml.writeAttribute(kSpreadsheetNs, QStringLiteral("Width"), xmlNumber(width));
    }

    for (int index = 0; index < items.size(); ++index) {
        if (index > 0) {
            xml.writeStartElement(QStringLiteral("Row"));
            xml.writeEndElement();
        }
        const NoticeItem &item = items.at(index);
        writeNoticeBlockXlsXml(
            xml,
            item.kind,
            item.absentTeacher,
            item.substituteTeacher,
            item.rows,
            reason,
            startDate,
            endDate);
    }

    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();

    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("保存 xls 文件失败: %1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

[[maybe_unused]] void normalizeExportSheet(xlnt::worksheet &sheet, const TemplateSnapshot &snapshot)
{
    const QVector<NoticeBlock> blocks = findNoticeBlocks(sheet);
    if (blocks.isEmpty()) {
        return;
    }

    try {
        const auto mergedRanges = sheet.merged_ranges();
        for (const auto &mergeRange : mergedRanges) {
            sheet.unmerge_cells(mergeRange);
        }
    } catch (const std::exception &ex) {
        throw std::runtime_error(std::string("拆分合并单元格失败: ") + ex.what());
    }

    const NoticeTemplateRows absentRows = absentTemplateRows();
    const NoticeTemplateRows subRows = subTemplateRows();

    try {
        for (const NoticeBlock &block : blocks) {
            const NoticeTemplateRows &rows = block.kind == NoticeItem::Kind::Substitute ? subRows : absentRows;
            applySnapshotRowStyle(sheet, snapshot, block.titleRow, rows.title);
            applySnapshotRowStyle(sheet, snapshot, block.blankRow, rows.blank);
            applySnapshotRowStyle(sheet, snapshot, block.textRow, rows.text);
            applySnapshotRowStyle(sheet, snapshot, block.headerRow, rows.header);
            for (xlnt::row_t dataRow : block.dataRows) {
                applySnapshotRowStyle(sheet, snapshot, dataRow, rows.data);
            }
            if (!block.dataRows.isEmpty()) {
                normalizeDataBlockBorders(sheet, block.dataRows.first(), block.dataRows.size());
            }
            applySnapshotRowStyle(sheet, snapshot, block.timeRow, rows.time);
            if (block.arrangeRow <= sheet.highest_row()) {
                applySnapshotRowStyle(sheet, snapshot, block.arrangeRow, rows.arrange);
            }

            for (int columnIndex = 1; columnIndex <= kNoticeHeaderLabels.size(); ++columnIndex) {
                setCellValue(sheet, block.headerRow, columnIndex, kNoticeHeaderLabels.at(columnIndex - 1));
            }
        }
    } catch (const std::exception &ex) {
        throw std::runtime_error(std::string("应用通知单样式失败: ") + ex.what());
    }

    try {
        for (const NoticeBlock &block : blocks) {
            sheet.merge_cells(
                xlnt::range_reference(column(1), block.titleRow, column(kNoticeTemplateColumns), block.titleRow));
            sheet.merge_cells(
                xlnt::range_reference(column(1), block.textRow, column(kNoticeTemplateColumns), block.textRow));
        }
    } catch (const std::exception &ex) {
        throw std::runtime_error(std::string("重建合并单元格失败: ") + ex.what());
    }
}

[[maybe_unused]] void fillAbsentNotice(
    xlnt::worksheet &sheet,
    const QString &absentTeacher,
    QVector<Assignment> rows,
    const QString &reason,
    const QString &startDate,
    const QString &endDate,
    xlnt::row_t startRow = 1)
{
    rows = sortedAssignments(std::move(rows));

    const int rowOffset = static_cast<int>(startRow) - 1;
    int topTitle = rowOffset + 1;
    int topText = rowOffset + 3;
    int topDataStart = rowOffset + 5;
    int topTime = rowOffset + 7;
    int topArrange = rowOffset + 8;
    int bottomTitle = rowOffset + 10;
    int bottomText = rowOffset + 12;
    int bottomDataStart = rowOffset + 14;
    int bottomTime = rowOffset + 16;
    int bottomArrange = rowOffset + 17;

    const int dataCount = static_cast<int>(rows.size());
    const int extraRows = qMax(0, dataCount - kHeaderDataRows);
    if (extraRows > 0) {
        sheet.insert_rows(row(topTime), static_cast<std::uint32_t>(extraRows));
        topTime += extraRows;
        topArrange += extraRows;
        bottomTitle += extraRows;
        bottomText += extraRows;
        bottomDataStart += extraRows;
        bottomTime += extraRows;
        bottomArrange += extraRows;
    } else if (dataCount <= 1) {
        sheet.delete_rows(row(topDataStart + 1), 1);
        topTime -= 1;
        topArrange -= 1;
        bottomTitle -= 1;
        bottomText -= 1;
        bottomDataStart -= 1;
        bottomTime -= 1;
        bottomArrange -= 1;
    }

    setCellValue(sheet, row(topTitle), 1, QStringLiteral("%1 老师调代课通知").arg(absentTeacher));
    setCellValue(sheet, row(topText), 1, buildNoticeReasonText(absentTeacher, reason));

    const int totalRows = qMax(1, dataCount);
    for (int index = 0; index < totalRows; ++index) {
        const xlnt::row_t targetRow = row(topDataStart + index);
        if (index >= dataCount) {
            clearRowValues(sheet, targetRow);
            continue;
        }

        const Assignment &assignment = rows.at(index);
        const QString timeText = QStringLiteral("%1第%2节").arg(assignment.day).arg(assignment.period);
        setCellValue(sheet, targetRow, 1, assignment.className);
        setCellValue(sheet, targetRow, 2, assignment.subject);
        setCellValue(sheet, targetRow, 3, timeText);
        clearCellValue(sheet, targetRow, 4);
    }

    setCellValue(sheet, row(topTime), 2, QStringLiteral("代课时间:%1～%2").arg(startDate, endDate));
    setCellValue(sheet, row(topArrange), 4, QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));

    const int deleteCount = bottomArrange - bottomTitle + 1;
    if (deleteCount > 0) {
        sheet.delete_rows(row(bottomTitle), static_cast<std::uint32_t>(deleteCount));
    }
}

[[maybe_unused]] void fillSubNotice(
    xlnt::worksheet &sheet,
    const QString &absentTeacher,
    const QString &substituteTeacher,
    QVector<Assignment> rows,
    const QString &reason,
    const QString &startDate,
    const QString &endDate,
    xlnt::row_t startRow = 1)
{
    rows = sortedAssignments(std::move(rows));

    const int rowOffset = static_cast<int>(startRow) - 1;
    const int topTitle = rowOffset + 1;
    int bottomTitle = rowOffset + 10;
    int bottomText = rowOffset + 12;
    int bottomDataStart = rowOffset + 14;
    int bottomTime = rowOffset + 16;
    int bottomArrange = rowOffset + 17;

    const int dataCount = static_cast<int>(rows.size());
    const int extraRows = qMax(0, dataCount - kHeaderDataRows);
    if (extraRows > 0) {
        sheet.insert_rows(row(bottomTime), static_cast<std::uint32_t>(extraRows));
        bottomTime += extraRows;
        bottomArrange += extraRows;
    } else if (dataCount <= 1) {
        sheet.delete_rows(row(bottomDataStart + 1), 1);
        bottomTime -= 1;
        bottomArrange -= 1;
    }

    setCellValue(sheet, row(bottomTitle), 1, QStringLiteral("%1 老师调代课通知").arg(substituteTeacher));
    setCellValue(sheet, row(bottomText), 1, buildNoticeReasonText(absentTeacher, reason));

    const int totalRows = qMax(1, dataCount);
    for (int index = 0; index < totalRows; ++index) {
        const xlnt::row_t targetRow = row(bottomDataStart + index);
        if (index >= dataCount) {
            clearRowValues(sheet, targetRow);
            continue;
        }

        const Assignment &assignment = rows.at(index);
        const QString timeText = QStringLiteral("%1第%2节").arg(assignment.day).arg(assignment.period);
        setCellValue(sheet, targetRow, 1, assignment.className);
        setCellValue(sheet, targetRow, 2, assignment.subject);
        clearCellValue(sheet, targetRow, 3);
        setCellValue(sheet, targetRow, 4, timeText);
    }

    setCellValue(sheet, row(bottomTime), 2, QStringLiteral("代课时间:%1～%2").arg(startDate, endDate));
    setCellValue(sheet, row(bottomArrange), 4, QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));

    const int deleteCount = bottomTitle - topTitle;
    if (deleteCount > 0) {
        sheet.delete_rows(row(topTitle), static_cast<std::uint32_t>(deleteCount));
    }
}

xlnt::color makeColor(const char *hex)
{
    return xlnt::color(xlnt::rgb_color(hex));
}

xlnt::alignment centeredAlignment(bool wrap = false)
{
    xlnt::alignment alignment;
    alignment.horizontal(xlnt::horizontal_alignment::center);
    alignment.vertical(xlnt::vertical_alignment::center);
    alignment.wrap(wrap);
    return alignment;
}

xlnt::font createFont(const char *name, double size, bool bold, const char *hex)
{
    xlnt::font font;
    font.name(name);
    font.size(size);
    font.bold(bold);
    font.color(makeColor(hex));
    return font;
}

xlnt::border createBorder(xlnt::border_style style, const char *hex)
{
    xlnt::border border;
    xlnt::border::border_property property;
    property.style(style);
    property.color(makeColor(hex));
    border.side(xlnt::border_side::start, property);
    border.side(xlnt::border_side::end, property);
    border.side(xlnt::border_side::top, property);
    border.side(xlnt::border_side::bottom, property);
    return border;
}

void applyCellStyle(
    xlnt::cell cell,
    const xlnt::font &font,
    const xlnt::fill &fill,
    const xlnt::alignment &alignment,
    const xlnt::border &border)
{
    cell.font(font);
    cell.fill(fill);
    cell.alignment(alignment);
    cell.border(border);
}

xlnt::alignment wrapAlignment()
{
    xlnt::alignment alignment;
    alignment.wrap(true);
    return alignment;
}

xlnt::alignment horizontalCenteredAlignment()
{
    xlnt::alignment alignment;
    alignment.horizontal(xlnt::horizontal_alignment::center);
    return alignment;
}

xlnt::border tableBorder(
    xlnt::border_style leftStyle,
    xlnt::border_style rightStyle,
    xlnt::border_style topStyle,
    xlnt::border_style bottomStyle)
{
    xlnt::border border;
    auto applySide = [&border](xlnt::border_side side, xlnt::border_style style) {
        if (style == xlnt::border_style::none) {
            return;
        }
        xlnt::border::border_property property;
        property.style(style);
        border.side(side, property);
    };
    applySide(xlnt::border_side::start, leftStyle);
    applySide(xlnt::border_side::end, rightStyle);
    applySide(xlnt::border_side::top, topStyle);
    applySide(xlnt::border_side::bottom, bottomStyle);
    return border;
}

void applyTemplateRowStyle(xlnt::worksheet &sheet, xlnt::row_t targetRow, int sourceRow, int maxColumn)
{
    const xlnt::font titleFont = createFont("SimSun", 18.0, false, "FF000000");
    const xlnt::font textFont = createFont("SimSun", 12.0, false, "FF000000");
    const xlnt::font subTextFont = createFont("Arial", 12.0, false, "FF000000");
    const xlnt::font tableFont = createFont("Arial", 12.0, false, "FF000000");

    switch (sourceRow) {
    case 1:
    case 10:
        for (int columnIndex = 1; columnIndex <= maxColumn; ++columnIndex) {
            auto cell = sheet.cell(column(columnIndex), targetRow);
            cell.font(titleFont);
            cell.alignment(horizontalCenteredAlignment());
        }
        break;
    case 3:
    case 12:
        for (int columnIndex = 1; columnIndex <= maxColumn; ++columnIndex) {
            auto cell = sheet.cell(column(columnIndex), targetRow);
            cell.font(sourceRow == 3 ? textFont : subTextFont);
            cell.alignment(wrapAlignment());
        }
        break;
    case 4:
    case 13:
        if (maxColumn >= 1) {
            auto cell = sheet.cell(column(1), targetRow);
            cell.font(tableFont);
            cell.alignment(centeredAlignment());
            cell.border(tableBorder(
                xlnt::border_style::thick,
                xlnt::border_style::thin,
                xlnt::border_style::thick,
                xlnt::border_style::thin));
        }
        for (int columnIndex = 2; columnIndex < maxColumn; ++columnIndex) {
            auto cell = sheet.cell(column(columnIndex), targetRow);
            cell.font(tableFont);
            cell.alignment(centeredAlignment());
            cell.border(tableBorder(
                xlnt::border_style::thin,
                xlnt::border_style::thin,
                xlnt::border_style::thick,
                xlnt::border_style::thin));
        }
        if (maxColumn >= 4) {
            auto cell = sheet.cell(column(4), targetRow);
            cell.font(tableFont);
            cell.alignment(centeredAlignment());
            cell.border(tableBorder(
                xlnt::border_style::thin,
                xlnt::border_style::thick,
                xlnt::border_style::thick,
                xlnt::border_style::thin));
        }
        break;
    case 5:
    case 14:
        if (maxColumn >= 1) {
            auto cell = sheet.cell(column(1), targetRow);
            cell.font(tableFont);
            cell.alignment(centeredAlignment());
            cell.border(tableBorder(
                xlnt::border_style::thick,
                xlnt::border_style::thin,
                xlnt::border_style::thin,
                xlnt::border_style::thick));
        }
        for (int columnIndex = 2; columnIndex < maxColumn; ++columnIndex) {
            auto cell = sheet.cell(column(columnIndex), targetRow);
            cell.font(tableFont);
            cell.alignment(centeredAlignment());
            cell.border(tableBorder(
                xlnt::border_style::thin,
                xlnt::border_style::thin,
                xlnt::border_style::thin,
                xlnt::border_style::thick));
        }
        if (maxColumn >= 4) {
            auto cell = sheet.cell(column(4), targetRow);
            cell.font(tableFont);
            cell.alignment(centeredAlignment());
            cell.border(tableBorder(
                xlnt::border_style::thin,
                xlnt::border_style::thick,
                xlnt::border_style::thin,
                xlnt::border_style::thick));
        }
        break;
    case 7:
    case 16:
        if (maxColumn >= 2) {
            auto cell = sheet.cell(column(2), targetRow);
            cell.font(textFont);
        }
        if (maxColumn >= 4) {
            auto cell = sheet.cell(column(4), targetRow);
            cell.font(tableFont);
            cell.alignment(horizontalCenteredAlignment());
        }
        break;
    case 8:
    case 17:
        if (maxColumn >= 4) {
            auto cell = sheet.cell(column(4), targetRow);
            cell.font(textFont);
            cell.alignment(horizontalCenteredAlignment());
        }
        break;
    default:
        break;
    }
}

}  // namespace

bool ExcelHelper::validateWorkbook(const QString &path, QString *error)
{
    if (error) {
        error->clear();
    }
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        if (error) {
            *error = QStringLiteral("文件不存在。");
        }
        return false;
    }
    if (fileInfo.size() <= 0) {
        if (error) {
            *error = QStringLiteral("文件内容为空。");
        }
        return false;
    }
    const QString suffix = fileInfo.suffix().toLower();
    if (suffix != QStringLiteral("xlsx") && suffix != QStringLiteral("xls")) {
        if (error) {
            *error = QStringLiteral("通知单模板仅支持 xlsx 或 xls 文件。");
        }
        return false;
    }
    if (isXlsPath(path)) {
        return validateNoticeTemplateXls(path, error);
    }

    try {
        xlnt::workbook workbook;
        loadWorkbook(workbook, path);
        if (workbook.sheet_count() == 0) {
            if (error) {
                *error = QStringLiteral("模板中没有工作表。");
            }
            return false;
        }

        QString lastValidationError;
        for (std::size_t sheetIndex = 0; sheetIndex < workbook.sheet_count(); ++sheetIndex) {
            xlnt::worksheet sheet = workbook.sheet_by_index(sheetIndex);
            QString sheetError;
            const bool valid = validateNoticeTemplateCells(
                static_cast<int>(sheet.highest_row()),
                static_cast<int>(sheet.highest_column().index),
                [&](int rowIndex, int columnIndex) {
                    return cellText(sheet, static_cast<xlnt::row_t>(rowIndex), columnIndex);
                },
                &sheetError);
            if (valid) {
                return true;
            }
            lastValidationError = sheetError;
        }
        if (error) {
            *error = lastValidationError.isEmpty()
                ? QStringLiteral("模板中没有符合要求的通知单工作表。")
                : lastValidationError;
        }
        return false;
    } catch (const std::exception &ex) {
        if (error) {
            *error = QStringLiteral("无法读取 xlsx 文件: %1").arg(QString::fromLocal8Bit(ex.what()));
        }
        return false;
    }
}

QVector<ScheduleEntry> ExcelHelper::readSchedule(
    const QString &path,
    QString *error,
    QString *warning)
{
    if (error) {
        error->clear();
    }
    if (warning) {
        warning->clear();
    }
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        if (error) {
            *error = QStringLiteral("课表文件不存在。");
        }
        return {};
    }
    if (fileInfo.size() <= 0) {
        if (error) {
            *error = QStringLiteral("课表文件内容为空。");
        }
        return {};
    }
    const QString suffix = fileInfo.suffix().toLower();
    if (suffix != QStringLiteral("xlsx") && suffix != QStringLiteral("xls")) {
        if (error) {
            *error = QStringLiteral("课表导入仅支持 xlsx 或 xls 文件。");
        }
        return {};
    }
    if (isXlsPath(path)) {
        return readScheduleXls(path, error, warning);
    }

    QVector<ScheduleEntry> rows;
    try {
        xlnt::workbook workbook;
        loadWorkbook(workbook, path);
        if (workbook.sheet_count() == 0) {
            if (error) {
                *error = QStringLiteral("课表文件中没有工作表。");
            }
            return {};
        }

        int selectedSheet = -1;
        int fallbackSheet = -1;
        ScheduleColumnMapping selectedMapping;
        ScheduleColumnMapping bestPartial;
        for (std::size_t sheetIndex = 0; sheetIndex < workbook.sheet_count(); ++sheetIndex) {
            xlnt::worksheet sheet = workbook.sheet_by_index(sheetIndex);
            const int rowCount = static_cast<int>(sheet.highest_row());
            const int columnCount = static_cast<int>(sheet.highest_column().index);
            if (fallbackSheet < 0 && rowCount >= 2 && columnCount >= 5) {
                fallbackSheet = static_cast<int>(sheetIndex);
            }
            const ScheduleColumnMapping mapping = detectScheduleColumns(
                rowCount,
                columnCount,
                [&](int rowIndex, int columnIndex) {
                    return cellText(sheet, static_cast<xlnt::row_t>(rowIndex), columnIndex);
                });
            if (mapping.complete()) {
                selectedSheet = static_cast<int>(sheetIndex);
                selectedMapping = mapping;
                break;
            }
            if (mapping.recognizedCount > bestPartial.recognizedCount) {
                bestPartial = mapping;
            }
        }

        if (selectedSheet < 0 && bestPartial.recognizedCount > 0) {
            if (error) {
                *error = QStringLiteral("课表表头不完整，缺少：%1。").arg(missingScheduleHeaders(bestPartial));
            }
            return {};
        }
        if (selectedSheet < 0 && fallbackSheet >= 0) {
            selectedSheet = fallbackSheet;
            selectedMapping.headerRow = 1;
            selectedMapping.columns = {1, 2, 3, 4, 5};
            selectedMapping.recognizedCount = 5;
        }
        if (selectedSheet < 0) {
            if (error) {
                *error = QStringLiteral("课表文件中没有可识别的数据工作表。");
            }
            return {};
        }

        xlnt::worksheet sheet = workbook.sheet_by_index(static_cast<std::size_t>(selectedSheet));
        rows = parseScheduleRows(
            static_cast<int>(sheet.highest_row()),
            selectedMapping,
            [&](int rowIndex, int columnIndex) {
                return cellText(sheet, static_cast<xlnt::row_t>(rowIndex), columnIndex);
            },
            fromStdString(sheet.title()),
            error,
            warning);
    } catch (const std::exception &ex) {
        if (error) {
            *error = QStringLiteral("读取课表失败: %1").arg(QString::fromLocal8Bit(ex.what()));
        }
        return {};
    }

    return rows;
}

bool ExcelHelper::exportFreeTeachers(
    const QString &outputPath,
    const QVector<QString> &teachers,
    const SubjectMap &subjectMap,
    const QString &slotText,
    QString *error)
{
    if (teachers.isEmpty()) {
        if (error) {
            *error = QStringLiteral("没有可导出的无课老师。");
        }
        return false;
    }
    if (isXlsPath(outputPath)) {
        return exportFreeTeachersXlsXml(outputPath, teachers, subjectMap, slotText, error);
    }

    try {
        xlnt::workbook workbook;
        xlnt::worksheet sheet = workbook.active_sheet();
        sheet.title(toStdString(QStringLiteral("无课名单")));

        sheet.merge_cells("A1:D1");
        sheet.merge_cells("A2:D2");
        setCellValue(sheet, 1, 1, QStringLiteral("无课老师名单"));
        setCellValue(sheet, 2, 1, QStringLiteral("无课节次：%1").arg(slotText));
        setCellValue(sheet, 4, 1, QStringLiteral("ID"));
        setCellValue(sheet, 4, 2, QStringLiteral("姓名"));
        setCellValue(sheet, 4, 3, QStringLiteral("科目"));
        setCellValue(sheet, 4, 4, QStringLiteral("无课节次"));

        const xlnt::font titleFont = createFont("Microsoft YaHei UI", 16, true, "FFFFFFFF");
        const xlnt::font subtitleFont = createFont("Microsoft YaHei UI", 10.5, false, "FF69432A");
        const xlnt::font headerFont = createFont("Microsoft YaHei UI", 11, true, "FFFFFFFF");
        const xlnt::font dataFont = createFont("Microsoft YaHei UI", 10.5, false, "FF20303F");

        const xlnt::fill titleFill = xlnt::fill::solid(makeColor("FFD97B3F"));
        const xlnt::fill subtitleFill = xlnt::fill::solid(makeColor("FFF7E4D0"));
        const xlnt::fill headerFill = xlnt::fill::solid(makeColor("FFC96D31"));
        const xlnt::fill whiteFill = xlnt::fill::solid(makeColor("FFFFFFFF"));
        const xlnt::fill stripedFill = xlnt::fill::solid(makeColor("FFF9F3EC"));

        const xlnt::alignment centered = centeredAlignment();
        const xlnt::alignment wrappedCentered = centeredAlignment(true);
        const xlnt::border headerBorder = createBorder(xlnt::border_style::medium, "FFB45F29");
        const xlnt::border dataBorder = createBorder(xlnt::border_style::thin, "FFD8C6AE");

        applyCellStyle(sheet.cell("A1"), titleFont, titleFill, centered, headerBorder);
        applyCellStyle(sheet.cell("A2"), subtitleFont, subtitleFill, centered, dataBorder);

        for (int columnIndex = 1; columnIndex <= 4; ++columnIndex) {
            applyCellStyle(sheet.cell(column(columnIndex), 4), headerFont, headerFill, centered, headerBorder);
        }

        for (int index = 0; index < teachers.size(); ++index) {
            const int rowIndex = 5 + index;
            const QString &teacher = teachers.at(index);
            setCellValue(sheet, static_cast<xlnt::row_t>(rowIndex), 1, index + 1);
            setCellValue(sheet, static_cast<xlnt::row_t>(rowIndex), 2, teacher);
            setCellValue(sheet, static_cast<xlnt::row_t>(rowIndex), 3, subjectMap.value(teacher));
            setCellValue(sheet, static_cast<xlnt::row_t>(rowIndex), 4, slotText);

            const xlnt::fill rowFill = (index % 2 == 0) ? whiteFill : stripedFill;
            for (int columnIndex = 1; columnIndex <= 4; ++columnIndex) {
                applyCellStyle(
                    sheet.cell(column(columnIndex), static_cast<xlnt::row_t>(rowIndex)),
                    dataFont,
                    rowFill,
                    wrappedCentered,
                    dataBorder);
            }
        }

        sheet.column_properties(column(1)).width = 8;
        sheet.column_properties(column(1)).custom_width = true;
        sheet.column_properties(column(2)).width = 16;
        sheet.column_properties(column(2)).custom_width = true;
        sheet.column_properties(column(3)).width = 14;
        sheet.column_properties(column(3)).custom_width = true;
        sheet.column_properties(column(4)).width = 26;
        sheet.column_properties(column(4)).custom_width = true;

        sheet.row_properties(1).height = 26;
        sheet.row_properties(1).custom_height = true;
        sheet.row_properties(2).height = 20;
        sheet.row_properties(2).custom_height = true;

        saveWorkbook(workbook, outputPath);
        return true;
    } catch (const std::exception &ex) {
        if (error) {
            *error = QStringLiteral("导出无课名单失败: %1").arg(QString::fromLocal8Bit(ex.what()));
        }
        return false;
    }
}

bool ExcelHelper::exportTaskData(
    const QString &outputPath,
    const QVector<TaskDetail> &tasks,
    QString *error)
{
    if (tasks.isEmpty()) {
        if (error) {
            *error = QStringLiteral("没有可导出的代课任务。");
        }
        return false;
    }
    if (isXlsPath(outputPath)) {
        if (error) {
            *error = QStringLiteral("代课数据汇总导出仅支持 xlsx 格式。");
        }
        return false;
    }

    try {
        xlnt::workbook workbook;
        xlnt::worksheet taskSheet = workbook.active_sheet();
        taskSheet.title(toStdString(QStringLiteral("任务列表")));
        xlnt::worksheet detailSheet = workbook.create_sheet();
        detailSheet.title(toStdString(QStringLiteral("代课明细")));

        const xlnt::font headerFont = createFont("Microsoft YaHei UI", 10.5, true, "FFFFFFFF");
        const xlnt::font dataFont = createFont("Microsoft YaHei UI", 10.0, false, "FF263238");
        const xlnt::fill headerFill = xlnt::fill::solid(makeColor("FFD97745"));
        const xlnt::fill whiteFill = xlnt::fill::solid(makeColor("FFFFFFFF"));
        const xlnt::fill alternateFill = xlnt::fill::solid(makeColor("FFF8F9FA"));
        const xlnt::alignment centered = centeredAlignment(true);
        const xlnt::border headerBorder = createBorder(xlnt::border_style::thin, "FFB85F2E");
        const xlnt::border dataBorder = createBorder(xlnt::border_style::thin, "FFDDE2E6");

        const QStringList taskHeaders = {
            QStringLiteral("任务ID"),
            QStringLiteral("任务名称"),
            QStringLiteral("代课原因"),
            QStringLiteral("基于任务"),
            QStringLiteral("起始日期"),
            QStringLiteral("结束日期"),
            QStringLiteral("更新时间"),
            QStringLiteral("代课安排数"),
        };
        const QStringList detailHeaders = {
            QStringLiteral("任务ID"),
            QStringLiteral("任务名称"),
            QStringLiteral("被代课老师"),
            QStringLiteral("周几"),
            QStringLiteral("节次"),
            QStringLiteral("班级"),
            QStringLiteral("科目"),
            QStringLiteral("代课老师"),
            QStringLiteral("代课老师科目"),
        };

        auto writeHeader = [&](xlnt::worksheet &sheet, const QStringList &headers) {
            for (int columnIndex = 0; columnIndex < headers.size(); ++columnIndex) {
                setCellValue(sheet, 1, columnIndex + 1, headers.at(columnIndex));
                applyCellStyle(
                    sheet.cell(column(columnIndex + 1), 1),
                    headerFont,
                    headerFill,
                    centered,
                    headerBorder);
            }
            sheet.row_properties(1).height = 24;
            sheet.row_properties(1).custom_height = true;
        };
        writeHeader(taskSheet, taskHeaders);
        writeHeader(detailSheet, detailHeaders);

        QHash<qint64, QString> taskNames;
        for (const TaskDetail &task : tasks) {
            taskNames.insert(task.summary.id, task.summary.name);
        }

        int taskRow = 2;
        for (const TaskDetail &task : tasks) {
            const QString baseTaskName = task.summary.baseTaskId > 0
                ? taskNames.value(task.summary.baseTaskId, QStringLiteral("任务 %1").arg(task.summary.baseTaskId))
                : QStringLiteral("—");
            const QStringList values = {
                QString::number(task.summary.id),
                task.summary.name,
                task.summary.reason,
                baseTaskName,
                task.summary.startDate,
                task.summary.endDate,
                task.summary.updatedAt,
                QString::number(task.assignments.size()),
            };
            const xlnt::fill rowFill = taskRow % 2 == 0 ? whiteFill : alternateFill;
            for (int columnIndex = 0; columnIndex < values.size(); ++columnIndex) {
                setCellValue(taskSheet, static_cast<xlnt::row_t>(taskRow), columnIndex + 1, values.at(columnIndex));
                applyCellStyle(
                    taskSheet.cell(column(columnIndex + 1), static_cast<xlnt::row_t>(taskRow)),
                    dataFont,
                    rowFill,
                    centered,
                    dataBorder);
            }
            ++taskRow;
        }

        int detailRow = 2;
        for (const TaskDetail &task : tasks) {
            const QVector<Assignment> assignments = sortedAssignments(task.assignments);
            for (const Assignment &assignment : assignments) {
                const QStringList values = {
                    QString::number(task.summary.id),
                    task.summary.name,
                    assignment.absentTeacher,
                    assignment.day,
                    QString::number(assignment.period),
                    assignment.className,
                    assignment.subject,
                    assignment.substituteTeacher,
                    assignment.substituteSubject,
                };
                const xlnt::fill rowFill = detailRow % 2 == 0 ? whiteFill : alternateFill;
                for (int columnIndex = 0; columnIndex < values.size(); ++columnIndex) {
                    setCellValue(
                        detailSheet,
                        static_cast<xlnt::row_t>(detailRow),
                        columnIndex + 1,
                        values.at(columnIndex));
                    applyCellStyle(
                        detailSheet.cell(column(columnIndex + 1), static_cast<xlnt::row_t>(detailRow)),
                        dataFont,
                        rowFill,
                        centered,
                        dataBorder);
                }
                ++detailRow;
            }
        }

        const std::array<double, 8> taskWidths = {10, 24, 12, 22, 14, 14, 22, 14};
        for (int index = 0; index < static_cast<int>(taskWidths.size()); ++index) {
            taskSheet.column_properties(column(index + 1)).width = taskWidths.at(index);
            taskSheet.column_properties(column(index + 1)).custom_width = true;
        }
        const std::array<double, 9> detailWidths = {10, 24, 16, 10, 10, 16, 14, 16, 16};
        for (int index = 0; index < static_cast<int>(detailWidths.size()); ++index) {
            detailSheet.column_properties(column(index + 1)).width = detailWidths.at(index);
            detailSheet.column_properties(column(index + 1)).custom_width = true;
        }

        taskSheet.freeze_panes("A2");
        detailSheet.freeze_panes("A2");
        taskSheet.auto_filter(QStringLiteral("A1:H%1").arg(qMax(1, taskRow - 1)).toStdString());
        detailSheet.auto_filter(QStringLiteral("A1:I%1").arg(qMax(1, detailRow - 1)).toStdString());

        saveWorkbook(workbook, outputPath);
        return true;
    } catch (const std::exception &ex) {
        if (error) {
            *error = QStringLiteral("导出代课数据失败: %1").arg(QString::fromLocal8Bit(ex.what()));
        }
        return false;
    }
}

bool ExcelHelper::exportSubstituteStatistics(
    const QString &outputPath,
    const QVector<TaskDetail> &tasks,
    const QString &rangeText,
    QString *error)
{
    if (error) {
        error->clear();
    }
    if (tasks.isEmpty()) {
        if (error) {
            *error = QStringLiteral("所选范围内没有代课任务。");
        }
        return false;
    }
    if (isXlsPath(outputPath)) {
        if (error) {
            *error = QStringLiteral("代课统计仅支持 xlsx 格式。");
        }
        return false;
    }

    struct SubstituteStat {
        QString teacher;
        QString subject;
        int lessonCount = 0;
        QSet<qint64> taskIds;
    };

    QVector<SubstituteStat> statistics;
    QHash<QString, int> statisticIndexes;
    int totalAssignments = 0;
    for (const TaskDetail &task : tasks) {
        for (const Assignment &assignment : task.assignments) {
            if (assignment.substituteTeacher.trimmed().isEmpty()) {
                continue;
            }
            ++totalAssignments;
            const QString subject = assignment.substituteSubject.trimmed().isEmpty()
                ? assignment.subject
                : assignment.substituteSubject;
            const QString key = assignment.substituteTeacher + QChar(0x1f) + subject;
            int index = statisticIndexes.value(key, -1);
            if (index < 0) {
                index = statistics.size();
                statisticIndexes.insert(key, index);
                statistics.push_back({assignment.substituteTeacher, subject, 0, {}});
            }
            SubstituteStat &stat = statistics[index];
            ++stat.lessonCount;
            stat.taskIds.insert(task.summary.id);
        }
    }
    if (totalAssignments == 0) {
        if (error) {
            *error = QStringLiteral("所选范围内没有代课安排。");
        }
        return false;
    }

    std::sort(statistics.begin(), statistics.end(), [](const SubstituteStat &left, const SubstituteStat &right) {
        if (left.lessonCount != right.lessonCount) {
            return left.lessonCount > right.lessonCount;
        }
        const int teacherOrder = left.teacher.localeAwareCompare(right.teacher);
        return teacherOrder == 0 ? left.subject < right.subject : teacherOrder < 0;
    });

    try {
        xlnt::workbook workbook;
        xlnt::worksheet summarySheet = workbook.active_sheet();
        summarySheet.title(toStdString(QStringLiteral("代课统计")));
        xlnt::worksheet detailSheet = workbook.create_sheet();
        detailSheet.title(toStdString(QStringLiteral("代课明细")));

        const xlnt::font titleFont = createFont("Microsoft YaHei UI", 15.0, true, "FFFFFFFF");
        const xlnt::font subtitleFont = createFont("Microsoft YaHei UI", 10.0, false, "FF8A522F");
        const xlnt::font headerFont = createFont("Microsoft YaHei UI", 10.5, true, "FFFFFFFF");
        const xlnt::font dataFont = createFont("Microsoft YaHei UI", 10.0, false, "FF263238");
        const xlnt::font totalFont = createFont("Microsoft YaHei UI", 10.5, true, "FF8A522F");
        const xlnt::fill titleFill = xlnt::fill::solid(makeColor("FFD97745"));
        const xlnt::fill subtitleFill = xlnt::fill::solid(makeColor("FFFFF4EC"));
        const xlnt::fill headerFill = xlnt::fill::solid(makeColor("FFD97745"));
        const xlnt::fill whiteFill = xlnt::fill::solid(makeColor("FFFFFFFF"));
        const xlnt::fill alternateFill = xlnt::fill::solid(makeColor("FFF8F9FA"));
        const xlnt::fill totalFill = xlnt::fill::solid(makeColor("FFFFE6D5"));
        const xlnt::alignment centered = centeredAlignment(true);
        const xlnt::border headerBorder = createBorder(xlnt::border_style::thin, "FFB85F2E");
        const xlnt::border dataBorder = createBorder(xlnt::border_style::thin, "FFDDE2E6");

        summarySheet.merge_cells("A1:E1");
        summarySheet.merge_cells("A2:E2");
        setCellValue(summarySheet, 1, 1, QStringLiteral("代课统计汇总"));
        setCellValue(
            summarySheet,
            2,
            1,
            QStringLiteral("统计范围：%1　任务数：%2　代课安排：%3节")
                .arg(rangeText)
                .arg(tasks.size())
                .arg(totalAssignments));
        applyCellStyle(summarySheet.cell("A1"), titleFont, titleFill, centered, headerBorder);
        applyCellStyle(summarySheet.cell("A2"), subtitleFont, subtitleFill, centered, dataBorder);

        const QStringList summaryHeaders = {
            QStringLiteral("序号"),
            QStringLiteral("代课教师"),
            QStringLiteral("学科"),
            QStringLiteral("代课节数"),
            QStringLiteral("涉及任务数"),
        };
        for (int columnIndex = 0; columnIndex < summaryHeaders.size(); ++columnIndex) {
            setCellValue(summarySheet, 4, columnIndex + 1, summaryHeaders.at(columnIndex));
            applyCellStyle(
                summarySheet.cell(column(columnIndex + 1), 4),
                headerFont,
                headerFill,
                centered,
                headerBorder);
        }

        int summaryRow = 5;
        for (int index = 0; index < statistics.size(); ++index) {
            const SubstituteStat &stat = statistics.at(index);
            const QStringList values = {
                QString::number(index + 1),
                stat.teacher,
                stat.subject,
                QString::number(stat.lessonCount),
                QString::number(stat.taskIds.size()),
            };
            const xlnt::fill rowFill = summaryRow % 2 == 0 ? alternateFill : whiteFill;
            for (int columnIndex = 0; columnIndex < values.size(); ++columnIndex) {
                setCellValue(summarySheet, static_cast<xlnt::row_t>(summaryRow), columnIndex + 1, values.at(columnIndex));
                applyCellStyle(
                    summarySheet.cell(column(columnIndex + 1), static_cast<xlnt::row_t>(summaryRow)),
                    dataFont,
                    rowFill,
                    centered,
                    dataBorder);
            }
            ++summaryRow;
        }
        setCellValue(summarySheet, static_cast<xlnt::row_t>(summaryRow), 1, QStringLiteral("合计"));
        setCellValue(summarySheet, static_cast<xlnt::row_t>(summaryRow), 4, totalAssignments);
        for (int columnIndex = 1; columnIndex <= 5; ++columnIndex) {
            applyCellStyle(
                summarySheet.cell(column(columnIndex), static_cast<xlnt::row_t>(summaryRow)),
                totalFont,
                totalFill,
                centered,
                dataBorder);
        }

        const QStringList detailHeaders = {
            QStringLiteral("任务ID"),
            QStringLiteral("任务名称"),
            QStringLiteral("起始日期"),
            QStringLiteral("结束日期"),
            QStringLiteral("代课原因"),
            QStringLiteral("被代课老师"),
            QStringLiteral("周几"),
            QStringLiteral("节次"),
            QStringLiteral("班级"),
            QStringLiteral("科目"),
            QStringLiteral("代课老师"),
            QStringLiteral("代课老师科目"),
        };
        for (int columnIndex = 0; columnIndex < detailHeaders.size(); ++columnIndex) {
            setCellValue(detailSheet, 1, columnIndex + 1, detailHeaders.at(columnIndex));
            applyCellStyle(
                detailSheet.cell(column(columnIndex + 1), 1),
                headerFont,
                headerFill,
                centered,
                headerBorder);
        }

        int detailRow = 2;
        for (const TaskDetail &task : tasks) {
            const QVector<Assignment> assignments = sortedAssignments(task.assignments);
            for (const Assignment &assignment : assignments) {
                const QStringList values = {
                    QString::number(task.summary.id),
                    task.summary.name,
                    task.summary.startDate,
                    task.summary.endDate,
                    task.summary.reason,
                    assignment.absentTeacher,
                    assignment.day,
                    QString::number(assignment.period),
                    assignment.className,
                    assignment.subject,
                    assignment.substituteTeacher,
                    assignment.substituteSubject,
                };
                const xlnt::fill rowFill = detailRow % 2 == 0 ? whiteFill : alternateFill;
                for (int columnIndex = 0; columnIndex < values.size(); ++columnIndex) {
                    setCellValue(
                        detailSheet,
                        static_cast<xlnt::row_t>(detailRow),
                        columnIndex + 1,
                        values.at(columnIndex));
                    applyCellStyle(
                        detailSheet.cell(column(columnIndex + 1), static_cast<xlnt::row_t>(detailRow)),
                        dataFont,
                        rowFill,
                        centered,
                        dataBorder);
                }
                ++detailRow;
            }
        }

        const std::array<double, 5> summaryWidths = {9, 18, 14, 14, 16};
        for (int index = 0; index < static_cast<int>(summaryWidths.size()); ++index) {
            summarySheet.column_properties(column(index + 1)).width = summaryWidths.at(index);
            summarySheet.column_properties(column(index + 1)).custom_width = true;
        }
        const std::array<double, 12> detailWidths = {10, 24, 14, 14, 12, 16, 10, 10, 16, 14, 16, 16};
        for (int index = 0; index < static_cast<int>(detailWidths.size()); ++index) {
            detailSheet.column_properties(column(index + 1)).width = detailWidths.at(index);
            detailSheet.column_properties(column(index + 1)).custom_width = true;
        }
        summarySheet.freeze_panes("A5");
        detailSheet.freeze_panes("A2");
        summarySheet.auto_filter(
            QStringLiteral("A4:E%1").arg(qMax(4, summaryRow - 1)).toStdString());
        detailSheet.auto_filter(
            QStringLiteral("A1:L%1").arg(qMax(1, detailRow - 1)).toStdString());

        saveWorkbook(workbook, outputPath);
        return true;
    } catch (const std::exception &ex) {
        if (error) {
            *error = QStringLiteral("导出代课统计失败: %1").arg(QString::fromLocal8Bit(ex.what()));
        }
        return false;
    }
}

bool ExcelHelper::exportNotices(
    const QString &templatePath,
    const QString &outputPath,
    const QVector<Assignment> &assignments,
    const QString &reason,
    const QString &startDate,
    const QString &endDate,
    QString *error)
{
    if (assignments.isEmpty()) {
        if (error) {
            *error = QStringLiteral("没有可导出的代课安排。");
        }
        return false;
    }
    if (!QFileInfo::exists(templatePath)) {
        if (error) {
            *error = QStringLiteral("通知单模板不存在。");
        }
        return false;
    }
    QString templateError;
    if (!ExcelHelper::validateWorkbook(templatePath, &templateError)) {
        if (error) {
            *error = templateError;
        }
        return false;
    }

    QString stage = QStringLiteral("加载模板");
    try {
        stage = QStringLiteral("整理通知内容");
        const QVector<NoticeItem> items = buildNoticeItems(assignments);
        if (items.isEmpty()) {
            if (error) {
                *error = QStringLiteral("没有可导出的通知单内容。");
            }
            return false;
        }

        if (isXlsPath(outputPath)) {
            stage = QStringLiteral("生成 xls 通知单");
            return exportNoticesXlsXml(outputPath, items, reason, startDate, endDate, error);
        }

        TemplateSnapshot templateSnapshot;
        bool hasTemplateSnapshot = false;
        if (!isXlsPath(templatePath)) {
            stage = QStringLiteral("读取模板样式");
            xlnt::workbook templateWorkbook;
            loadWorkbook(templateWorkbook, templatePath);
            for (std::size_t sheetIndex = 0; sheetIndex < templateWorkbook.sheet_count(); ++sheetIndex) {
                xlnt::worksheet templateSheet = templateWorkbook.sheet_by_index(sheetIndex);
                QString validationError;
                if (validateNoticeTemplateCells(
                        static_cast<int>(templateSheet.highest_row()),
                        static_cast<int>(templateSheet.highest_column().index),
                        [&](int rowIndex, int columnIndex) {
                            return cellText(templateSheet, static_cast<xlnt::row_t>(rowIndex), columnIndex);
                        },
                        &validationError)) {
                    templateSnapshot = snapshotTemplateBlock(templateSheet);
                    hasTemplateSnapshot = true;
                    break;
                }
            }
        }

        stage = QStringLiteral("创建工作簿");
        xlnt::workbook workbook;
        xlnt::worksheet sheet = workbook.active_sheet();
        initializeNoticeSheet(sheet);

        stage = QStringLiteral("生成通知单");
        int nextStartRow = 1;
        for (const NoticeItem &item : items) {
            const xlnt::row_t startRow = row(nextStartRow);

            stage = item.kind == NoticeItem::Kind::Absent
                ? QStringLiteral("生成被代课通知:%1").arg(item.absentTeacher)
                : QStringLiteral("生成代课通知:%1").arg(item.substituteTeacher);
            writeNoticeBlock(
                sheet,
                startRow,
                item.kind,
                item.absentTeacher,
                item.substituteTeacher,
                item.rows,
                reason,
                startDate,
                endDate);
            nextStartRow += noticeBlockHeight(item.kind, item.rows.size()) + 1;
        }
        if (hasTemplateSnapshot) {
            stage = QStringLiteral("应用模板样式");
            normalizeExportSheet(sheet, templateSnapshot);
        }
        stage = QStringLiteral("保存文件");
        saveWorkbook(workbook, outputPath);
        return true;
    } catch (const std::exception &ex) {
        if (error) {
            const QString detail = QString::fromLocal8Bit(ex.what()).trimmed();
            *error = detail.isEmpty()
                ? QStringLiteral("导出通知单失败，阶段: %1").arg(stage)
                : QStringLiteral("导出通知单失败，阶段: %1，原因: %2").arg(stage, detail);
        }
        return false;
    }
}

}  // namespace substitute
