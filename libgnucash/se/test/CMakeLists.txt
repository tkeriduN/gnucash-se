set(gtest_se_INCLUDES
  ${CMAKE_SOURCE_DIR}/libgnucash/se
  ${CMAKE_SOURCE_DIR}/libgnucash/engine
  ${CMAKE_BINARY_DIR}/common # for config.h
  ${CMAKE_SOURCE_DIR}/common # for platform.h
)

set(gtest_se_LIBS
  gnc-engine
  PkgConfig::GLIB2
  gtest)

gnc_add_test(test-gnc-se gtest-gnc-se.cpp gtest_se_INCLUDES gtest_se_LIBS)

set_dist_list(test_se_DIST CMakeLists.txt gtest-gnc-se.cpp)
