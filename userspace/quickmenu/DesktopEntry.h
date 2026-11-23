#pragma once

#include <QByteArray>
#include <QColor>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <cmath>

namespace DesktopEntry {

using Fields = QMap<QString, QString>;

inline QString unescape(const QString &value)
{
    QString result;
    result.reserve(value.size());
    for (int index = 0; index < value.size(); ++index) {
        if (value.at(index) != QLatin1Char('\\') || index + 1 >= value.size()) {
            result.append(value.at(index));
            continue;
        }
        const QChar next = value.at(++index);
        switch (next.unicode()) {
        case 's': result.append(QLatin1Char(' ')); break;
        case 'n': result.append(QLatin1Char('\n')); break;
        case 't': result.append(QLatin1Char('\t')); break;
        case 'r': result.append(QLatin1Char('\r')); break;
        case '\\': result.append(QLatin1Char('\\')); break;
        default: result.append(next); break;
        }
    }
    return result;
}

inline Fields parse(const QByteArray &contents)
{
    Fields entry;
    bool inGroup = false;
    bool found = false;
    const QStringList lines = QString::fromUtf8(contents).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            inGroup = line == QLatin1String("[Desktop Entry]");
            found = found || inGroup;
            continue;
        }
        if (!inGroup)
            continue;
        const int separator = static_cast<int>(line.indexOf(QLatin1Char('=')));
        if (separator <= 0)
            continue;
        const QString key = line.left(separator).trimmed();
        if (!entry.contains(key))
            entry.insert(key, unescape(line.mid(separator + 1).trimmed()));
    }
    if (!found)
        entry.clear();
    return entry;
}

inline QString localized(const Fields &entry, const QString &key)
{
    const QString locale = QLocale().name();
    const QString language = locale.section(QLatin1Char('_'), 0, 0);
    for (const QString &candidate : {locale, language}) {
        if (candidate.isEmpty() || candidate == QLatin1String("C"))
            continue;
        const QString localized = entry.value(key + QLatin1Char('[') + candidate + QLatin1Char(']'));
        if (!localized.isEmpty())
            return localized;
    }
    return entry.value(key);
}

inline bool readNumber(const Fields &entry, const QString &key, double minimum, double maximum,
                double &value, QString &reason)
{
    if (!entry.contains(key))
        return true;
    bool valid = false;
    const double number = QString(entry.value(key)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&valid);
    if (!valid || !std::isfinite(number) || number < minimum || number > maximum) {
        reason = QStringLiteral("%1 must be a number between %2 and %3").arg(key).arg(minimum).arg(maximum);
        return false;
    }
    value = number;
    return true;
}

inline bool readColor(const Fields &entry, const QString &key, QColor &value, QString &reason)
{
    if (!entry.contains(key))
        return true;
    static const QRegularExpression pattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    if (!pattern.match(entry.value(key)).hasMatch()) {
        reason = QStringLiteral("%1 must be a #rrggbb color").arg(key);
        return false;
    }
    value = QColor(entry.value(key));
    return true;
}


inline bool readInteger(const Fields &entry, const QString &key, int minimum, int maximum, int &value,
                        QString &reason)
{
    if (!entry.contains(key))
        return true;
    bool valid = false;
    const int number = entry.value(key).toInt(&valid);
    if (!valid || number < minimum || number > maximum) {
        reason = QStringLiteral("%1 must be an integer between %2 and %3").arg(key).arg(minimum).arg(maximum);
        return false;
    }
    value = number;
    return true;
}

}
