# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Isak Adamssvärd
#
# GnuCash SE: the command layer, the server, the command line client and the
# web client. Everything below this directory is licensed under the GNU Affero
# General Public License, version 3 or later (see README.md).
#
# The subdirectories are added as they come into being: core (command layer),
# server, cli, web, api, test.

if (WITH_SE_SERVER)
  add_subdirectory (core)
  add_subdirectory (tools)
endif()
