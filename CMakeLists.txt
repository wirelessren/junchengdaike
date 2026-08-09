cmake_minimum_required(VERSION 3.21)

project(SubstituteArrangementCpp LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Sql)

set(STATIC ON CACHE BOOL "Build xlnt as static library" FORCE)
set(TESTS OFF CACHE BOOL "" FORCE)
set(SAMPLES OFF CACHE BOOL "" FORCE)
set(BENCHMARKS OFF CACHE BOOL "" FORCE)
set(PYTHON OFF CACHE BOOL "" FORCE)
set(XLNT_CXX_LANG 17 CACHE STRING "" FORCE)

add_subdirectory(third_party/xlnt)

find_path(FREEXL_INCLUDE_DIR freexl.h)
find_library(FREEXL_LIBRARY NAMES freexl)

if(NOT FREEXL_INCLUDE_DIR OR NOT FREEXL_LIBRARY)
    message(FATAL_ERROR "FreeXL not found. Please install the native FreeXL development package for the active toolchain.")
endif()

set(COMMON_SOURCES
    src/datastore.cpp
    src/coreutils.cpp
    src/excelhelper.cpp
)

set(COMMON_HEADERS
    src/datastore.h
    src/coreutils.h
    src/excelhelper.h
    src/types.h
)

set(APP_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/style.cpp
    ${COMMON_SOURCES}
)

set(APP_HEADERS
    src/mainwindow.h
    src/style.h
    src/uimetrics.h
    ui/main_window_shell.ui
    ui/edit_tab.ui
    ui/free_tab.ui
    ui/task_tab.ui
    ui/about_tab.ui
    ui/settings_tab.ui
    ${COMMON_HEADERS}
)

set(APP_TARGET SubstituteArrangementCpp)
set(APP_OUTPUT_NAME "均程代课管理")

add_executable(${APP_TARGET} WIN32
    ${APP_SOURCES}
    ${APP_HEADERS}
    resources/app.qrc
    resources/app.rc
)

set_target_properties(${APP_TARGET} PROPERTIES
    OUTPUT_NAME "${APP_OUTPUT_NAME}"
)

target_include_directories(${APP_TARGET} PRIVATE
    src
    ${FREEXL_INCLUDE_DIR}
)

target_link_libraries(${APP_TARGET} PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    xlnt
    ${FREEXL_LIBRARY}
)

if(MSVC)
    target_compile_options(${APP_TARGET} PRIVATE /W4 /utf-8)
else()
    target_compile_options(${APP_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(SubstituteSmokeCheck
    tools/smoke_check.cpp
    ${COMMON_SOURCES}
    ${COMMON_HEADERS}
)

target_include_directories(SubstituteSmokeCheck PRIVATE
    src
    ${FREEXL_INCLUDE_DIR}
)

target_link_libraries(SubstituteSmokeCheck PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Sql
    xlnt
    ${FREEXL_LIBRARY}
)

if(MSVC)
    target_compile_options(SubstituteSmokeCheck PRIVATE /W4 /utf-8)
else()
    target_compile_options(SubstituteSmokeCheck PRIVATE -Wall -Wextra -Wpedantic)
endif()
