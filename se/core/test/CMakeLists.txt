# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Isak Adamssvärd

set (test_gcse_core_INCLUDES
  ${CMAKE_SOURCE_DIR}/se/core
  ${CMAKE_BINARY_DIR}/common # for config.h
)
set (test_gcse_core_LIBS gcse-core gnc-engine PkgConfig::GLIB2 gtest)

# The tests go through gcse::Company only, against books in SQLite and XML.
if (WITH_SQL)
  gnc_add_test (test-gcse-core test-gcse-core.cpp
    test_gcse_core_INCLUDES test_gcse_core_LIBS)
endif()
