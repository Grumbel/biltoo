// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYSURFACE_H
#define DISPLAYSURFACE_H

#include "content/contentxform.h"
#include "imageview_types.h"

#include <QHash>
#include <QString>
#include <QtGlobal>

/**
 * Display surface decision layer (docs/DISPLAY_SURFACE.md).
 *
 * Pure decide() is the install policy. DisplaySurfaceController holds per-surface
 * state (bind / need / freeze). PathRaster + materialize wiring is a later phase;
 * ImageView must not reimplement these rules.
 */
namespace DisplaySurface {

enum class Kind {
    ImageFocus = 0,
    GalleryTile,
    FilmstripCell,
    WorkspaceItem,
    SlideshowPhase,
};

enum class AttachedKind {
    None = 0,
    SoftPreview,
    FullSource,
};

enum class ActionType {
    None = 0,
    /** Materialize host on GUI as SoftPreview (stand-in only). */
    AttachSoft,
    /** Materialize host on GUI as FullSource (host edge ≤ GUI max). */
    AttachFull,
    /** PathRasterService::ensure — no host or host still below need. */
    ScheduleClimb,
    /** Worker full bake for multi-MP + content want; do not soft-loop. */
    ScheduleAsyncMaterialize,
};

struct State {
    int needEdge = 0;
    int haveDisplayEdge = 0;
    AttachedKind attachedKind = AttachedKind::None;
    ContentXform::Value applied;
    ContentXform::Value want;
    bool frozen = false;
    bool climbPending = false;
    /** ImageCache long edge for path; 0 = no host sample. Pre-crop. */
    int hostLongEdge = 0;
};

struct Action {
    ActionType type = ActionType::None;
    int climbNeedEdge = 0;
};

/**
 * Pure install policy. When FullSource already matches want, settles on
 * post-crop display vs need (or rematerializes if host projects a better
 * post-crop edge). Never treats raw pre-crop host ≫ post-crop shown as an
 * upgrade by itself (crop soft↔full pulse root cause).
 */
Action decide(const State &s);

using SurfaceId = qint64;
inline constexpr SurfaceId kInvalidSurfaceId = 0;

struct Binding {
    SurfaceId id = kInvalidSurfaceId;
    Kind kind = Kind::ImageFocus;
    QString path;
    SessionImageId sessionId = kInvalidSessionImageId;
    quint64 generation = 0;
    State state;
};

} // namespace DisplaySurface

/**
 * Registry only in this tip — no PathRaster / ImageCache / signals yet.
 * Consumers will bind surfaces and drive decide() after setNeed / freeze /
 * host edge updates in later tips.
 */
class DisplaySurfaceController
{
public:
    DisplaySurface::SurfaceId bind(DisplaySurface::Kind kind, const QString &path,
                                   SessionImageId sessionId);
    void unbind(DisplaySurface::SurfaceId id);

    bool setNeed(DisplaySurface::SurfaceId id, int needEdge);
    bool setFrozen(DisplaySurface::SurfaceId id, bool frozen);
    bool setHostLongEdge(DisplaySurface::SurfaceId id, int hostLongEdge);
    bool setClimbPending(DisplaySurface::SurfaceId id, bool pending);
    bool setAttached(DisplaySurface::SurfaceId id, DisplaySurface::AttachedKind kind,
                     int haveDisplayEdge, const ContentXform::Value &applied);
    bool setWant(DisplaySurface::SurfaceId id, const ContentXform::Value &want);

    DisplaySurface::Action evaluate(DisplaySurface::SurfaceId id) const;

    const DisplaySurface::Binding *binding(DisplaySurface::SurfaceId id) const;
    int surfaceCount() const { return m_byId.size(); }

private:
    DisplaySurface::Binding *mutableBinding(DisplaySurface::SurfaceId id);

    QHash<DisplaySurface::SurfaceId, DisplaySurface::Binding> m_byId;
    DisplaySurface::SurfaceId m_nextId = 1;
    quint64 m_generationClock = 1;
};

#endif
