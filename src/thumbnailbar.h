// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THUMBNAILBAR_H
#define THUMBNAILBAR_H

#include <QListWidget>
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
    explicit ThumbnailBar(QWidget *parent = nullptr);
    ~ThumbnailBar() override;

    void setFiles(const QStringList &files);
    void setCurrentIndex(int index);
    int currentIndex() const;

    void setWorkspaceMode(bool on);
    bool workspaceMode() const { return m_workspaceMode; }

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
    void workspaceSelectionChanged();
    void removeIndicesRequested(const QList<int> &indices);

protected:
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
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
    void applyThumbMetrics();
    void applyOrientation();
    void scheduleThumbnailLoads();
    void requestRemoveSelection();
    void startFileDrag(const QList<QListWidgetItem *> &items);
    int labelBandHeight() const;
    int thumbSizeFromBarExtent(int extent) const;
    static QImage makeThumbnail(const QString &path, int maxSize);

    std::atomic<quint64> m_generation{0};
    bool m_workspaceMode = false;
    int m_selectionAnchor = -1;
    bool m_labelsVisible = true;
    int m_thumbSize = kDefaultThumbSize;
    int m_decodedSize = 0;
    Qt::Orientation m_orientation = Qt::Horizontal;
    QStringList m_files;
    ThumbnailDelegate *m_delegate = nullptr;

    QPoint m_pressPos;
    QListWidgetItem *m_pressItem = nullptr;
    bool m_pressActive = false;
    bool m_dragStarted = false;
};

#endif // THUMBNAILBAR_H
