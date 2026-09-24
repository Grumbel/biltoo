// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <cstdlib>
#include "imageview.h"
#include "util/biltoo_thread.h"
#include <QScrollBar>
#include <QtMath>
#include "workspace/workspacegeometry.h"
#include "view/viewtransform.h"
#include "session/sessionappearance.h"
#include "util/ttfp_trace.h"
#include "display/imagecache.h"
#include "imageitem.h"
#include "host/imageloader.h"

#include <QSet>
#include <QDebug>
#include <QHash>
#include <QMultiHash>
#include <QPointer>
#include <QTimer>
#include <QUndoStack>
#include <QGraphicsItem>

QList<ImageItem *> ImageView::collectDoomedWorkspaceItems(const QStringList &paths,
                                                          const QVector<SessionImageId> &sessionIds) const
{
    return m_workspace.collectDoomedItems(paths, sessionIds);
}

void ImageView::destroyDoomedWorkspaceItems(const QList<ImageItem *> &doomed)
{
    m_workspace.destroyDoomedItems(doomed);
}

void ImageView::finishSetWorkspacePaths(bool haveIds, const QStringList &paths,
                                        const QVector<SessionImageId> &sessionIds)
{
    m_workspace.finishPathsSet(haveIds, paths, sessionIds);
}


void ImageView::setWorkspacePaths(const QStringList &paths,
                                  const QVector<SessionImageId> &sessionIds)
{
    m_workspace.setPaths(paths, sessionIds);
}


void ImageView::reorderItemsByPaths(const QStringList &paths,
                                    const QVector<SessionImageId> &ids)
{
    m_workspace.reorderItemsByPaths(paths, ids);
}

void ImageView::rebindWorkspaceSession(const QStringList &sessionFiles,
                                       const QVector<SessionImageId> &sessionIds)
{
    m_workspace.rebindSession(sessionFiles, sessionIds);
}

bool ImageView::pathOnLiveCanvas(const QString &path) const
{
    return m_workspace.pathOnLiveCanvas(path);
}

