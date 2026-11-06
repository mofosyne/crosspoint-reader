#include "ZimBrowseActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/reader/DictionaryDefinitionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/Dictionary.h"
#include "util/HtmlToPlainText.h"

namespace fui = freeink::ui;

namespace {
constexpr size_t MAX_SEARCH_LENGTH = 64;
// Articles beyond this are cut (a full "nopic" article can reach 600 KB).
constexpr size_t MAX_ARTICLE_BYTES = 512 * 1024;
// Spare heap kept after copying the article, for the viewer's line tables.
constexpr size_t VIEWER_HEADROOM = 96 * 1024;
}  // namespace

ZimBrowseActivity::ZimBrowseActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : UiListActivity("ZimBrowse", renderer, mappedInput), path_(std::move(path)) {
  title_ = FsHelpers::getFileNameWithoutExtension(path_);
}

void ZimBrowseActivity::onEnter() {
  UiListActivity::onEnter();
  rowItems_.reserve(zim::ZimBrowser::DEFAULT_PAGE_SIZE + 2);  // Search row + page + Next row

  decoder_ = makeUniqueNoThrow<zim::ZstdClusterDecoder>(zim::psramMemory());
  loader_ = makeUniqueNoThrow<zim::ZimBlobLoader>(zim::psramMemory());
  browser_ = makeUniqueNoThrow<zim::ZimBrowser>();
  if (!decoder_ || !decoder_->valid() || !loader_ || !browser_) {
    LOG_ERR("ZIM", "OOM: browse objects");
    return;
  }
  const unsigned long t0 = millis();
  if (!source_.open(path_)) {
    LOG_ERR("ZIM", "Cannot open %s", path_.c_str());
    return;
  }
  zim::Status status = browser_->open(source_, *decoder_, *loader_);
  if (status == zim::Status::Ok) status = browser_->search("");
  if (status != zim::Status::Ok) {
    LOG_ERR("ZIM", "Open failed st=%d", static_cast<int>(status));
    return;
  }
  LOG_INF("ZIM", "Opened %s: ns=%c, first page in %lu ms", path_.c_str(), browser_->contentNamespace(), millis() - t0);
  ready_ = true;
  RenderLock lock(*this);
  rebuildRows();
}

void ZimBrowseActivity::onExit() {
  Activity::onExit();
  browser_.reset();
  loader_.reset();
  decoder_.reset();
  source_.close();
}

void ZimBrowseActivity::setStatus(const char* text) { status_ = text; }

void ZimBrowseActivity::rebuildRows() {
  rowItems_.clear();
  if (!ready_) return;
  fui::ListItem search;
  search.label = tr(STR_SEARCH);
  search.value = status_.empty() ? nullptr : status_.c_str();
  search.actionValue = 0;
  rowItems_.push_back(search);
  for (size_t i = 0; i < browser_->count(); ++i) {
    fui::ListItem item;
    item.label = browser_->title(i).c_str();
    item.actionValue = static_cast<int16_t>(rowItems_.size());
    rowItems_.push_back(item);
  }
  if (browser_->hasMore()) {
    fui::ListItem next;
    next.label = tr(STR_NEXT_PAGE);
    next.actionValue = static_cast<int16_t>(rowItems_.size());
    rowItems_.push_back(next);
  }
}

void ZimBrowseActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (!ready_) {
    screen.centeredText(tr(STR_ERROR_GENERAL_FAILURE), screen.theme().bodyText);
    return;
  }
  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void ZimBrowseActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (!ready_ || index < 0 || index >= listCount()) return;
  if (index == 0) {
    startSearch();
    return;
  }
  const size_t row = static_cast<size_t>(index) - 1;
  if (row < browser_->count()) {
    openArticle(row);
  } else {
    showNextPage();
  }
}

void ZimBrowseActivity::startSearch() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH), "", MAX_SEARCH_LENGTH,
                                                           InputType::Text);
  if (!keyboard) {
    LOG_ERR("ZIM", "OOM: search keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    applySearch(std::get<KeyboardResult>(result.data).text);
  });
}

void ZimBrowseActivity::applySearch(const std::string& query) {
  const unsigned long t0 = millis();
  RenderLock lock(*this);
  const zim::Status status = browser_->search(query);
  char buf[48];
  if (status != zim::Status::Ok) {
    setStatus(tr(STR_ERROR_GENERAL_FAILURE));
  } else if (browser_->count() == 0) {
    setStatus(tr(STR_NO_ENTRIES));
  } else {
    snprintf(buf, sizeof(buf), "%lu ms", millis() - t0);
    setStatus(buf);
  }
  LOG_INF("ZIM", "Search '%s': st=%d rows=%u in %lu ms", query.c_str(), static_cast<int>(status),
          static_cast<unsigned>(browser_->count()), millis() - t0);
  rebuildRows();
  nav.reset();
  lock.unlock();
  requestUpdate();
}

void ZimBrowseActivity::showNextPage() {
  const unsigned long t0 = millis();
  RenderLock lock(*this);
  const zim::Status status = browser_->nextPage();
  char buf[48];
  snprintf(buf, sizeof(buf), "%lu ms", millis() - t0);
  setStatus(status == zim::Status::Ok ? buf : tr(STR_ERROR_GENERAL_FAILURE));
  rebuildRows();
  nav.reset();
  lock.unlock();
  requestUpdate();
}

void ZimBrowseActivity::openArticle(const size_t row) {
  const unsigned long t0 = millis();
  zim::Blob blob;
  std::string headword;
  zim::Status status = browser_->readArticle(row, blob, headword);
  const unsigned long readMs = millis() - t0;

  std::string text;
  if (status == zim::Status::Ok) {
    const size_t bytes = std::min(blob.size, MAX_ARTICLE_BYTES);
    if (HalMemory::getDefaultHeap().largestBlockBytes < bytes + VIEWER_HEADROOM) {
      LOG_ERR("ZIM", "Not enough heap for a %u byte article", static_cast<unsigned>(bytes));
      status = zim::Status::OutOfMemory;
    } else {
      // htmlToPlainText reuses this buffer, so the article costs one allocation.
      text.assign(reinterpret_cast<const char*>(blob.data), bytes);
    }
  }
  loader_->release();  // the 2-3 MiB buffers are not needed while the article is on screen
  if (status != zim::Status::Ok) {
    LOG_ERR("ZIM", "Article row %u failed st=%d after %lu ms", static_cast<unsigned>(row), static_cast<int>(status),
            readMs);
    RenderLock lock(*this);
    setStatus(tr(STR_ERROR_GENERAL_FAILURE));
    rebuildRows();
    lock.unlock();
    requestUpdate();
    return;
  }

  const size_t htmlBytes = text.size();
  const unsigned long t1 = millis();
  text = htmlToPlainText(std::move(text), DICTIONARY_PLAIN_HEADWORD_MARKER);
  const unsigned long convertMs = millis() - t1;
  LOG_INF("ZIM", "Article '%s': read %lu ms, to text %lu ms, html=%u text=%u", headword.c_str(), readMs, convertMs,
          static_cast<unsigned>(htmlBytes), static_cast<unsigned>(text.size()));

  {
    char buf[48];
    snprintf(buf, sizeof(buf), "%lu ms", millis() - t0);
    RenderLock lock(*this);
    setStatus(buf);
    rebuildRows();
  }
  auto viewer = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, std::move(headword),
                                                                std::move(text), /*htmlDefinition=*/false);
  if (!viewer) {
    LOG_ERR("ZIM", "OOM: article viewer");
    return;
  }
  startActivityForResult(std::move(viewer), [](const ActivityResult&) {});
}
