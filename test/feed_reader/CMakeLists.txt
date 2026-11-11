add_executable(FeedReaderTest
  FeedReaderTest.cpp
  ${REPO_ROOT}/lib/FeedReader/FeedDate.cpp
  ${REPO_ROOT}/lib/FeedReader/FeedParser.cpp
  ${REPO_ROOT}/lib/FeedReader/LinkExtractor.cpp
  ${REPO_ROOT}/lib/FeedReader/SourcesFile.cpp
  ${REPO_ROOT}/lib/FeedReader/SeenSet.cpp
  ${REPO_ROOT}/lib/FeedReader/CachedArticle.cpp
  ${REPO_ROOT}/lib/FeedReader/ContentExtract.cpp
  ${REPO_ROOT}/lib/FeedReader/HnComments.cpp
  ${REPO_ROOT}/lib/FeedReader/CommentFeed.cpp
  ${REPO_ROOT}/lib/FeedReader/Weather.cpp
  ${REPO_ROOT}/lib/FeedReader/Calendar.cpp
  ${REPO_ROOT}/lib/FeedReader/DashboardFile.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(FeedReaderTest PRIVATE
  ${REPO_ROOT}/lib/FeedReader
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
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
