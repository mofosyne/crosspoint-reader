# The firmware's vendored zstd decoder, built with the firmware's flags (lib/zstd/library.json),
# so these tests run the code that ships. System libzstd is not used here.
file(GLOB ZIM_ZSTD_SOURCES
  ${REPO_ROOT}/lib/zstd/common/*.c
  ${REPO_ROOT}/lib/zstd/decompress/*.c
)
add_library(vendored_zstd STATIC ${ZIM_ZSTD_SOURCES})
target_include_directories(vendored_zstd PUBLIC ${REPO_ROOT}/lib/zstd)
target_compile_definitions(vendored_zstd PRIVATE
  ZSTD_LEGACY_SUPPORT=0 ZSTD_DISABLE_ASM=1 ZSTD_STRIP_ERROR_STRINGS=1 ZSTD_NO_TRACE=1 ZSTD_MULTITHREAD=0)
target_compile_options(vendored_zstd PRIVATE -w)  # third-party code: not ours to warn about

add_executable(ZimZstdTest
  ZstdClusterDecoderTest.cpp
  ${REPO_ROOT}/lib/ZimZstd/ZstdClusterDecoder.cpp
)
target_include_directories(ZimZstdTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../zim_reader/stubs
  ${REPO_ROOT}/lib/ZimReader
  ${REPO_ROOT}/lib/ZimZstd
  ${REPO_ROOT}/freeink-sdk/libs/book/ContentProtection/include
)
target_link_libraries(ZimZstdTest PRIVATE vendored_zstd crosspoint_test_common GTest::gtest_main)

gtest_discover_tests(ZimZstdTest)
