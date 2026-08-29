# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(qimgview VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(qimgview
  src/main.cpp
  src/mainwindow.cpp
  src/mainwindow.h
  src/imageview.cpp
  src/imageview.h
  src/imageitem.cpp
  src/imageitem.h
  src/thumbnailbar.cpp
  src/thumbnailbar.h
)

target_link_libraries(qimgview PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)

install(TARGETS qimgview DESTINATION bin)
