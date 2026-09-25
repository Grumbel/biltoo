// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_INCLUDES_H
#define MAINWINDOW_INCLUDES_H

/** Shared includes for MainWindow translation units (ui / session / gallery / core). */

#include "shell/mainwindow.h"
#include "shell/icons.h"
#include "imageview.h"
#include "session/sessiondocument.h"
#include "host/imageloader.h"
#include "shell/thumbnailbar.h"
#include "shell/preferencesdialog.h"
#include "shell/cachepreparedialog.h"
#include "slideshow/slideshowsettingsdialog.h"
#include "session/sessionreorderdialog.h"
#include "shell/metadatapanel.h"
#include "shell/adjustmentspanel.h"
#include "shell/croppanel.h"
#include "display/imagecache.h"
#include "crop/croprecipe.h"
#include "item/batchtargets.h"
#include "shell/layoutpanel.h"
#include "shell/tocpanel.h"
#include "shell/helppanel.h"
#include "host/thumtoocache.h"
#include "content/contentxform.h"
#include "session/sessionappearance.h"
#include "host/pagepath.h"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QClipboard>
#include <QColor>
#include <QColorDialog>
#include <QCollator>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QInputDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QKeySequence>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QMimeData>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <algorithm>

#endif // MAINWINDOW_INCLUDES_H
