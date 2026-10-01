// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BILTOO_FACE_FACEGALLERY_H
#define BILTOO_FACE_FACEGALLERY_H

#include "face/facetypes.h"

#include <QString>
#include <QVector>

namespace biltoo::face {

/**
 * Local identity gallery (label + embedding). Persists as JSON under
 * QStandardPaths::AppDataLocation / face_gallery.json.
 *
 * model_id is stored so a future embedder swap does not silently compare
 * incompatible vectors.
 */
class FaceGallery {
public:
    FaceGallery() = default;

    const QVector<FaceIdentity> &identities() const { return m_ids; }

    bool load();
    bool save() const;

    /** Add or replace by id. Empty id → new UUID. Returns the stored id. */
    QString upsert(FaceIdentity identity);

    bool remove(const QString &id);
    void clear();

    /**
     * Best cosine match among identities with the same embeddingModelId.
     * @return index in identities() or -1.
     */
    int bestMatch(const QVector<float> &embedding, const QString &modelId,
                  float *scoreOut = nullptr) const;

private:
    QString storePath() const;

    QVector<FaceIdentity> m_ids;
};

} // namespace biltoo::face

#endif
