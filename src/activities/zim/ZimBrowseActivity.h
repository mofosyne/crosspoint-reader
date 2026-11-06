#pragma once

#include <ZimBlobLoader.h>
#include <ZimBrowser.h>
#include <ZimDevice.h>
#include <ZstdClusterDecoder.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Browse and read one ZIM archive (for example an offline Wikipedia dump). A list of titles in
// alphabetical order with a Search row on top; opening an article shows it as plain text in
// the dictionary viewer. PSRAM boards only: a decoded cluster is about 2 MiB.
class ZimBrowseActivity final : public UiListActivity {
 public:
  explicit ZimBrowseActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path);

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return title_.c_str(); }

  void startSearch();
  void applySearch(const std::string& query);
  void showNextPage();
  void openArticle(size_t row);
  // Rebuilds rowItems_ from the browser's current page. Call with the render lock held.
  void rebuildRows();
  void setStatus(const char* text);

  std::string path_;
  std::string title_;
  std::string status_;  // shown beside the Search row: last timing or last error
  bool ready_ = false;

  zim::SdFileByteSource source_;
  std::unique_ptr<zim::ZstdClusterDecoder> decoder_;
  std::unique_ptr<zim::ZimBlobLoader> loader_;
  std::unique_ptr<zim::ZimBrowser> browser_;
  std::vector<freeink::ui::ListItem> rowItems_;
};
