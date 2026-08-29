// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THUMBNAILBAR_H
#define THUMBNAILBAR_H

#include <QListWidget>
#include <QStringList>
#include <atomic>

class ThumbnailBar : public QListWidget
{
    Q_OBJECT

public:
    explicit ThumbnailBar(QWidget *parent = nullptr);
    ~ThumbnailBar() override;

    void setFiles(const QStringList &files);
    void setCurrentIndex(int index);
    int currentIndex() const;

signals:
    void indexActivated(int index);
    void indexDoubleClicked(int index);

protected:
    QSize sizeHint() const override;

private slots:
    void onItemActivated(QListWidgetItem *item);
    void onCurrentRowChanged(int row);
    void setThumbnailIcon(int row, const QImage &image);

private:
    void cancelPendingLoads();
    static QImage makeThumbnail(const QString &path, int maxSize);

    static constexpr int kThumbSize = 96;
    static constexpr int kBarHeight = 112;

    // Generation counter so stale async results are ignored after setFiles()
    std::atomic<quint64> m_generation{0};
};

#endif // THUMBNAILBAR_H
