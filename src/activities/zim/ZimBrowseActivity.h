#pragma once

#include <ZimBlobLoader.h>
#include <ZimBrowser.h>
#include <ZimDevice.h>
#include <ZstdClusterDecoder.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ZimArticle.h"
#include "activities/UiListActivity.h"

// Browse and read one ZIM archive (for example an offline Wikipedia dump). A list of titles in
// alphabetical order with a Search row on top; opening an article shows it as plain text in
// the dictionary viewer. PSRAM boards only: a decoded cluster is about 2 MiB.
class ZimBrowseActivity final : public UiListActivity {
 public:
  explicit ZimBrowseActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path);

  void onEnter() override;
  void onExit() override;
#ifdef CROSSPOINT_SERIAL_INPUT
  std::string debugState() override;
  bool debugCommand(const std::string& command) override;
#endif

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return title_.c_str(); }

  void showBusy();
  void startSearch();
  void applySearch(const std::string& query);
  void showNextPage();
  void openArticle(size_t row);
  void openMainPage();
  // Shows a converted article in the viewer; links inside it open through openLinkTarget().
  void showArticle(ZimArticle&& article);
  // Reads and converts the article behind an archive path (a link target). Releases the cluster
  // buffers afterwards, so earlier viewers on the stack keep only their own text.
  bool openLinkTarget(std::string_view path, ZimArticle& out);
  // Converts the article just read by the browser. Releases the cluster buffers.
  bool convertBlob(zim::Blob& blob, std::string headword, ZimArticle& out);
  void runBenchmark();
  // Copies the article HTML into `out` (cut at the size cap). False when the heap is too small.
  bool copyArticleHtml(const zim::Blob& blob, std::string& out) const;
  // Rebuilds rowItems_ from the browser's current page. Call with the render lock held.
  void rebuildRows();
  void setStatus(const char* text);

  std::string path_;
  std::string title_;
  std::string status_;  // shown beside the Search row: last timing or last error
  bool ready_ = false;
  bool hasMainPage_ = false;
  int benchmarkRow_ = -1;
  int mainPageRow_ = -1;
  size_t firstTitleRow_ = 1;  // list index of the first title (after Search and Main page)
  unsigned long openMs_ = 0;  // time to open the archive and load the first page

  zim::SdFileByteSource source_;
  std::unique_ptr<zim::ZstdClusterDecoder> decoder_;
  std::unique_ptr<zim::ZimBlobLoader> loader_;
  std::unique_ptr<zim::ZimBrowser> browser_;
  std::unique_ptr<ZimLinkCollector> collector_;
  std::vector<freeink::ui::ListItem> rowItems_;
};
