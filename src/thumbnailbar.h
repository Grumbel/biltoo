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
#include <QTimer>
#include <atomic>

/**
 * Paints filmstrip cells (crop-square or letterbox). See docs/FILMSTRIP_LAYOUT.md.
 * Draws the prepared pixmap from ThumbPixmapRole — never QIcon::pixmap(square),
 * which stretches non-square thumbs.
 */
class ThumbnailDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    static constexpr int kLabelGap = 2;
    /** Logical content size at thumbSize scale (aspect for letterbox). */
    static constexpr int ThumbContentSizeRole = Qt::UserRole + 42;
    /** True once a real decoded thumb is installed (not a placeholder). */
    static constexpr int ThumbLoadedRole = Qt::UserRole + 43;
    /** Prepared thumb pixmap (correct aspect + DPR). Prefer over DecorationRole. */
    static constexpr int ThumbPixmapRole = Qt::UserRole + 44;
    /** Long edge of installed pixmap (device px). Soft placeholder until >= want. */
    static constexpr int ThumbDecodeEdgeRole = Qt::UserRole + 45;
    /** Cross-axis pad (~thumb/24, clamped). Top/bottom on horizontal bar. */
    int cellPad() const;
    /** Flow-axis pad per side so 2·flowPad + spacing ≈ cellPad between images. */
    int flowPad() const;

    explicit ThumbnailDelegate(int thumbSize, QObject *parent = nullptr);

    void setThumbSize(int pixels);
    int thumbSize() const { return m_thumbSize; }

    void setLabelsVisible(bool on);
    bool labelsVisible() const { return m_labelsVisible; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    /** Cell size for the current thumb size and font (square slot). */
    QSize cellSize(const QFont &font) const;
    /** Letterbox cell: content + cross/flow pads + label (orientation-aware). */
    QSize cellSizeForContent(const QFont &font, QSize contentAspect) const;
    /** Logical letterbox content size (cross-axis = thumbSize) from any aspect. */
    QSize letterboxContentSize(QSize aspect) const;
    /** Provisional content aspect before decode (square — same cross-axis). */
    QSize provisionalContentSize() const;

    /**
     * Logical content size for an item: from ThumbContentSizeRole, else
     * letterbox of ThumbPixmapRole aspect, else provisional. Used by sizeHint
     * and paint so both always agree.
     */
    QSize logicalContentSize(const QModelIndex &index) const;

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
     * When true, center-crop to a square thumbSize cell. When false (default),
     * letterbox: whole image, cross-axis = thumbSize (docs/FILMSTRIP_LAYOUT.md).
     */
    void setCropToSquare(bool on);
    bool cropToSquare() const { return m_cropToSquare; }
    /** Match filmstrip fill to the ImageView/Gallery canvas background. */
    void setStripBackground(const QColor &color);
    /** Rows waiting on pool decode or ladder (for status / busy chrome). */
    int pendingLoadCount() const;
    bool isRowLoading(int row) const;

    /** Session rows currently on the Workspace canvas (membership badge). */
    void setOnCanvasIndices(const QSet<int> &indices);
    bool isOnCanvas(int row) const { return m_onCanvasIndices.contains(row); }

    void setBarOrientation(Qt::Orientation orientation);
    Qt::Orientation barOrientation() const { return m_orientation; }

    static int extentForThumbSize(int thumbSize);
    static int thumbSizeForExtent(int extent);

    static constexpr int kDefaultThumbSize = 96;
    static constexpr int kMinThumbSize = 48;
    /** Layout max matches highest ladder step (power-of-two). No soft-only clamp. */
    static constexpr int kMaxThumbSize = 1024;

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
    /** Pending filmstrip decode count changed (status bar / indicators). */
    void loadsChanged();

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
    void showEvent(QShowEvent *event) override;
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
    /** Apply native pixel size as letterbox aspect on a row (sizeHint + role). */
    void applyNativeAspect(QListWidgetItem *item, const QSize &native);
    /** Cache-first sizes for all rows; scheduleProbe for misses. */
    void primeGeometryFromCache();

    /** Clear pixmaps + loaded flags (rows stay). Used before full reload. */
    void invalidateThumbPixels();
    /** Recompute ThumbContentSizeRole + sizeHint from aspect at current thumbSize. */
    void refreshAllItemGeometry();
    /** Capture / restore: keep the viewport-centre image centred across thumbSize changes. */
    struct ScrollAnchor {
        int row = -1;
        int offsetInViewport = 0; // unused (centre restore)
        bool valid = false;
    };
    ScrollAnchor captureScrollAnchor() const;
    void restoreScrollAnchor(const ScrollAnchor &anchor);
    /** Debounced soft reload after thumbSize grow (avoid wipe on every drag pixel). */
    void scheduleDebouncedThumbReload();
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
    /** Decode ladder edge for sharp icons (≥ thumb×DPR, ≤ gallery soft max). Layout ignores this. */
    int filmstripDecodeEdge() const;

    std::atomic<quint64> m_generation{0};
    /** Row indices that already have a pool job (or finished) this generation. */
    QSet<int> m_thumbLoadScheduled;
    /** Soft miss waiting on thumtoo ladderReady (row index). */
    QSet<int> m_thumbAwaitLadder;
    /** Soft-miss settled for this generation — do not re-queue (CPU spin). */
    QSet<int> m_thumbFailed;
    QTimer *m_layoutRefreshTimer = nullptr;
    QTimer *m_thumbSizeReloadTimer = nullptr;
    void scheduleLayoutRefresh();
    bool m_multiSelect = false;
    int m_selectionAnchor = -1;
    bool m_centeringGuard = false;
    bool m_labelsVisible = true;
    bool m_cropToSquare = false; // letterbox; crop is opt-in
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
