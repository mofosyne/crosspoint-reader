#include "LinkExtractor.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace feed {
namespace {

bool ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

bool startsWithNoCase(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && ieq(s.substr(0, prefix.size()), prefix);
}

// "https://host:8080/a/b?x" -> scheme "https", authority "host:8080", path "/a/b", query "?x"
struct Parts {
  std::string scheme, authority, path, query;
  bool ok = false;
};

Parts split(std::string_view url) {
  Parts p;
  const size_t colon = url.find("://");
  if (colon == std::string_view::npos || colon == 0) return p;
  p.scheme.assign(url.substr(0, colon));
  std::transform(p.scheme.begin(), p.scheme.end(), p.scheme.begin(), [](unsigned char c) { return tolower(c); });
  if (p.scheme != "http" && p.scheme != "https") return p;
  std::string_view rest = url.substr(colon + 3);
  const size_t end = rest.find_first_of("/?#");
  p.authority.assign(rest.substr(0, end));
  std::transform(p.authority.begin(), p.authority.end(), p.authority.begin(), [](unsigned char c) { return tolower(c); });
  if (p.authority.empty()) return p;
  if (end != std::string_view::npos) {
    rest = rest.substr(end);
    const size_t hash = rest.find('#');
    if (hash != std::string_view::npos) rest = rest.substr(0, hash);
    const size_t q = rest.find('?');
    p.path.assign(rest.substr(0, q));
    if (q != std::string_view::npos) p.query.assign(rest.substr(q));
  }
  if (p.path.empty()) p.path = "/";
  p.ok = true;
  return p;
}

std::string removeDotSegments(const std::string& path) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= path.size()) {
    const size_t slash = path.find('/', i);
    const std::string seg = path.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
    if (seg == "..") {
      if (!out.empty() && !(out.size() == 1 && out[0].empty())) out.pop_back();
    } else if (seg != ".") {
      out.push_back(seg);
    }
    if (slash == std::string::npos) break;
    i = slash + 1;
  }
  std::string joined;
  for (size_t k = 0; k < out.size(); ++k) {
    if (k) joined += '/';
    joined += out[k];
  }
  if (joined.empty() || joined[0] != '/') joined.insert(joined.begin(), '/');
  // a trailing "." or ".." leaves a directory
  if ((path.size() >= 2 && path.compare(path.size() - 2, 2, "/.") == 0) ||
      (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0)) {
    if (joined.back() != '/') joined += '/';
  }
  return joined;
}

std::string trimmed(std::string_view s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return std::string(s.substr(a, b - a));
}

// Value of attribute `name` inside tag text [tag, end): quoted or bare.
std::string attribute(std::string_view tag, std::string_view name) {
  size_t pos = 0;
  while (pos < tag.size() && !isspace(static_cast<unsigned char>(tag[pos]))) ++pos;  // the tag name
  while (pos < tag.size()) {
    while (pos < tag.size() && (isspace(static_cast<unsigned char>(tag[pos])) || tag[pos] == '/')) ++pos;
    const size_t nameStart = pos;
    while (pos < tag.size() && !isspace(static_cast<unsigned char>(tag[pos])) && tag[pos] != '=' && tag[pos] != '/') ++pos;
    const std::string_view attrName = tag.substr(nameStart, pos - nameStart);
    while (pos < tag.size() && isspace(static_cast<unsigned char>(tag[pos]))) ++pos;
    if (pos >= tag.size() || tag[pos] != '=') continue;
    ++pos;
    while (pos < tag.size() && isspace(static_cast<unsigned char>(tag[pos]))) ++pos;
    size_t valueStart = pos;
    size_t valueEnd;
    if (pos < tag.size() && (tag[pos] == '"' || tag[pos] == '\'')) {
      const char quote = tag[pos++];
      valueStart = pos;
      while (pos < tag.size() && tag[pos] != quote) ++pos;
      valueEnd = pos;
      if (pos < tag.size()) ++pos;
    } else {
      while (pos < tag.size() && !isspace(static_cast<unsigned char>(tag[pos]))) ++pos;
      valueEnd = pos;
    }
    if (ieq(attrName, name)) return std::string(tag.substr(valueStart, valueEnd - valueStart));
  }
  return {};
}

std::string decodeEntities(std::string s) {
  static const struct {
    const char* name;
    const char* value;
  } ENTITIES[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&apos;", "'"}, {"&nbsp;", " "}};
  for (const auto& e : ENTITIES) {
    size_t pos = 0;
    const size_t len = strlen(e.name);
    while ((pos = s.find(e.name, pos)) != std::string::npos) {
      s.replace(pos, len, e.value);
      pos += strlen(e.value);
    }
  }
  return s;
}

std::string collapse(const std::string& s) {
  std::string out;
  bool space = false;
  for (const unsigned char c : s) {
    if (isspace(c)) {
      space = !out.empty();
    } else {
      if (space) out += ' ';
      space = false;
      out += static_cast<char>(c);
    }
  }
  return out;
}

std::string stripTags(std::string_view html) {
  std::string out;
  bool inTag = false;
  for (const char c : html) {
    if (c == '<') inTag = true;
    else if (c == '>') inTag = false;
    else if (!inTag) out += c;
  }
  return collapse(decodeEntities(out));
}

}  // namespace

std::string resolveUrl(std::string_view base, std::string_view ref) {
  ref = std::string_view(ref.data(), ref.size());
  const std::string r = trimmed(ref);
  if (r.empty() || r[0] == '#') return {};
  const size_t hash = r.find('#');
  const std::string noFragment = hash == std::string::npos ? r : r.substr(0, hash);
  if (noFragment.empty()) return {};
  // a scheme: letters then ':' before any '/' or '?'
  const size_t colon = noFragment.find(':');
  if (colon != std::string::npos && colon > 0 && noFragment.find_first_of("/?") > colon) {
    const Parts abs = split(noFragment);
    if (!abs.ok) return {};
    return abs.scheme + "://" + abs.authority + removeDotSegments(abs.path) + abs.query;
  }
  const Parts b = split(base);
  if (!b.ok) return {};
  if (startsWithNoCase(noFragment, "//")) {
    const Parts abs = split(b.scheme + ":" + noFragment);
    if (!abs.ok) return {};
    return abs.scheme + "://" + abs.authority + removeDotSegments(abs.path) + abs.query;
  }
  std::string path;
  std::string query;
  if (noFragment[0] == '/') {
    const size_t q = noFragment.find('?');
    path = noFragment.substr(0, q);
    if (q != std::string::npos) query = noFragment.substr(q);
  } else if (noFragment[0] == '?') {
    path = b.path;
    query = noFragment;
  } else {
    const size_t q = noFragment.find('?');
    const std::string refPath = noFragment.substr(0, q);
    if (q != std::string::npos) query = noFragment.substr(q);
    const size_t lastSlash = b.path.rfind('/');
    path = b.path.substr(0, lastSlash + 1) + refPath;
  }
  return b.scheme + "://" + b.authority + removeDotSegments(path) + query;
}

std::vector<PageLink> extractLinks(std::string_view html, std::string_view baseUrl, std::string_view pattern,
                                   size_t maxLinks) {
  std::vector<PageLink> links;
  const Parts base = split(baseUrl);
  if (!base.ok) return links;
  const bool anyHost = pattern.find("://") != std::string_view::npos;
  size_t pos = 0;
  while (links.size() < maxLinks && (pos = html.find('<', pos)) != std::string_view::npos) {
    if (pos + 2 >= html.size() || (html[pos + 1] != 'a' && html[pos + 1] != 'A') ||
        !isspace(static_cast<unsigned char>(html[pos + 2]))) {
      ++pos;
      continue;
    }
    const size_t tagEnd = html.find('>', pos);
    if (tagEnd == std::string_view::npos) break;
    const std::string href = attribute(html.substr(pos + 1, tagEnd - pos - 1), "href");
    size_t close = html.find("</a", tagEnd);
    if (close == std::string_view::npos) close = std::min(html.size(), tagEnd + 400);
    const std::string text = stripTags(html.substr(tagEnd + 1, close - tagEnd - 1));
    pos = tagEnd + 1;
    const std::string url = resolveUrl(baseUrl, decodeEntities(href));
    if (url.empty()) continue;
    if (!anyHost && split(url).authority != base.authority) continue;
    if (!pattern.empty() && url.find(pattern) == std::string::npos) continue;
    auto same = std::find_if(links.begin(), links.end(), [&](const PageLink& l) { return l.url == url; });
    if (same != links.end()) {
      if (same->text.size() < text.size()) same->text = text;  // keep the most descriptive text
      continue;
    }
    links.push_back({url, text});
  }
  return links;
}

std::string findFeedLink(std::string_view html, std::string_view baseUrl) {
  size_t pos = 0;
  while ((pos = html.find("<link", pos)) != std::string_view::npos) {
    const size_t end = html.find('>', pos);
    if (end == std::string_view::npos) break;
    const std::string_view tag = html.substr(pos + 1, end - pos - 1);
    pos = end;
    if (!ieq(attribute(tag, "rel"), "alternate")) continue;
    const std::string type = attribute(tag, "type");
    if (!startsWithNoCase(type, "application/rss+xml") && !startsWithNoCase(type, "application/atom+xml")) continue;
    const std::string url = resolveUrl(baseUrl, decodeEntities(attribute(tag, "href")));
    if (!url.empty()) return url;
  }
  return {};
}

std::string pageTitle(std::string_view html) {
  size_t open = 0;
  while ((open = html.find('<', open)) != std::string_view::npos) {
    if (open + 6 < html.size() && ieq(html.substr(open, 6), "<title") &&
        (html[open + 6] == '>' || isspace(static_cast<unsigned char>(html[open + 6])))) {
      break;
    }
    ++open;
  }
  if (open == std::string_view::npos) return {};
  const size_t start = html.find('>', open);
  if (start == std::string_view::npos) return {};
  const size_t end = html.find("</", start);
  if (end == std::string_view::npos) return {};
  return stripTags(html.substr(start + 1, end - start - 1));
}

}  // namespace feed
