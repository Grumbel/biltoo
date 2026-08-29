// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THUMBNAILBAR_H
#define THUMBNAILBAR_H

#include <QListWidget>
#include <QStringList>

class ThumbnailBar : public QListWidget
{
    Q_OBJECT

public:
    explicit ThumbnailBar(QWidget *parent = nullptr);

    void setFiles(const QStringList &files);
    void setCurrentIndex(int index);
    int currentIndex() const;

signals:
    void indexActivated(int index);

protected:
    QSize sizeHint() const override;

private slots:
    void onItemActivated(QListWidgetItem *item);
    void onCurrentRowChanged(int row);

private:
    void loadThumbnail(int row);

    static constexpr int kThumbSize = 96;
    static constexpr int kBarHeight = 112;
};

#endif // THUMBNAILBAR_H
