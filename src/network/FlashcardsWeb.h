#pragma once

class WebServer;

// Web page for the flashcards, at /flashcards.
//   GET  /api/flashcards/decks               [{"name":"words","bytes":1234}, ...]
//   GET  /api/flashcards/deck?name=words     the deck's cards.tsv text (decks up to 24 KB; larger ones go
//                                            through the file manager, in /cards/<deck>/cards.tsv)
//   POST /api/flashcards/deck?name=words     replaces the deck text (creates the deck)
//   POST /api/flashcards/card                JSON {"deck","front","reading","back","context"}: adds one card
void sendFlashcardsPage(WebServer& server);
void handleFlashcardDecks(WebServer& server);
void handleFlashcardDeck(WebServer& server);
void handleFlashcardCard(WebServer& server);
