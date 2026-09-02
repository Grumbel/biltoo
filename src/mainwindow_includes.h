// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_INCLUDES_H
#define MAINWINDOW_INCLUDES_H

/** Shared includes for MainWindow translation units (ui / session / gallery / core). */

#include "mainwindow.h"
#include "icons.h"
#include "imageview.h"
#include "sessiondocument.h"
#include "imageloader.h"
#include "thumbnailbar.h"
#include "preferencesdialog.h"
#include "metadatapanel.h"
#include "adjustmentspanel.h"
#include "layoutpanel.h"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QCollator>
#include <QCursor>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
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
