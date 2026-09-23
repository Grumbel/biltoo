// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/displaysurface.h"

namespace DisplaySurface {

namespace {

/** Strict long-edge cover (same as DisplayEdgePolicy::coversEdge). */
bool coversNeed(int haveLongEdge, int needEdge)
{
    if (needEdge <= 0) {
        return true;
    }
    if (haveLongEdge <= 0) {
        return false;
    }
    return haveLongEdge >= needEdge;
}

} // namespace

Action decide(const State &s)
{
    Action a;

    if (s.frozen) {
        a.type = ActionType::None;
        return a;
    }

    const bool xformEqual = ContentXform::equal(s.applied, s.want);
    const bool hasHost = s.hostLongEdge > 0;
    const int climbNeed =
        s.needEdge > 0 ? s.needEdge : ContentXform::kGuiMaterializeMaxEdge;
    const bool needUnmet = !coversNeed(s.haveDisplayEdge, s.needEdge);
    const bool hostCoversNeed = hasHost && coversNeed(s.hostLongEdge, s.needEdge);

    // FullSource matching want: never soft-demote via host≫shown (crop pulse).
    // Settled only when post-crop *display* meets need, or the current host
    // cannot improve the post-crop bake further. PreferCache plateau (host and
    // have both ~1024 while need is 2048+) must still ScheduleClimb.
    //
    // Wrong prior rule: hostCoversNeed → None even when have was a Soft-sourced
    // crop bake (host later native → stuck low-res crop). Compare projected
    // post-crop edges (ContentXform::estimatedDisplayLongEdge), not host vs shown.
    if (s.attachedKind == AttachedKind::FullSource && xformEqual) {
        if (!needUnmet) {
            a.type = ActionType::None;
            return a;
        }
        const int estFromHost =
            ContentXform::estimatedDisplayLongEdge(s.hostLongEdge, s.want);
        const bool hostImprovesDisplay =
            hasHost && estFromHost > s.haveDisplayEdge;
        if (hostImprovesDisplay) {
            if (s.hostLongEdge > ContentXform::kGuiMaterializeMaxEdge) {
                a.type = ActionType::ScheduleAsyncMaterialize;
                return a;
            }
            a.type = ActionType::AttachFull;
            return a;
        }
        // Host cannot improve post-crop pixels — climb for a better host if
        // the pre-crop sample itself is still short of need (uncropped path
        // and oversized crops both need this).
        if (!hostCoversNeed) {
            if (s.climbPending) {
                a.type = ActionType::None;
                return a;
            }
            a.type = ActionType::ScheduleClimb;
            a.climbNeedEdge = climbNeed;
            return a;
        }
        a.type = ActionType::None;
        return a;
    }

    // Soft matching want: escalate host / need.
    // Prefer a larger host *before* further climb (Prefer 1024 while Soft 512).
    // Use raw host edge vs shown for Soft: post-crop estimate can be *smaller*
    // than a soft stand-in (crop soft over-painted), which blocked
    // ScheduleAsyncMaterialize for large host + crop (decide_softEqualWant_largeHost).
    if (s.attachedKind == AttachedKind::SoftPreview && xformEqual) {
        if (!hasHost) {
            if (s.climbPending || !needUnmet) {
                a.type = ActionType::None;
                return a;
            }
            a.type = ActionType::ScheduleClimb;
            a.climbNeedEdge = climbNeed;
            return a;
        }
        if (s.hostLongEdge > s.haveDisplayEdge) {
            if (s.hostLongEdge > ContentXform::kGuiMaterializeMaxEdge) {
                a.type = ActionType::ScheduleAsyncMaterialize;
                return a;
            }
            // Soft-band host (≤ kGuiMaterializeMaxEdge) must stay SoftPreview.
            // AttachFull marked hasDecodedPixels and Gallery treated the tile as
            // final (anyFull → no soft/PreferCache climb) while still ≤512 soft.
            a.type = ActionType::AttachSoft;
            return a;
        }
        if (needUnmet && !hostCoversNeed) {
            if (s.climbPending) {
                a.type = ActionType::None;
                return a;
            }
            a.type = ActionType::ScheduleClimb;
            a.climbNeedEdge = climbNeed;
            return a;
        }
        a.type = ActionType::None;
        return a;
    }

    // Blank or appearance changed.
    if (!hasHost) {
        if (s.climbPending) {
            a.type = ActionType::None;
            return a;
        }
        a.type = ActionType::ScheduleClimb;
        a.climbNeedEdge = climbNeed;
        return a;
    }

    if (s.hostLongEdge > ContentXform::kGuiMaterializeMaxEdge) {
        if (s.attachedKind == AttachedKind::None || !xformEqual) {
            if (s.attachedKind == AttachedKind::None || s.haveDisplayEdge <= 0
                || !xformEqual) {
                if (s.attachedKind == AttachedKind::None || s.haveDisplayEdge <= 0) {
                    a.type = ActionType::AttachSoft;
                    return a;
                }
            }
        }
        a.type = ActionType::ScheduleAsyncMaterialize;
        return a;
    }

    // Soft-band host on a blank/mismatched surface: SoftPreview, not FullSource.
    // FullSource here permanently blocked Gallery soft→PreferCache upscale.
    a.type = ActionType::AttachSoft;
    return a;
}

} // namespace DisplaySurface

DisplaySurface::SurfaceId DisplaySurfaceController::bind(DisplaySurface::Kind kind,
                                                         const QString &path,
                                                         SessionImageId sessionId)
{
    DisplaySurface::Binding b;
    b.id = m_nextId++;
    if (b.id == DisplaySurface::kInvalidSurfaceId) {
        b.id = m_nextId++;
    }
    b.kind = kind;
    b.path = path;
    b.sessionId = sessionId;
    b.generation = m_generationClock++;
    m_byId.insert(b.id, b);
    return b.id;
}

void DisplaySurfaceController::unbind(DisplaySurface::SurfaceId id)
{
    m_byId.remove(id);
}

DisplaySurface::Binding *DisplaySurfaceController::mutableBinding(
    DisplaySurface::SurfaceId id)
{
    auto it = m_byId.find(id);
    if (it == m_byId.end()) {
        return nullptr;
    }
    return &(*it);
}

const DisplaySurface::Binding *DisplaySurfaceController::binding(
    DisplaySurface::SurfaceId id) const
{
    auto it = m_byId.constFind(id);
    if (it == m_byId.cend()) {
        return nullptr;
    }
    return &(*it);
}

bool DisplaySurfaceController::setNeed(DisplaySurface::SurfaceId id, int needEdge)
{
    DisplaySurface::Binding *b = mutableBinding(id);
    if (!b) {
        return false;
    }
    b->state.needEdge = needEdge > 0 ? needEdge : 0;
    return true;
}

bool DisplaySurfaceController::setFrozen(DisplaySurface::SurfaceId id, bool frozen)
{
    DisplaySurface::Binding *b = mutableBinding(id);
    if (!b) {
        return false;
    }
    b->state.frozen = frozen;
    return true;
}

bool DisplaySurfaceController::setHostLongEdge(DisplaySurface::SurfaceId id,
                                               int hostLongEdge)
{
    DisplaySurface::Binding *b = mutableBinding(id);
    if (!b) {
        return false;
    }
    b->state.hostLongEdge = hostLongEdge > 0 ? hostLongEdge : 0;
    return true;
}

bool DisplaySurfaceController::setClimbPending(DisplaySurface::SurfaceId id,
                                               bool pending)
{
    DisplaySurface::Binding *b = mutableBinding(id);
    if (!b) {
        return false;
    }
    b->state.climbPending = pending;
    return true;
}

bool DisplaySurfaceController::setAttached(DisplaySurface::SurfaceId id,
                                           DisplaySurface::AttachedKind kind,
                                           int haveDisplayEdge,
                                           const ContentXform::Value &applied)
{
    DisplaySurface::Binding *b = mutableBinding(id);
    if (!b) {
        return false;
    }
    b->state.attachedKind = kind;
    b->state.haveDisplayEdge = haveDisplayEdge > 0 ? haveDisplayEdge : 0;
    b->state.applied = applied;
    return true;
}

bool DisplaySurfaceController::setWant(DisplaySurface::SurfaceId id,
                                       const ContentXform::Value &want)
{
    DisplaySurface::Binding *b = mutableBinding(id);
    if (!b) {
        return false;
    }
    b->state.want = want;
    return true;
}

DisplaySurface::Action DisplaySurfaceController::evaluate(
    DisplaySurface::SurfaceId id) const
{
    const DisplaySurface::Binding *b = binding(id);
    if (!b) {
        DisplaySurface::Action a;
        a.type = DisplaySurface::ActionType::None;
        return a;
    }
    return DisplaySurface::decide(b->state);
}
