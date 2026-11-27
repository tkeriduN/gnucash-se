# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Isak Adamssvärd
#
# Tools for the development of GnuCash SE. They are built but not installed.

# gcse-bench: generate a book of a given size and measure the engine with it.
add_executable (gcse-bench gcse-bench.cpp)
target_include_directories (gcse-bench PRIVATE
  ${CMAKE_SOURCE_DIR}/libgnucash/se
  ${CMAKE_SOURCE_DIR}/libgnucash/engine
  ${CMAKE_BINARY_DIR}/common # for config.h
  ${CMAKE_SOURCE_DIR}/common # for platform.h
)
target_link_libraries (gcse-bench PRIVATE gnc-engine PkgConfig::GLIB2)

# A small book in SQLite, generated and measured: the tool must keep working
# as the engine changes. The backends are found through the build tree.
if (WITH_SQL)
  add_test (NAME test-gcse-bench COMMAND gcse-bench selftest)
  set_tests_properties (test-gcse-bench PROPERTIES
    ENVIRONMENT "GNC_UNINSTALLED=YES;GNC_BUILDDIR=${CMAKE_BINARY_DIR}")
  add_dependencies (check gcse-bench)
endif()
