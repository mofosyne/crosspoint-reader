add_executable(FeedReaderTest
  FeedReaderTest.cpp
  ${REPO_ROOT}/lib/FeedReader/FeedDate.cpp
  ${REPO_ROOT}/lib/FeedReader/FeedParser.cpp
  ${REPO_ROOT}/lib/FeedReader/LinkExtractor.cpp
  ${REPO_ROOT}/lib/FeedReader/SourcesFile.cpp
  ${REPO_ROOT}/lib/FeedReader/SeenSet.cpp
  ${REPO_ROOT}/lib/FeedReader/CachedArticle.cpp
  ${REPO_ROOT}/lib/FeedReader/ContentExtract.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(FeedReaderTest PRIVATE
  ${REPO_ROOT}/lib/FeedReader
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(FeedReaderTest PRIVATE
  XML_CONTEXT_BYTES=0
  XML_GE=0
)

target_link_libraries(FeedReaderTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FeedReaderTest)
