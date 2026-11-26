# GnuCash SE: lock date and voucher numbering.
# Design: docs/design/01-las-och-numrering.md

add_subdirectory(test)

set(gnc_se_SOURCES gnc-se.cpp)
set(gnc_se_HEADERS gnc-se.hpp)

# Add dependency on config.h
set_source_files_properties (${gnc_se_SOURCES} PROPERTIES OBJECT_DEPENDS ${CONFIG_H})

add_library(gnc-se ${gnc_se_SOURCES} ${gnc_se_HEADERS})

target_link_libraries(gnc-se
    gnc-engine
    PkgConfig::GLIB2)

target_compile_definitions(gnc-se PRIVATE -DG_LOG_DOMAIN=\"gnc.se\")

target_include_directories(gnc-se
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_BINARY_DIR}/common)

if (APPLE)
  set_target_properties (gnc-se PROPERTIES INSTALL_NAME_DIR "${CMAKE_INSTALL_FULL_LIBDIR}")
endif()

if (COVERAGE)
  add_coverage_target(gnc-se)
endif()

install(TARGETS gnc-se
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# No headers to install

set_local_dist(se_DIST_local CMakeLists.txt ${gnc_se_SOURCES} ${gnc_se_HEADERS})
set(se_DIST ${se_DIST_local} ${test_se_DIST} PARENT_SCOPE)
