// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THUMBNAILBAR_H
#define THUMBNAILBAR_H

#include <QHash>
#include <QSet>
#include <QImage>
#include "imageview_types.h"
#include <QListWidget>
#include <QVector>
#include <QMimeData>
#include <QPoint>
#include <QStringList>
#include <QStyledItemDelegate>
#include <atomic>

/**
 * Paints a square thumbnail with a single-line caption directly under it.
 * Avoids QListWidget IconMode style padding and keeps label colour correct
 * when the item is selected.
 */
class ThumbnailDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    static constexpr int kLabelGap = 2;
    static constexpr int kCellPadX = 2;

    explicit ThumbnailDelegate(int thumbSize, QObject *parent = nullptr);

    void setThumbSize(int pixels);
    int thumbSize() const { return m_thumbSize; }

    void setLabelsVisible(bool on);
    bool labelsVisible() const { return m_labelsVisible; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    /** Cell size for the current thumb size and font. */
    QSize cellSize(const QFont &font) const;

    /** Caption band under the icon (0 when labels hidden). */
    int labelBandHeight(const QFont &font) const;
    static int labelBandHeightForFont(const QFont &font);

private:
    int m_thumbSize = 96;
    bool m_labelsVisible = true;
};

class ThumbnailBar : public QListWidget
{
    Q_OBJECT

public:
    /** Qt::UserRole / RolePath = path (QString). RoleSessionId = SessionImageId. */
    enum ItemDataRole {
        RolePath = Qt::UserRole,
        RoleSessionId = Qt::UserRole + 1
    };

    explicit ThumbnailBar(QWidget *parent = nullptr);
    ~ThumbnailBar() override;

    void setFiles(const QStringList &files);
    /**
     * Atomic session list update: install ids before rebuilding rows so
     * scheduleThumbnailLoads never pairs a row with a stale session id
     * (wrong crop/appearance on drag-drop / Duplicate / History).
     */
    void setSession(const QStringList &files, const QVector<SessionImageId> &ids);
    void setCurrentIndex(int index);
    int currentIndex() const;

    /**
     * Session appearance override (e.g. after crop): keep a full image for
     * this path and rebuild the cell icon. Survives thumb-size reloads
     * until setFiles() clears the strip.
     */
    void setSessionImageOverride(const QString &path, const QImage &image);
    void setSessionImageOverride(SessionImageId sessionId, const QString &path,
                                 const QImage &image);
    void setSessionIds(const QVector<SessionImageId> &ids);

    /** Multi-select session paths for Workspace canvas membership (not app ViewMode). */
    void setMultiSelectEnabled(bool on);
    bool multiSelectEnabled() const { return m_multiSelect; }
    /** @deprecated Use setMultiSelectEnabled. */
    void setWorkspaceMode(bool on) { setMultiSelectEnabled(on); }
    /** @deprecated Use multiSelectEnabled. */
    bool workspaceMode() const { return multiSelectEnabled(); }

    QList<int> selectedIndices() const;
    void setSelectedIndices(const QList<int> &indices);
    /** Select every thumbnail (workspace: put all on canvas). */
    void selectAllThumbs();
    void selectNoneThumbs();
    void invertThumbSelection();

    void setThumbSize(int pixels);
    int thumbSize() const { return m_thumbSize; }

    void setLabelsVisible(bool on);
    bool labelsVisible() const { return m_labelsVisible; }

    /**
     * When true (default), center-crop to a square cell. When false, fit the
     * full image with letterboxing so aspect ratio is visible.
     */
    void setCropToSquare(bool on);
    bool cropToSquare() const { return m_cropToSquare; }

    /** Session rows currently on the Workspace canvas (membership badge). */
    void setOnCanvasIndices(const QSet<int> &indices);
    bool isOnCanvas(int row) const { return m_onCanvasIndices.contains(row); }

    void setBarOrientation(Qt::Orientation orientation);
    Qt::Orientation barOrientation() const { return m_orientation; }

    static int extentForThumbSize(int thumbSize);
    static int thumbSizeForExtent(int extent);

    static constexpr int kDefaultThumbSize = 96;
    static constexpr int kMinThumbSize = 48;
    static constexpr int kMaxThumbSize = 256;

    static int heightForThumbSize(int thumbSize) { return extentForThumbSize(thumbSize); }
    static int thumbSizeForHeight(int height) { return thumbSizeForExtent(height); }

signals:
    void indexActivated(int index);
    void indexAddToWorkspace(int index);
    /** Multi-select changed (selection only — does not drive canvas membership). */
    void workspaceSelectionChanged();
    /** Double-click: toggle this session index on/off the Workspace canvas. */
    void canvasMembershipToggled(int index);
    void removeIndicesRequested(const QList<int> &indices);

protected:
    void changeEvent(QEvent *event) override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QListWidgetItem *> items) const;
    Qt::DropActions supportedDragActions() const;

private slots:
    void onItemActivated(QListWidgetItem *item);
    void onCurrentRowChanged(int row);
    void setThumbnailIcon(int row, const QImage &image);

private:
    void cancelPendingLoads();
    void clearPressState();
    void applyThumbMetrics();
    void applyOrientation();
    void scheduleThumbnailLoads();
    /** Queue decode jobs for rows near the viewport / current index only. */
    void scheduleVisibleThumbnailLoads();
    void requestRemoveSelection();
    void startFileDrag(const QList<QListWidgetItem *> &items);
    /** Centre icons when the row/column is shorter than the viewport. */
    void updateCenteringMargins();
    int labelBandHeight() const;
    int thumbSizeFromBarExtent(int extent) const;
    QImage makeThumbnail(const QString &path, int maxSize) const;
    QImage prepareThumbnailFromImage(const QImage &image, int maxSize) const;
    /** Physical pixel edge for decode/prepare (logical thumb × devicePixelRatio). */
    int thumbDecodePixels() const;

    std::atomic<quint64> m_generation{0};
    /** Row indices that already have a pool job (or finished) this generation. */
    QSet<int> m_thumbLoadScheduled;
    /** Soft miss waiting on thumtoo ladderReady (row index). */
    QSet<int> m_thumbAwaitLadder;
    bool m_multiSelect = false;
    int m_selectionAnchor = -1;
    bool m_centeringGuard = false;
    bool m_labelsVisible = true;
    bool m_cropToSquare = true;
    int m_thumbSize = kDefaultThumbSize;
    int m_decodedSize = 0;
    Qt::Orientation m_orientation = Qt::Horizontal;
    QStringList m_files;
    /** Session-only images (crop, …) preferred over on-disk decode for thumbs. */
    QHash<QString, QImage> m_sessionImageOverrides;
    QHash<SessionImageId, QImage> m_sessionIdImageOverrides;
    QVector<SessionImageId> m_sessionIds;
    QSet<int> m_onCanvasIndices;
    ThumbnailDelegate *m_delegate = nullptr;

    QPoint m_pressPos;
    QListWidgetItem *m_pressItem = nullptr;
    bool m_pressActive = false;
    bool m_dragStarted = false;
    Qt::KeyboardModifiers m_pressModifiers;
};

#endif // THUMBNAILBAR_H
