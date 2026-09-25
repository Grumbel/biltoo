// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGESIZECOORDINATOR_H
#define IMAGESIZECOORDINATOR_H

#include "item/imagesizebook.h"
#include "imageview_types.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

class ImageView;

/**
 * Owns the session ImageSizeBook and probe/remember policy (SIZE.md).
 *
 * ImageView remains the DisplayPipelineHost surface and GallerySizeResolveHost;
 * applyProbedImageSize body lives on DisplayPipelineController; ImageView is a thin router.
 */
class ImageSizeCoordinator
{
public:
    explicit ImageSizeCoordinator(ImageView *view);

    ImageSizeBook &book() { return m_book; }
    const ImageSizeBook &book() const { return m_book; }

    void rememberImageSize(const QString &path, const QSize &size);
    /** Drop session size-book entry so hard reload can re-probe (SIZE.md). */
    void forgetLogicalSize(const QString &path);
    void rememberSizeFromDecode(const QString &path, const QImage &image);

    QSize imageSizeForPath(const QString &path);
    QSize layoutSizeForPath(const QString &path, const QImage &previewHint);
    QSize logicalSizeForPath(const QString &path) const;
    QSize ensureLogicalSizeForPath(const QString &path);

    void primeGalleryGeometryFromCache(const QStringList &paths);
    void scheduleImageSizeProbe(const QString &path);

private:
    ImageView *m_view = nullptr;
    ImageSizeBook m_book;
};

#endif // IMAGESIZECOORDINATOR_H
