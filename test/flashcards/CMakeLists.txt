add_executable(FlashcardsTest
  FlashcardsTest.cpp
  ${REPO_ROOT}/lib/Flashcards/DeckFile.cpp
  ${REPO_ROOT}/lib/Flashcards/Scheduler.cpp
)

target_include_directories(FlashcardsTest PRIVATE ${REPO_ROOT}/lib/Flashcards)

target_link_libraries(FlashcardsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FlashcardsTest)
