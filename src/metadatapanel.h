// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef METADATAPANEL_H
#define METADATAPANEL_H

#include <QWidget>

class QTreeWidget;
class QLabel;

/**
 * Side panel listing file info and embedded image metadata.
 * When built with libexiv2 (QIMGVIEW_HAVE_EXIV2), shows Exif/IPTC/XMP in
 * grouped sections with a camera summary. Otherwise falls back to
 * QImageReader text keys.
 */
class MetadataPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MetadataPanel(QWidget *parent = nullptr);

    void clear();
    void setImagePath(const QString &path);

private:
    void addRow(const QString &key, const QString &value);

    QLabel *m_header = nullptr;
    QTreeWidget *m_tree = nullptr;
};

#endif // METADATAPANEL_H
