// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYPIPELINE_JOBS_H
#define DISPLAYPIPELINE_JOBS_H

#include "session/sessionappearance.h"

#include <QImage>
#include <QPointer>
#include <QString>

class QObject;
class DisplayPipelineController;

/**
 * Async load helpers for DisplayPipelineController (Dual ImageView Stage 2a).
 *
 * Lifetime is the host QObject (ImageView); behaviour is on the pipeline.
 * When @p life is destroyed, queued GUI work is a no-op. @p pipe must remain
 * owned by that host for the duration of the job (pipeline is a member of
 * ImageView).
 */

/** Queue pipeline onImagePreviewLoaded on the GUI thread; no-op if @a life is gone. */
void queuePreviewLoaded(const QPointer<QObject> &life, DisplayPipelineController *pipe,
                        const QString &path, const QImage &preview, quint64 gen, int role);

/** Queue pipeline onImageLoaded on the GUI thread; no-op if @a life is gone. */
void queueImageLoaded(const QPointer<QObject> &life, DisplayPipelineController *pipe,
                      const QString &path, const QImage &image, quint64 gen, int role);

/** Worker LQIP/soft underlay — see LqipDisplayPolicy::lqipOrCachedSample. */
QImage loadSoftPreviewPixels(const QString &path, int softEdge);

/** LQIP seed job only. Never SoftOnly / PreferCache soft encode. */
void startSoftPreviewJob(const QPointer<QObject> &life, DisplayPipelineController *pipe,
                         const QString &path, quint64 gen, int roleInt, int softEdge,
                         const WorkspaceItemState &sessionApp);

/** PreferCache / loadThumbnail at a display edge (slideshow quality climb). */
void startDisplayQualityJob(const QPointer<QObject> &life, DisplayPipelineController *pipe,
                            const QString &path, quint64 gen, int roleInt, int qualityEdge,
                            const WorkspaceItemState &sessionApp);

#endif // DISPLAYPIPELINE_JOBS_H
