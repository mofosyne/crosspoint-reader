find_library(ZSTD_LIBRARY zstd)
find_path(ZSTD_INCLUDE_DIR zstd.h)

add_executable(ZimReaderTest
  ZimReaderTest.cpp
  ${REPO_ROOT}/lib/ZimReader/ZimReader.cpp
  ${REPO_ROOT}/lib/ZimZstd/ZimBlobLoader.cpp
)

target_include_directories(ZimReaderTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/ZimReader
  ${REPO_ROOT}/lib/ZimZstd
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/freeink-sdk/libs/book/ContentProtection/include
)

# libzstd is optional: without it the zstd cluster tests are skipped.
if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
  target_compile_definitions(ZimReaderTest PRIVATE ZIM_TEST_HAVE_ZSTD=1)
  target_include_directories(ZimReaderTest PRIVATE ${ZSTD_INCLUDE_DIR})
  target_link_libraries(ZimReaderTest PRIVATE ${ZSTD_LIBRARY})
endif()

target_link_libraries(ZimReaderTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ZimReaderTest)
