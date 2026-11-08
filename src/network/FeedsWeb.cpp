#include "FeedsWeb.h"

#include <DashboardFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SourcesFile.h>
#include <WebServer.h>

#include <string>

#include "feeds/FeedStore.h"

namespace {

constexpr size_t MAX_SOURCES_BYTES = 32 * 1024;
constexpr size_t MAX_DASHBOARD_BYTES = 4 * 1024;

const char PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Feeds</title>
<style>
body{font:16px system-ui,sans-serif;max-width:46rem;margin:0 auto;padding:1rem}
textarea{width:100%;height:14rem;font:14px ui-monospace,monospace;box-sizing:border-box}
button{padding:.5rem 1rem;margin:.4rem .5rem .4rem 0}
pre{background:#eee;padding:.5rem;white-space:pre-wrap;min-height:1.2rem}
code{background:#eee;padding:0 .2rem}
</style></head><body>
<h1>Feeds</h1>
<p><a href="/">Home</a> &middot; <a href="/settings">Settings</a></p>

<h2>Sources</h2>
<p>One per line: <code>feed|site|sitemap URL every=DAYS limit=N content=auto|feed|page comments=none|link links=TEXT name=NAME</code></p>
<textarea id="sources" spellcheck="false"></textarea>
<div><button onclick="save('sources')">Save sources</button></div>
<pre id="sources-msg"></pre>

<h2>Dashboard</h2>
<p>Weather: <code>weather LATITUDE LONGITUDE name=Melbourne days=4</code> or <code>weather off</code></p>
<textarea id="dashboard" spellcheck="false" style="height:6rem"></textarea>
<div><button onclick="save('dashboard')">Save dashboard</button></div>
<pre id="dashboard-msg"></pre>

<script>
async function load(name){
  const r = await fetch('/api/feeds/file?name='+name);
  document.getElementById(name).value = r.ok ? await r.text() : '';
}
async function save(name){
  const msg = document.getElementById(name+'-msg');
  msg.textContent = 'Saving...';
  try{
    const r = await fetch('/api/feeds/file?name='+name,{method:'POST',headers:{'Content-Type':'text/plain'},body:document.getElementById(name).value});
    msg.textContent = await r.text();
  }catch(e){ msg.textContent = 'Failed: '+e; }
}
load('sources'); load('dashboard');
</script></body></html>
)HTML";

}  // namespace

void sendFeedsPage(WebServer& server) { server.send(200, "text/html", PAGE); }

void handleFeedsFile(WebServer& server) {
  const String name = server.arg("name");
  const bool sources = name == "sources";
  if (!sources && name != "dashboard") {
    server.send(400, "text/plain", "name must be sources or dashboard");
    return;
  }
  const char* path = sources ? feeds::SOURCES_PATH : feeds::DASHBOARD_PATH;
  const size_t limit = sources ? MAX_SOURCES_BYTES : MAX_DASHBOARD_BYTES;

  if (server.method() == HTTP_GET) {
    std::string text;
    if (!Storage.readFileToString("FEEDWEB", path, limit, text)) text.clear();
    server.send(200, "text/plain", text.c_str());
    return;
  }
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body");
    return;
  }
  const String body = server.arg("plain");
  if (body.length() > limit) {
    server.send(413, "text/plain", "Too long");
    return;
  }
  Storage.ensureDirectoryExists("/feeds");
  if (!Storage.writeFile(path, body)) {
    server.send(500, "text/plain", "Could not write the file");
    return;
  }
  std::string reply = "Saved.";
  std::vector<std::string> errors;
  if (sources) {
    const auto parsed = feed::parseSources(body.c_str());
    reply += " " + std::to_string(parsed.sources.size()) + " sources.";
    errors = parsed.errors;
  } else {
    errors = feed::parseDashboard(body.c_str()).errors;
  }
  for (const auto& e : errors) reply += "\n! " + e;
  LOG_INF("FEEDWEB", "Wrote %s (%u bytes)", path, static_cast<unsigned>(body.length()));
  server.send(200, "text/plain", reply.c_str());
}
