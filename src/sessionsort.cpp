// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionsort.h"

#include "pagepath.h"

#include <QCollator>
#include <QtMath>

#include <algorithm>
#include <random>

namespace SessionSort {

bool modeNeedsImageProbe(Mode mode)
{
    return mode == Mode::Width
        || mode == Mode::Height
        || mode == Mode::PixelCount
        || mode == Mode::AspectRatio
        || mode == Mode::MTime
        || mode == Mode::FileSize;
}

bool looksLikePagedDocument(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return false;
    }
    int pages = 0;
    for (const QString &path : paths) {
        if (PagePath::isPageRef(path) || PagePath::isPdfImageRef(path)) {
            ++pages;
        }
    }
    return pages * 2 >= paths.size();
}

QVector<int> orderIndices(Mode mode,
                          const QStringList &paths,
                          const QHash<QString, QSize> &sizes,
                          const QHash<QString, qint64> &mtimes,
                          const QHash<QString, qint64> &fsizes)
{
    auto nameLess = [](const QString &a, const QString &b) {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        return collator.compare(PagePath::displayName(a), PagePath::displayName(b)) < 0;
    };
    auto pathLess = [](const QString &a, const QString &b) {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        return collator.compare(a, b) < 0;
    };

    QVector<int> order(paths.size());
    for (int i = 0; i < order.size(); ++i) {
        order[i] = i;
    }

    if (mode == Mode::Name) {
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            return nameLess(paths.at(ia), paths.at(ib));
        });
        return order;
    }
    if (mode == Mode::Path) {
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            return pathLess(paths.at(ia), paths.at(ib));
        });
        return order;
    }
    if (mode == Mode::Shuffle) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(order.begin(), order.end(), gen);
        return order;
    }

    if (mode == Mode::MTime) {
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const qint64 ta = mtimes.value(paths.at(ia));
            const qint64 tb = mtimes.value(paths.at(ib));
            if (ta != tb) {
                return ta < tb;
            }
            return nameLess(paths.at(ia), paths.at(ib));
        });
    } else if (mode == Mode::FileSize) {
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const qint64 sa = fsizes.value(paths.at(ia));
            const qint64 sb = fsizes.value(paths.at(ib));
            if (sa != sb) {
                return sa < sb;
            }
            return nameLess(paths.at(ia), paths.at(ib));
        });
    } else {
        auto sizeOf = [&](int i) -> QSize {
            return sizes.value(paths.at(i));
        };
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const QSize sa = sizeOf(ia);
            const QSize sb = sizeOf(ib);
            if (mode == Mode::Width) {
                if (sa.width() != sb.width()) {
                    return sa.width() < sb.width();
                }
            } else if (mode == Mode::Height) {
                if (sa.height() != sb.height()) {
                    return sa.height() < sb.height();
                }
            } else if (mode == Mode::AspectRatio) {
                const qreal ra = (sa.height() > 0)
                    ? qreal(sa.width()) / qreal(sa.height()) : 0.0;
                const qreal rb = (sb.height() > 0)
                    ? qreal(sb.width()) / qreal(sb.height()) : 0.0;
                if (!qFuzzyCompare(ra, rb)) {
                    return ra < rb;
                }
            } else { // PixelCount
                const qint64 pa = qint64(sa.width()) * sa.height();
                const qint64 pb = qint64(sb.width()) * sb.height();
                if (pa != pb) {
                    return pa < pb;
                }
            }
            return nameLess(paths.at(ia), paths.at(ib));
        });
    }
    return order;
}

} // namespace SessionSort
