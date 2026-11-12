#include "FeedsWeb.h"

#include <Calendar.h>
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
constexpr size_t MAX_EVENTS_BYTES = 16 * 1024;

const char PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Feeds and dashboard</title>
<style>
body{font:16px system-ui,sans-serif;max-width:46rem;margin:0 auto;padding:1rem}
nav a{margin-right:1rem}
textarea{width:100%;height:12rem;font:14px ui-monospace,monospace;box-sizing:border-box}
input,select,button{font:inherit;padding:.35rem .5rem;margin:.15rem .25rem .15rem 0}
button{padding:.45rem 1rem}
pre{background:#eee;padding:.5rem;white-space:pre-wrap;min-height:1.2rem}
code{background:#eee;padding:0 .2rem}
fieldset{margin:.6rem 0;border:1px solid #bbb}
.small{font-size:.9rem;color:#444}
</style></head><body>
<nav><a href="/">Home</a><a href="/files">Files</a><a href="/settings">Settings</a></nav>
<h1>Feeds and dashboard</h1>

<h2>Feed sources</h2>
<p class="small">One per line: <code>feed|site|sitemap URL every=DAYS limit=N content=auto|feed|page comments=none|link links=TEXT name=NAME</code></p>
<textarea id="sources" spellcheck="false"></textarea>
<div><button onclick="save('sources')">Save sources</button></div>
<pre id="sources-msg"></pre>

<h2>Dashboard</h2>
<p class="small">Weather: <code>weather LATITUDE LONGITUDE name=Melbourne days=4</code> or <code>weather off</code>.
Calendars (iCal, fetched on each sync): <code>calendar URL</code> or <code>calendar URL region=Victoria</code> (only the public holidays of that region).
A private calendar link is a secret: it is kept on the SD card and nowhere else.</p>
<textarea id="dashboard" spellcheck="false" style="height:8rem"></textarea>
<fieldset><legend>Add a calendar</legend>
<input id="calUrl" size="38" placeholder="iCal link (https://...)">
<input id="calRegion" size="12" placeholder="region (optional)">
<button onclick="addCalendar()">Add</button><br>
<select id="holiday">
<option value="en.australian">Australia</option><option value="en.new_zealand">New Zealand</option>
<option value="en.usa">United States</option><option value="en.uk">United Kingdom</option>
<option value="en.canadian">Canada</option><option value="en.german">Germany</option>
<option value="en.french">France</option><option value="en.italian">Italy</option>
<option value="en.spain">Spain</option><option value="en.brazilian">Brazil</option>
<option value="en.indian">India</option><option value="en.china">China</option>
<option value="en.japanese">Japan</option><option value="en.south_korea">South Korea</option>
<option value="en.singapore">Singapore</option>
</select>
<input id="holRegion" size="16" value="Victoria" placeholder="region, e.g. Victoria">
<button onclick="addHoliday()">Add public holidays</button>
<div class="small">Holiday feeds are Google's public ones. Give a state or region to see only its holidays; leave it empty for the whole country.</div>
</fieldset>
<div><button onclick="save('dashboard')">Save dashboard</button><button onclick="loadStatus()">Check calendar sync</button></div>
<pre id="dashboard-msg"></pre>
<pre id="cal-status"></pre>
<p class="small">Calendars are fetched when the dashboard is refreshed (Extras, Dashboard, Refresh) and on each sync.</p>

<h2>Events</h2>
<p class="small">One per line: <code>2026-11-20 09:00 Dentist</code> (time optional). Use <code>XXXX</code> for the year to repeat every year: <code>XXXX-03-14 Birthday</code>. For a day like "first Tuesday of November" write <code>XXXX-11-TUE1</code> (<code>TUE1</code> to <code>TUE4</code>, or <code>TUEL</code> for the last). Edit freely, then save.</p>
<textarea id="events" spellcheck="false" style="height:10rem"></textarea>
<fieldset><legend>Add an event</legend>
<input id="evDate" type="date"> <input id="evTime" type="time">
<input id="evTitle" size="24" placeholder="Title">
<label><input id="evYearly" type="checkbox"> every year</label>
<button onclick="addEvent()">Add</button>
</fieldset>
<div><button onclick="save('events')">Save events</button></div>
<pre id="events-msg"></pre>

<script>
const $ = (id) => document.getElementById(id);
async function load(name){
  const r = await fetch('/api/feeds/file?name='+name);
  $(name).value = r.ok ? await r.text() : '';
}
async function save(name){
  const msg = $(name+'-msg');
  msg.textContent = 'Saving...';
  try{
    const r = await fetch('/api/feeds/file?name='+name,{method:'POST',headers:{'Content-Type':'text/plain'},body:$(name).value});
    msg.textContent = await r.text();
  }catch(e){ msg.textContent = 'Failed: '+e; }
}
function addLine(id, line){
  const t = $(id);
  if (t.value && !t.value.endsWith('\n')) t.value += '\n';
  t.value += line + '\n';
}
function addCalendar(){
  const url = $('calUrl').value.trim();
  if (!/^https?:\/\//.test(url)) { $('dashboard-msg').textContent = 'Paste an https:// iCal link first.'; return; }
  const region = $('calRegion').value.trim();
  addLine('dashboard', 'calendar ' + url + (region ? ' region=' + (/\s/.test(region) ? '"' + region + '"' : region) : ''));
  $('calUrl').value = '';
  $('dashboard-msg').textContent = 'Added below. Press Save dashboard.';
}
function addHoliday(){
  const id = $('holiday').value;
  const region = $('holRegion').value.trim();
  const url = 'https://calendar.google.com/calendar/ical/' + id + '%23holiday%40group.v.calendar.google.com/public/basic.ics';
  addLine('dashboard', 'calendar ' + url + (region ? ' region=' + (/\s/.test(region) ? '"' + region + '"' : region) : ''));
  $('dashboard-msg').textContent = 'Added below. Press Save dashboard.';
}
function addEvent(){
  const d = $('evDate').value, title = $('evTitle').value.trim();
  if (!d || !title) { $('events-msg').textContent = 'Pick a date and give a title.'; return; }
  const when = $('evYearly').checked ? 'XXXX-' + d.slice(5) : d;
  const time = $('evTime').value;
  addLine('events', when + (time ? ' ' + time : '') + ' ' + title);
  $('evTitle').value = '';
  $('events-msg').textContent = 'Added below. Press Save events.';
}
async function loadStatus(){ const r = await fetch('/api/feeds/file?name=status'); $('cal-status').textContent = r.ok ? await r.text() : ''; }
load('sources'); load('dashboard'); load('events'); loadStatus();
</script></body></html>
)HTML";

}  // namespace

void sendFeedsPage(WebServer& server) { server.send(200, "text/html", PAGE); }

void handleFeedsFile(WebServer& server) {
  const String name = server.arg("name");
  if (name == "status") {  // what the last sync did with each calendar
    const auto config = feeds::loadDashboardConfig();
    const auto status = feeds::loadCalendarStatus();
    std::string text;
    for (size_t i = 0; i < config.calendars.size(); ++i) {
      const std::string& url = config.calendars[i].url;
      const size_t start = url.find("//");
      const size_t end = url.find('/', start == std::string::npos ? 0 : start + 2);
      text += "Calendar " + std::to_string(i + 1) + " (" +
              url.substr(start == std::string::npos ? 0 : start + 2,
                         end == std::string::npos ? std::string::npos : end - start - 2) +
              "): ";
      if (i >= status.size() || status[i].when == 0) {
        text += "not synced yet. Press Refresh on the dashboard, or sync.";
      } else if (!status[i].ok) {
        text += "FAILED, " + status[i].detail;
      } else {
        text += "synced, " + std::to_string(status[i].events) + " events";
      }
      text += "\n";
    }
    if (text.empty()) text = "No calendars added.";
    server.send(200, "text/plain", text.c_str());
    return;
  }
  const bool sources = name == "sources";
  const bool events = name == "events";
  if (!sources && !events && name != "dashboard") {
    server.send(400, "text/plain", "name must be sources, dashboard or events");
    return;
  }
  const char* path = sources ? feeds::SOURCES_PATH : events ? feeds::EVENTS_PATH : feeds::DASHBOARD_PATH;
  const size_t limit = sources ? MAX_SOURCES_BYTES : events ? MAX_EVENTS_BYTES : MAX_DASHBOARD_BYTES;

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
  } else if (events) {
    feed::EventList all(0, 100000, 1);  // only here to collect the lines that cannot be read
    feed::parseManualEvents(body.c_str(), all, &errors);
  } else {
    errors = feed::parseDashboard(body.c_str()).errors;
  }
  for (const auto& e : errors) reply += "\n! " + e;
  LOG_INF("FEEDWEB", "Wrote %s (%u bytes)", path, static_cast<unsigned>(body.length()));
  server.send(200, "text/plain", reply.c_str());
}
