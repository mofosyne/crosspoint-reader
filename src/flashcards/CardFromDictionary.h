#pragma once

#include <DeckFile.h>

#include <string>

namespace flashcards {

// A card for a dictionary entry: the headword on the front and the plain text of the definition on the back
// (markup and the headword line removed, cut at about 480 bytes at a line or word end).
Card cardFromDefinition(const std::string& headword, const std::string& definition, bool definitionIsHtml);

}  // namespace flashcards
