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
     * Pixel size of the square thumbnail icon. Also adjusts preferred extent
     * (icon + label). When raised above the last decode size, thumbnails are
     * reloaded asynchronously at the new resolution.
     */
    void setThumbSize(int pixels);
    int thumbSize() const { return m_thumbSize; }

    /**
     * Layout orientation of the strip:
     * - Qt::Horizontal: bottom bar, icons flow left-to-right
     * - Qt::Vertical: left bar, icons flow top-to-bottom
     */
    void setBarOrientation(Qt::Orientation orientation);
    Qt::Orientation barOrientation() const { return m_orientation; }

    /** Preferred bar extent along the thin axis for a given thumb icon size. */
    static int extentForThumbSize(int thumbSize);
    /** Thumb icon size that fits in a bar of the given thin-axis extent. */
    static int thumbSizeForExtent(int extent);

    static constexpr int kDefaultThumbSize = 96;
    static constexpr int kMinThumbSize = 48;
    static constexpr int kMaxThumbSize = 256;

    // Back-compat aliases used by MainWindow settings
    static int heightForThumbSize(int thumbSize) { return extentForThumbSize(thumbSize); }
    static int thumbSizeForHeight(int height) { return thumbSizeForExtent(height); }

signals:
    void indexActivated(int index);
    /** Ctrl/Shift+click in classic mode: request adding this image to the workspace. */
    void indexAddToWorkspace(int index);
    /** Emitted after a toggle (or bulk change) of workspace membership selection. */
    void workspaceSelectionChanged();
    /** Request removing these session indices (Delete key or context menu). */
    void removeIndicesRequested(const QList<int> &indices);

protected:
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onItemActivated(QListWidgetItem *item);
    void onCurrentRowChanged(int row);
    void setThumbnailIcon(int row, const QImage &image);

private:
    void cancelPendingLoads();
    void applyThumbMetrics();
    void applyOrientation();
    void scheduleThumbnailLoads();
    void requestRemoveSelection();
    static QImage makeThumbnail(const QString &path, int maxSize);

    // Generation counter so stale async results are ignored after setFiles()
    std::atomic<quint64> m_generation{0};
    bool m_workspaceMode = false;
    int m_thumbSize = kDefaultThumbSize;
    int m_decodedSize = 0;
    Qt::Orientation m_orientation = Qt::Horizontal;
    QStringList m_files;
};

#endif // THUMBNAILBAR_H
