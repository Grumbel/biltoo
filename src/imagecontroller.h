// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGECONTROLLER_H
#define IMAGECONTROLLER_H

class ImageView;

/**
 * Image-mode collaborator for ImageView.
 *
 * Owns the Image-mode enter transition (canvas prepare, live clear, classic
 * path reload). Gallery/Workspace leave policy (stash) runs in those
 * controllers' onLeave before this enter is called.
 * ImageView remains the QGraphicsView shell and public API surface.
 */
class ImageController
{
public:
    explicit ImageController(ImageView *view);

    /** Enter Image mode from any previous mode (stash already handled by onLeave). */
    void enter();

private:
    ImageView *m_view = nullptr;
};

#endif // IMAGECONTROLLER_H
