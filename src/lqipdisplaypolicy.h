// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LQIPDISPLAYPOLICY_H
#define LQIPDISPLAYPOLICY_H

#include <QImage>
#include <QString>
#include <utility>

#include "displayquality.h"

/**
 * Pure LQIP underlay sample selection for worker threads.
 * Gallery never PreferCache-encodes whole-frame underlays here (tiles own sharpness).
 */
namespace LqipDisplayPolicy {

/**
 * Host LQIP-band cache sample, else durable LQIP (installed into ImageCache),
 * else any remaining host sample still in the LQIP band. Null if none.
 *
 * Call only off the GUI thread (cache / Store I/O).
 */
QImage lqipOrCachedSample(const QString &path);

/**
 * Gallery SoftPreview install gates (LQIP underlay only).
 * @return false when incoming is above the LQIP band (reject whole-frame soft).
 */
bool galleryWithinLqipBand(int incomingEdge, int lqipMaxEdge);

/** Overload using DisplayQuality::kLqipMaxEdge. */
inline bool galleryWithinLqipBand(int incomingEdge)
{
    return galleryWithinLqipBand(incomingEdge, DisplayQuality::kLqipMaxEdge);
}

/**
 * True when a larger LQIP may replace the shown underlay (never soft climb).
 */
bool galleryAcceptsLqipUpgrade(int shownEdge, int incomingEdge, int lqipMaxEdge);

inline bool galleryAcceptsLqipUpgrade(int shownEdge, int incomingEdge)
{
    return galleryAcceptsLqipUpgrade(shownEdge, incomingEdge, DisplayQuality::kLqipMaxEdge);
}

/** Aggregate max display long edge and whether any sample is FullSource. */
struct PathHaveEdge {
    int have = 0;
    bool anyFull = false;
};

/**
 * Reduce per-item display edges for one path (caller filters items by path).
 * @p displayEdges and @p decoded parallel; sizes must match.
 */
PathHaveEdge aggregatePathHaveEdge(const int *displayEdges, const bool *decoded,
                                   int count);

} // namespace LqipDisplayPolicy

#endif // LQIPDISPLAYPOLICY_H
