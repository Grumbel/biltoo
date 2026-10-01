// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "face/facegallery.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

#include <cmath>

namespace biltoo::face {
namespace {

float cosine(const QVector<float> &a, const QVector<float> &b)
{
    if (a.isEmpty() || a.size() != b.size()) {
        return 0.f;
    }
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < a.size(); ++i) {
        const double x = double(a[i]);
        const double y = double(b[i]);
        dot += x * y;
        na += x * x;
        nb += y * y;
    }
    if (na <= 0.0 || nb <= 0.0) {
        return 0.f;
    }
    return float(dot / (std::sqrt(na) * std::sqrt(nb)));
}

} // namespace

QString FaceGallery::storePath() const
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return root + QStringLiteral("/face_gallery.json");
}

bool FaceGallery::load()
{
    m_ids.clear();
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return false;
    }
    const QJsonArray arr = doc.object().value(QStringLiteral("identities")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        FaceIdentity id;
        id.id = o.value(QStringLiteral("id")).toString();
        id.label = o.value(QStringLiteral("label")).toString();
        id.embeddingModelId = o.value(QStringLiteral("model_id")).toString();
        const QJsonArray emb = o.value(QStringLiteral("embedding")).toArray();
        id.embedding.reserve(emb.size());
        for (const QJsonValue &e : emb) {
            id.embedding.append(float(e.toDouble()));
        }
        if (!id.id.isEmpty() && !id.embedding.isEmpty()) {
            m_ids.append(id);
        }
    }
    return true;
}

bool FaceGallery::save() const
{
    const QString path = storePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray arr;
    for (const FaceIdentity &id : m_ids) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), id.id);
        o.insert(QStringLiteral("label"), id.label);
        o.insert(QStringLiteral("model_id"), id.embeddingModelId);
        QJsonArray emb;
        for (float x : id.embedding) {
            emb.append(double(x));
        }
        o.insert(QStringLiteral("embedding"), emb);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("identities"), arr);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

QString FaceGallery::upsert(FaceIdentity identity)
{
    if (identity.id.isEmpty()) {
        identity.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    for (int i = 0; i < m_ids.size(); ++i) {
        if (m_ids[i].id == identity.id) {
            m_ids[i] = identity;
            save();
            return identity.id;
        }
    }
    m_ids.append(identity);
    save();
    return identity.id;
}

bool FaceGallery::remove(const QString &id)
{
    for (int i = 0; i < m_ids.size(); ++i) {
        if (m_ids[i].id == id) {
            m_ids.removeAt(i);
            save();
            return true;
        }
    }
    return false;
}

void FaceGallery::clear()
{
    m_ids.clear();
    save();
}

int FaceGallery::bestMatch(const QVector<float> &embedding, const QString &modelId,
                           float *scoreOut) const
{
    int best = -1;
    float bestScore = -2.f;
    for (int i = 0; i < m_ids.size(); ++i) {
        if (!modelId.isEmpty() && m_ids[i].embeddingModelId != modelId) {
            continue;
        }
        const float s = cosine(embedding, m_ids[i].embedding);
        if (s > bestScore) {
            bestScore = s;
            best = i;
        }
    }
    if (scoreOut) {
        *scoreOut = best >= 0 ? bestScore : 0.f;
    }
    return best;
}

} // namespace biltoo::face
