#include "FlashcardsWeb.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WebServer.h>

#include <string>

#include "flashcards/DeckStore.h"

namespace {

constexpr size_t MAX_DECK_TEXT = 24 * 1024;

const char PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Flashcards</title>
<style>
body{font:16px system-ui,sans-serif;max-width:46rem;margin:0 auto;padding:1rem}
nav a{margin-right:1rem}
textarea{width:100%;height:14rem;font:14px ui-monospace,monospace;box-sizing:border-box}
input,select,button,textarea{font:inherit}
input,select{padding:.35rem .5rem;margin:.15rem .25rem .15rem 0;box-sizing:border-box}
button{padding:.45rem 1rem;margin:.15rem .25rem .15rem 0}
pre{background:#eee;padding:.5rem;white-space:pre-wrap;min-height:1.2rem}
code{background:#eee;padding:0 .2rem}
fieldset{margin:.6rem 0;border:1px solid #bbb}
label{display:block;margin:.3rem 0}
label input{display:block;width:100%}
.small{font-size:.9rem;color:#444}
</style></head><body>
<nav><a href="/">Home</a><a href="/files">Files</a><a href="/feeds">Feeds</a></nav>
<h1>Flashcards</h1>

<fieldset><legend>Deck</legend>
<select id="deck" onchange="loadDeck()"></select>
<input id="newDeck" placeholder="or a new deck name">
<span class="small">Cards go in the one you pick, or in the new one if you type a name.</span>
</fieldset>

<fieldset><legend>Add a card</legend>
<label>Word<input id="front" autocomplete="off"></label>
<label>Reading (optional, for example kana or pinyin)<input id="reading" autocomplete="off"></label>
<label>Meaning<input id="back" autocomplete="off"></label>
<label>Example sentence (optional)<input id="context" autocomplete="off"></label>
<button onclick="addCard()">Add card</button>
<pre id="add-msg"></pre>
</fieldset>

<h2>Edit the deck</h2>
<p class="small">One card per line, separated by tabs: <code>word&nbsp;&nbsp;meaning</code>, or <code>word&nbsp;&nbsp;reading&nbsp;&nbsp;meaning&nbsp;&nbsp;example</code>.
An Anki "Notes in Plain Text" export loads as it is. Decks over 24 KB: put <code>cards.tsv</code> in <code>/cards/&lt;deck&gt;/</code> with the file manager.
Editing keeps your progress on the cards whose word did not change.</p>
<textarea id="text" spellcheck="false" wrap="off"></textarea>
<div><button onclick="saveDeck()">Save deck</button></div>
<pre id="deck-msg"></pre>

<script>
const $ = (id) => document.getElementById(id);
function deckName(){ return $('newDeck').value.trim() || $('deck').value; }
async function loadDecks(selectName){
  const r = await fetch('/api/flashcards/decks');
  const decks = r.ok ? await r.json() : [];
  $('deck').innerHTML = '';
  for (const d of decks) { const o = document.createElement('option'); o.value = o.textContent = d.name; $('deck').appendChild(o); }
  if (!decks.length) { const o = document.createElement('option'); o.value = 'words'; o.textContent = 'words (new)'; $('deck').appendChild(o); }
  if (selectName) $('deck').value = selectName;
  loadDeck();
}
async function loadDeck(){
  const r = await fetch('/api/flashcards/deck?name=' + encodeURIComponent($('deck').value));
  $('text').value = r.ok ? await r.text() : '';
  $('deck-msg').textContent = r.status === 413 ? 'This deck is too big to edit here. Use the file manager.' : '';
}
async function saveDeck(){
  const name = deckName();
  $('deck-msg').textContent = 'Saving...';
  const r = await fetch('/api/flashcards/deck?name=' + encodeURIComponent(name), {method:'POST', headers:{'Content-Type':'text/plain'}, body:$('text').value});
  $('deck-msg').textContent = await r.text();
  if (r.ok && $('newDeck').value.trim()) { $('newDeck').value = ''; loadDecks(name); }
}
async function addCard(){
  const body = {deck:deckName(), front:$('front').value.trim(), reading:$('reading').value.trim(), back:$('back').value.trim(), context:$('context').value.trim()};
  if (!body.front || !body.back) { $('add-msg').textContent = 'A card needs a word and a meaning.'; return; }
  const r = await fetch('/api/flashcards/card', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)});
  $('add-msg').textContent = await r.text();
  if (r.ok) { for (const id of ['front','reading','back','context']) $(id).value = ''; $('front').focus(); const n = body.deck; $('newDeck').value = ''; loadDecks(n); }
}
loadDecks();
</script></body></html>
)HTML";

}  // namespace

void sendFlashcardsPage(WebServer& server) { server.send(200, "text/html", PAGE); }

void handleFlashcardDecks(WebServer& server) {
  std::string json = "[";
  bool first = true;
  for (const auto& deck : flashcards::listDecks()) {
    HalFile f = Storage.open((deck.folder + "/cards.tsv").c_str());
    const size_t bytes = f ? f.fileSize() : 0;
    if (f) f.close();
    if (!first) json += ',';
    first = false;
    json += "{\"name\":\"" + deck.name + "\",\"bytes\":" + std::to_string(bytes) + "}";  // names are letters, digits, - _ space
  }
  json += "]";
  server.send(200, "application/json", json.c_str());
}

void handleFlashcardDeck(WebServer& server) {
  const std::string name = flashcards::cleanDeckName(server.arg("name").c_str());
  if (name.empty()) {
    server.send(400, "text/plain", "Give the deck a name (letters, digits, space, - and _).");
    return;
  }
  const std::string folder = std::string(flashcards::CARDS_ROOT) + "/" + name;
  const std::string path = folder + "/cards.tsv";
  if (server.method() == HTTP_GET) {
    if (!Storage.exists(path.c_str())) {
      server.send(200, "text/plain", "");
      return;
    }
    std::string text;
    HalFile f = Storage.open(path.c_str());
    if (f && f.fileSize() > MAX_DECK_TEXT) {
      f.close();
      server.send(413, "text/plain", "Too big to edit here");
      return;
    }
    if (f) f.close();
    if (!Storage.readFileToString("CARDSWEB", path, MAX_DECK_TEXT, text)) text.clear();
    server.send(200, "text/plain", text.c_str());
    return;
  }
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body");
    return;
  }
  const String body = server.arg("plain");
  if (body.length() > MAX_DECK_TEXT) {
    server.send(413, "text/plain", "Too long for this page. Use the file manager.");
    return;
  }
  Storage.ensureDirectoryExists(flashcards::CARDS_ROOT);
  Storage.ensureDirectoryExists(folder.c_str());
  if (!Storage.writeFile(path.c_str(), body)) {
    server.send(500, "text/plain", "Could not write the deck");
    return;
  }
  const auto parsed = flashcards::parseDeck(body.c_str());
  std::string reply = "Saved. " + std::to_string(parsed.cards.size()) + " cards.";
  for (size_t i = 0; i < parsed.errors.size() && i < 5; ++i) reply += "\n! " + parsed.errors[i];
  LOG_INF("CARDSWEB", "Wrote %s (%u bytes)", path.c_str(), static_cast<unsigned>(body.length()));
  server.send(200, "text/plain", reply.c_str());
}

void handleFlashcardCard(WebServer& server) {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  flashcards::Card card;
  card.front = flashcards::plainField(doc["front"] | "");
  card.reading = flashcards::plainField(doc["reading"] | "");
  card.back = flashcards::plainField(doc["back"] | "");
  card.context = flashcards::plainField(doc["context"] | "");
  const std::string deck = flashcards::cleanDeckName(doc["deck"] | flashcards::DEFAULT_DECK);
  if (deck.empty() || card.front.empty() || card.back.empty()) {
    server.send(400, "text/plain", "A card needs a deck, a word and a meaning.");
    return;
  }
  if (!flashcards::appendCard(deck, card)) {
    server.send(500, "text/plain", "Could not write the card");
    return;
  }
  server.send(200, "text/plain", "Added \"" + String(card.front.c_str()) + "\" to " + String(deck.c_str()) + ".");
}
