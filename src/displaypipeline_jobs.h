// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYPIPELINE_JOBS_H
#define DISPLAYPIPELINE_JOBS_H

#include "sessionappearance.h"

#include <QImage>
#include <QPointer>
#include <QString>

class ImageView;

/** Queue onImagePreviewLoaded on the GUI thread; no-op if @a guard is gone. */
void queuePreviewLoaded(const QPointer<ImageView> &guard, const QString &path,
                        const QImage &preview, quint64 gen, int role);

/** Queue onImageLoaded on the GUI thread; no-op if @a guard is gone. */
void queueImageLoaded(const QPointer<ImageView> &guard, const QString &path,
                      const QImage &image, quint64 gen, int role);

/** Worker LQIP/soft underlay — see SoftDisplayPolicy::lqipOrCachedSoft. */
QImage loadSoftPreviewPixels(const QString &path, int softEdge);

/** LQIP seed job only. Never SoftOnly / PreferCache soft encode. */
void startSoftPreviewJob(const QPointer<ImageView> &guard, const QString &path,
                         quint64 gen, int roleInt, int softEdge,
                         const WorkspaceItemState &sessionApp);

/** PreferCache / loadThumbnail at a display edge (slideshow quality climb). */
void startDisplayQualityJob(const QPointer<ImageView> &guard, const QString &path,
                            quint64 gen, int roleInt, int qualityEdge,
                            const WorkspaceItemState &sessionApp);

#endif // DISPLAYPIPELINE_JOBS_H
