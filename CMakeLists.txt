cmake_minimum_required(VERSION 3.16)
project(FileIntegrityMonitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_library(filemoncore
    core/FileIntegrityEngine.cpp
    core/FileScanner.cpp
)

target_include_directories(filemoncore PUBLIC core)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql)

set(GUI_SOURCES
    gui/main.cpp
    gui/MainWindow.cpp
    gui/FileMonitor.cpp
    gui/QtHasher.cpp
    gui/ScanWorker.cpp
    gui/Notifier.cpp
)

set(STORAGE_SOURCES
    storage/DatabaseManager.cpp
    storage/QtStorageAdapter.cpp
)

add_executable(file_integrity_monitor ${GUI_SOURCES} ${STORAGE_SOURCES})

target_include_directories(file_integrity_monitor PRIVATE
    gui
    storage
    core
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(file_integrity_monitor PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Gui
    filemoncore
)

