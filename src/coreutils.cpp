#include "coreutils.h"

#include <QRegularExpression>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace substitute {

namespace {

QString dayByNumber(const QString &digit)
{
    static const QHash<QString, QString> kDayMap = {
        {QStringLiteral("1"), QStringLiteral("周一")},
        {QStringLiteral("2"), QStringLiteral("周二")},
        {QStringLiteral("3"), QStringLiteral("周三")},
        {QStringLiteral("4"), QStringLiteral("周四")},
        {QStringLiteral("5"), QStringLiteral("周五")},
        {QStringLiteral("6"), QStringLiteral("周六")},
        {QStringLiteral("7"), QStringLiteral("周日")},
    };
    return kDayMap.value(digit);
}

QChar gbkInitialForCode(int code)
{
    if (code >= 45217 && code <= 45252) return QChar(u'A');
    if (code >= 45253 && code <= 45760) return QChar(u'B');
    if (code >= 45761 && code <= 46317) return QChar(u'C');
    if (code >= 46318 && code <= 46825) return QChar(u'D');
    if (code >= 46826 && code <= 47009) return QChar(u'E');
    if (code >= 47010 && code <= 47296) return QChar(u'F');
    if (code >= 47297 && code <= 47613) return QChar(u'G');
    if (code >= 47614 && code <= 48118) return QChar(u'H');
    if (code >= 48119 && code <= 49061) return QChar(u'J');
    if (code >= 49062 && code <= 49323) return QChar(u'K');
    if (code >= 49324 && code <= 49895) return QChar(u'L');
    if (code >= 49896 && code <= 50370) return QChar(u'M');
    if (code >= 50371 && code <= 50613) return QChar(u'N');
    if (code >= 50614 && code <= 50621) return QChar(u'O');
    if (code >= 50622 && code <= 50905) return QChar(u'P');
    if (code >= 50906 && code <= 51386) return QChar(u'Q');
    if (code >= 51387 && code <= 51445) return QChar(u'R');
    if (code >= 51446 && code <= 52217) return QChar(u'S');
    if (code >= 52218 && code <= 52697) return QChar(u'T');
    if (code >= 52698 && code <= 52979) return QChar(u'W');
    if (code >= 52980 && code <= 53688) return QChar(u'X');
    if (code >= 53689 && code <= 54480) return QChar(u'Y');
    if (code >= 54481 && code <= 55289) return QChar(u'Z');
    return QChar();
}

QChar chineseInitial(const QChar &character)
{
#ifdef Q_OS_WIN
    const wchar_t wideChar = static_cast<wchar_t>(character.unicode());
    char bytes[4] = {};
    const int byteCount = WideCharToMultiByte(
        936,
        0,
        &wideChar,
        1,
        bytes,
        static_cast<int>(sizeof(bytes)),
        nullptr,
        nullptr);
    if (byteCount != 2) {
        return QChar();
    }

    const int code = static_cast<unsigned char>(bytes[0]) * 256
        + static_cast<unsigned char>(bytes[1]);
    return gbkInitialForCode(code);
#else
    Q_UNUSED(character);
    return QChar();
#endif
}

}  // namespace

QString normalizeDay(const QString &value)
{
    QString text = value.trimmed();
    if (text.isEmpty() || text.compare(QStringLiteral("nan"), Qt::CaseInsensitive) == 0) {
        return {};
    }

    text.replace(QStringLiteral("星期"), QString());
    text.replace(QStringLiteral("周"), QString());

    static const QRegularExpression digitExpr(QStringLiteral("([1-7])"));
    const QRegularExpressionMatch digitMatch = digitExpr.match(text);
    if (digitMatch.hasMatch()) {
        return dayByNumber(digitMatch.captured(1));
    }

    static const QHash<QChar, QString> kAlias = {
        {QChar(u'一'), QStringLiteral("周一")},
        {QChar(u'二'), QStringLiteral("周二")},
        {QChar(u'三'), QStringLiteral("周三")},
        {QChar(u'四'), QStringLiteral("周四")},
        {QChar(u'五'), QStringLiteral("周五")},
        {QChar(u'六'), QStringLiteral("周六")},
        {QChar(u'日'), QStringLiteral("周日")},
        {QChar(u'天'), QStringLiteral("周日")},
        {QChar(u'七'), QStringLiteral("周日")},
    };

    for (const QChar &ch : text) {
        if (kAlias.contains(ch)) {
            return kAlias.value(ch);
        }
    }
    return value.trimmed();
}

int normalizePeriod(const QString &value)
{
    static const QRegularExpression numberExpr(QStringLiteral("(\\d+)"));
    const QRegularExpressionMatch match = numberExpr.match(value);
    if (!match.hasMatch()) {
        return 0;
    }
    bool ok = false;
    const int period = match.captured(1).toInt(&ok);
    return ok ? period : 0;
}

QString inferTaskReason(const QString &name)
{
    const QString text = name.trimmed();
    if (text.contains(QStringLiteral("请假"))) {
        return QStringLiteral("请假");
    }
    return QStringLiteral("公差");
}

QString normalizeTaskReason(const QString &reason, const QString &taskName)
{
    const QString text = reason.trimmed();
    if (kTaskReasons.contains(text)) {
        return text;
    }
    return inferTaskReason(taskName);
}

QString teacherInitials(const QString &name)
{
    QString initials;
    const QString text = name.trimmed();
    for (const QChar &character : text) {
        if (character.isLetter() && character.unicode() <= 0x7f) {
            initials.append(character.toUpper());
            continue;
        }

        const QChar initial = chineseInitial(character);
        if (!initial.isNull()) {
            initials.append(initial);
        }
    }

    return initials.isEmpty() ? QStringLiteral("#") : initials;
}

QString teacherInitial(const QString &name)
{
    const QString initials = teacherInitials(name);
    if (initials.isEmpty()) {
        return QStringLiteral("#");
    }

    const QChar first = initials.front();
    return (first.isLetter() && first.unicode() <= 0x7f)
        ? QString(first)
        : QStringLiteral("#");
}

QString assignmentSlotKey(const Assignment &assignment)
{
    return QStringLiteral("%1|%2|%3")
        .arg(assignment.absentTeacher, assignment.day)
        .arg(assignment.period);
}

bool assignmentEquals(const Assignment &left, const Assignment &right)
{
    return left.absentTeacher == right.absentTeacher
        && left.day == right.day
        && left.period == right.period
        && left.className == right.className
        && left.subject == right.subject
        && left.substituteTeacher == right.substituteTeacher
        && left.substituteSubject == right.substituteSubject;
}

QVector<Assignment> mergeAssignments(
    const QVector<Assignment> &baseAssignments,
    const QVector<Assignment> &newAssignments)
{
    QHash<QString, Assignment> merged;
    for (const Assignment &assignment : baseAssignments) {
        merged.insert(assignmentSlotKey(assignment), assignment);
    }
    for (const Assignment &assignment : newAssignments) {
        merged.insert(assignmentSlotKey(assignment), assignment);
    }
    return merged.values().toVector();
}

QString safeFileComponent(const QString &text)
{
    QString value = text.trimmed();
    if (value.isEmpty()) {
        return QStringLiteral("导出文件");
    }

    static const QRegularExpression invalidExpr(QStringLiteral(R"([\\/:*?"<>|])"));
    value.replace(invalidExpr, QStringLiteral("_"));
    return value;
}

QString safeSheetTitle(const QString &base, const QSet<QString> &usedTitles)
{
    QString safe = base.trimmed();
    static const QRegularExpression invalidExpr(QStringLiteral(R"([\\/*?:\[\]])"));
    safe.replace(invalidExpr, QStringLiteral("-"));
    if (safe.isEmpty()) {
        safe = QStringLiteral("代课通知");
    }

    const QString title = safe.left(31);
    if (!usedTitles.contains(title)) {
        return title;
    }

    for (int index = 1; index < 100; ++index) {
        const QString suffix = QStringLiteral("-%1").arg(index);
        const int keepLength = qMax(0, 31 - suffix.size());
        const QString candidate = safe.left(keepLength) + suffix;
        if (!usedTitles.contains(candidate)) {
            return candidate;
        }
    }
    return safe.left(28) + QStringLiteral("-99");
}

}  // namespace substitute
