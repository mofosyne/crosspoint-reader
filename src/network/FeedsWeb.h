#pragma once

class WebServer;

// Web editor for the feed reader's two text files, /feeds/sources.txt and /feeds/dashboard.txt.
//   GET  /feeds                          the page
//   GET  /api/feeds/file?name=sources    (or dashboard, events) the file's text (empty when it does not exist)
//   POST /api/feeds/file?name=dashboard  replaces the file with the request body; the answer lists problems
void sendFeedsPage(WebServer& server);
void handleFeedsFile(WebServer& server);
