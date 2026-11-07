#pragma once

#include <CachedArticle.h>
#include <FeedParser.h>
#include <HalMemory.h>
#include <SourcesFile.h>

#include <string>
#include <vector>

#include "FeedStore.h"

namespace feeds {

// One sync run, advanced a step at a time so the UI stays responsive between network calls.
// A step is one source check (fetch the feed, sitemap or index page and save the items it carries)
// or one page fetch for an item whose text the source did not provide. Wi-Fi must already be up.
class FeedSync {
 public:
  struct Status {
    std::string headline;  // "Checking Hacker News" / "Reading <title>"
    std::string detail;    // last problem or note
    size_t sourcesDone = 0;
    size_t sourcesTotal = 0;
    size_t saved = 0;   // articles written this run
    size_t failed = 0;  // items that could not be fetched or saved (retried next sync)
    bool finished = false;
  };

  explicit FeedSync(bool checkAll) : checkAll_(checkAll) {}

  // Reads the sources file and picks the sources that are due (all, with checkAll). False with
  // `error` set when there is nothing to do.
  bool begin(std::string& error);
  // Does one unit of work. False once the run is over.
  bool step();
  void cancel() { status_.finished = true; }
  const Status& status() const { return status_; }

 private:
  struct Pending {
    std::string url;
    std::string title;
    std::string commentsUrl;
    int64_t published = 0;
  };

  void startSource(size_t index);
  void finishSource();
  void checkSource();
  void checkFeed(const feed::Source& source);
  void checkIndexPage(const feed::Source& source);
  void checkSitemap(const feed::Source& source, const std::string& url, int depth);
  bool fetchToBuffer(const std::string& url, std::string_view& body);
  bool saveItem(const feed::Source& source, const Pending& item, std::string_view html, bool fragment);
  bool appendHnComments(const Pending& item, char* work, size_t& used, std::vector<feed::CachedArticle::Link>& links);
  void appendCommentFeed(const std::string& feedUrl, const Pending& item, char* work, size_t& used,
                         std::vector<feed::CachedArticle::Link>& links);
  void readPending();

  bool checkAll_;
  Status status_;
  std::vector<feed::Source> due_;
  size_t current_ = 0;
  bool sourceOpen_ = false;
  bool sourceChecked_ = false;
  SourceState state_;
  std::vector<Pending> pending_;
  size_t pendingAt_ = 0;
  int64_t now_ = 0;

  HalMemory::PsramBuffer fetchBuf_;  // page bodies
  size_t fetchCap_ = 0;
  HalMemory::PsramBuffer workBuf_;  // the article text being built
  size_t workCap_ = 0;
};

}  // namespace feeds
