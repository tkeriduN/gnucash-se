# GnuCash SE: lock date and voucher numbering.
# Design: docs/design/01-las-och-numrering.md
#
# The sources are compiled into gnc-engine (see ../engine/CMakeLists.txt),
# so that the engine itself enforces the locks for every program that
# uses it.

add_subdirectory(test)

set_local_dist(se_DIST_local CMakeLists.txt gnc-se.cpp gnc-se.h gnc-se.hpp)
set(se_DIST ${se_DIST_local} ${test_se_DIST} PARENT_SCOPE)
