# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Isak Adamssvärd
#
# Build defaults for GnuCash SE.
#
# Included from the top-level CMakeLists.txt before its own option() block.
# An option that is already defined keeps its value, so the defaults set here
# win over the ones inherited from GnuCash, and a value given on the command
# line (-D WITH_GNUCASH=ON) still wins over both.

# GnuCash SE is a server with a web client. The GTK program in gnucash/ is
# kept in the tree, identical to upstream, but is not built.
option (WITH_GNUCASH "Build the GTK program too (GnuCash SE itself only needs the engine)" OFF)

# The server and its tools are developed on Linux first. The engine is still
# built and tested on macOS and Windows; the server follows there with the
# step for local use.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  option (WITH_SE_SERVER "Build the GnuCash SE server and its tools" ON)
else()
  option (WITH_SE_SERVER "Build the GnuCash SE server and its tools" OFF)
endif()
