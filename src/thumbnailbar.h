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

    /**
     * When true, plain click toggles multi-selection of thumbnails (workspace
     * membership). When false, classic single-selection navigates the session.
     */
    void setWorkspaceMode(bool on);
    bool workspaceMode() const { return m_workspaceMode; }

    /** Selected row indices (sorted ascending). */
    QList<int> selectedIndices() const;
    /** Programmatically set which rows are selected without emitting signals. */
    void setSelectedIndices(const QList<int> &indices);

signals:
    void indexActivated(int index);
    /** Ctrl/Shift+click in classic mode: request adding this image to the workspace. */
    void indexAddToWorkspace(int index);
    /** Emitted after a toggle (or bulk change) of workspace membership selection. */
    void workspaceSelectionChanged();

protected:
    QSize sizeHint() const override;
    void mousePressEvent(QMouseEvent *event) override;

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
    bool m_workspaceMode = false;
};

#endif // THUMBNAILBAR_H
