#include <gtest/gtest.h>

#include "DeckFile.h"
#include "Scheduler.h"

using namespace flashcards;

TEST(DeckFile, LoadsAnAnkiPlainTextExport) {
  const auto deck = parseDeck(
      "#separator:tab\n#html:true\n#tags column:3\n"
      "\xE7\x8C\xAB\tcat<br>(neko)\tjp animals\n"
      "\xE6\xB0\xB4\twater &amp; liquid\n");
  ASSERT_EQ(deck.cards.size(), 2u);
  EXPECT_EQ(deck.cards[0].front, "\xE7\x8C\xAB");
  EXPECT_EQ(deck.cards[0].back, "cat\n(neko)");
  EXPECT_EQ(deck.cards[0].tags, "jp animals");
  EXPECT_EQ(deck.cards[1].back, "water & liquid");
  EXPECT_TRUE(deck.errors.empty());
}

TEST(DeckFile, ReportsBadLinesAndKeepsTheRest) {
  const auto deck = parseDeck("no tab here\nfront\t\nok\tfine\r\n");
  ASSERT_EQ(deck.cards.size(), 1u);
  EXPECT_EQ(deck.cards[0].back, "fine");
  EXPECT_EQ(deck.errors.size(), 2u);
}

TEST(DeckFile, PlainFieldDecodesEntitiesAndTags) {
  EXPECT_EQ(plainField("<b>a</b>&lt;b&gt; &#65;&#x42; first\\nsecond"), "a<b> AB first\nsecond");
  EXPECT_EQ(plainField("one<div>two</div>"), "one\ntwo");
}

TEST(DeckFile, SerializedCardsReadBack) {
  Card c{"word", "line one\nline two\twith tab", "tag"};
  const auto deck = parseDeck(serializeCard(c));
  ASSERT_EQ(deck.cards.size(), 1u);
  EXPECT_EQ(deck.cards[0].back, "line one\nline two with tab");
  EXPECT_EQ(deck.cards[0].tags, "tag");
}

TEST(DeckFile, KeyFollowsTheFrontOnly) {
  EXPECT_EQ(cardKey("cat"), cardKey("cat"));
  EXPECT_NE(cardKey("cat"), cardKey("dog"));
}

TEST(Scheduler, GoodReviewsGrowTheInterval) {
  CardState s;
  s = applyGrade(s, Grade::Good, 100);
  EXPECT_EQ(s.interval, 1);
  EXPECT_EQ(s.due, 101);
  s = applyGrade(s, Grade::Good, 101);
  EXPECT_EQ(s.interval, 3);
  s = applyGrade(s, Grade::Good, 104);
  EXPECT_EQ(s.interval, 8);  // 3 * 2.5 rounded
  EXPECT_EQ(s.reps, 3);
}

TEST(Scheduler, AgainBringsTheCardBackToday) {
  CardState s;
  s = applyGrade(applyGrade(s, Grade::Good, 100), Grade::Good, 101);
  s = applyGrade(s, Grade::Again, 104);
  EXPECT_EQ(s.due, 104);
  EXPECT_EQ(s.lapses, 1);
  EXPECT_EQ(s.ease, 230);
  s = applyGrade(s, Grade::Good, 104);
  EXPECT_EQ(s.interval, 1);
}

TEST(Scheduler, EaseHasAFloorAndEasyIsLonger) {
  CardState s;
  for (int i = 0; i < 20; ++i) s = applyGrade(s, Grade::Again, 1);
  EXPECT_EQ(s.ease, 130);
  CardState a = applyGrade(CardState{}, Grade::Easy, 10);
  EXPECT_EQ(a.interval, 4);
  EXPECT_GT(daysUntil(a, Grade::Easy, 14), daysUntil(a, Grade::Good, 14));
}

TEST(Scheduler, QueueHasDueCardsFirstThenALimitedNumberOfNewOnes) {
  std::vector<CardState> states(5);
  states[1] = applyGrade(CardState{}, Grade::Good, 90);   // due 91
  states[3] = applyGrade(CardState{}, Grade::Good, 95);   // due 96
  states[4] = applyGrade(CardState{}, Grade::Good, 99);   // due 100, not yet due on day 98
  const auto queue = studyQueue(states, 98, 1);
  ASSERT_EQ(queue.size(), 3u);
  EXPECT_EQ(queue[0], 1u);
  EXPECT_EQ(queue[1], 3u);
  EXPECT_EQ(queue[2], 0u);  // the one new card allowed
}

TEST(Scheduler, ProgressRoundTripsByKey) {
  std::vector<CardState> a(2);
  a[0].key = cardKey("x");
  a[1].key = cardKey("y");
  a[1] = applyGrade(applyGrade(a[1], Grade::Good, 50), Grade::Good, 51);
  std::vector<CardState> b(2);
  b[0].key = cardKey("y");  // deck order changed
  b[1].key = cardKey("x");
  applyProgress(serializeProgress(a), b);
  EXPECT_TRUE(b[0].seen);
  EXPECT_EQ(b[0].interval, 3);
  EXPECT_FALSE(b[1].seen);
}
