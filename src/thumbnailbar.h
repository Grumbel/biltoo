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

    /**
     * Pixel size of the square thumbnail icon. Also adjusts preferred height
     * (icon + label). When raised above the last decode size, thumbnails are
     * reloaded asynchronously at the new resolution.
     */
    void setThumbSize(int pixels);
    int thumbSize() const { return m_thumbSize; }

    /** Preferred bar height for a given thumb icon size (icon + label + padding). */
    static int heightForThumbSize(int thumbSize);
    /** Thumb icon size that fits in a bar of the given height. */
    static int thumbSizeForHeight(int height);

    static constexpr int kDefaultThumbSize = 96;
    static constexpr int kMinThumbSize = 48;
    static constexpr int kMaxThumbSize = 256;

signals:
    void indexActivated(int index);
    /** Ctrl/Shift+click in classic mode: request adding this image to the workspace. */
    void indexAddToWorkspace(int index);
    /** Emitted after a toggle (or bulk change) of workspace membership selection. */
    void workspaceSelectionChanged();

protected:
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onItemActivated(QListWidgetItem *item);
    void onCurrentRowChanged(int row);
    void setThumbnailIcon(int row, const QImage &image);

private:
    void cancelPendingLoads();
    void applyThumbMetrics();
    void scheduleThumbnailLoads();
    static QImage makeThumbnail(const QString &path, int maxSize);

    // Generation counter so stale async results are ignored after setFiles()
    std::atomic<quint64> m_generation{0};
    bool m_workspaceMode = false;
    int m_thumbSize = kDefaultThumbSize;
    int m_decodedSize = 0;
    QStringList m_files;
};

#endif // THUMBNAILBAR_H
