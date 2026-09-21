// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionsort.h"

#include "pagepath.h"

#include <QtMath>

#include <algorithm>
#include <random>

namespace SessionSort {

namespace {

/**
 * Natural order without QCollator: Qt's numeric collator needs ICU; headless
 * Nix builds often ship without it, so b10 sorted before b2. Digit runs compare
 * as integers; other runs are case-insensitive UTF-16 code units.
 */
bool naturalLess(const QString &a, const QString &b)
{
    const int na = a.size();
    const int nb = b.size();
    int ia = 0;
    int ib = 0;
    while (ia < na && ib < nb) {
        const QChar ca = a.at(ia);
        const QChar cb = b.at(ib);
        if (ca.isDigit() && cb.isDigit()) {
            // Skip leading zeros but remember length for equal-value tie-break.
            int za = ia;
            while (za < na && a.at(za) == QLatin1Char('0')) {
                ++za;
            }
            int zb = ib;
            while (zb < nb && b.at(zb) == QLatin1Char('0')) {
                ++zb;
            }
            int ea = za;
            while (ea < na && a.at(ea).isDigit()) {
                ++ea;
            }
            int eb = zb;
            while (eb < nb && b.at(eb).isDigit()) {
                ++eb;
            }
            const int lena = ea - za;
            const int lenb = eb - zb;
            if (lena != lenb) {
                return lena < lenb;
            }
            for (int k = 0; k < lena; ++k) {
                const ushort da = a.at(za + k).unicode();
                const ushort db = b.at(zb + k).unicode();
                if (da != db) {
                    return da < db;
                }
            }
            // Equal numeric value: fewer leading zeros sorts first (stable feel).
            const int zlena = ea - ia;
            const int zlenb = eb - ib;
            if (zlena != zlenb) {
                return zlena < zlenb;
            }
            ia = ea;
            ib = eb;
            continue;
        }
        const QChar la = ca.toLower();
        const QChar lb = cb.toLower();
        if (la != lb) {
            return la.unicode() < lb.unicode();
        }
        ++ia;
        ++ib;
    }
    return (na - ia) < (nb - ib);
}

} // namespace

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
        return naturalLess(PagePath::displayName(a), PagePath::displayName(b));
    };
    auto pathLess = [](const QString &a, const QString &b) {
        return naturalLess(a, b);
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
