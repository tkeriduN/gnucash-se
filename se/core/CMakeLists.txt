# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Isak Adamssvärd
#
# gcse-core: the command layer. The only code that calls the accounting
# engine; everything above it sees gcse-core.hpp alone.
# Design: docs/design/07-kommandolager.md.

add_library (gcse-core STATIC gcse-core.cpp gcse-core.hpp)
set_target_properties (gcse-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories (gcse-core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE
    ${CMAKE_SOURCE_DIR}/libgnucash/se
    ${CMAKE_SOURCE_DIR}/libgnucash/engine
    ${CMAKE_BINARY_DIR}/common # for config.h
    ${CMAKE_SOURCE_DIR}/common # for platform.h
)
target_link_libraries (gcse-core PRIVATE gnc-engine PkgConfig::GLIB2)

add_subdirectory (test)
