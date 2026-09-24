// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView thin routers co-located with text/ ownership.

#include "imageview.h"
#include "text/textsearchpolicy.h"

// --- from src/imageview_text.cpp ---


bool ImageView::hitTextLinkAt(const QPoint &viewPos, int *pageOut, QString *uriOut) const
{
    return m_textCtrl.hitLinkAt(viewPos, pageOut, uriOut);
}

QString ImageView::selectedText() const
{
    return m_textCtrl.selectedText();
}



