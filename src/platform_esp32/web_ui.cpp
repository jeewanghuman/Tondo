#include "platform_esp32/web_ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#if __has_include(<esp_core_dump.h>)
#include <esp_core_dump.h>
#include <esp_partition.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

#include "core/log_ring.h"
#include "core/remote.h"
#include "core/shot_csv.h"
#include "core/sound.h"
#include "core/system.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/config_backup.h"
#include "platform_esp32/network.h"
#include "platform_esp32/webapp_dist.h"
#include "version.h"

namespace platform {

namespace {

const char* mode_name(core::ShotMode m) { return core::shot_mode_name(m); }

// Row batcher for the chunked JSON/CSV bodies.
//
// WebServer::sendContent costs THREE socket writes per call in chunked mode
// (chunk-size line, body, CRLF) plus a malloc/free for the size line — so a
// row-at-a-time loop turned a 600-sample shot into ~1800 tiny TCP segments.
// Each one is its own select()+send() and its own packet across the SDIO link
// to the C6: measured 7-8 s to serve a 7 KB CSV (~1 KB/s), and the buffer churn
// starved the BLE transport outright ("vhci_drv: Tx ... malloc failed").
//
// Accumulating ~1 KB per flush cuts that to a few dozen writes with identical
// bytes on the wire. Rows longer than the buffer pass straight through, so
// correctness never depends on the size chosen here.
class RowBatch {
 public:
  explicit RowBatch(WebServer& server) : server_(server) {}
  ~RowBatch() { flush(); }

  void add(const char* row) {
    const size_t n = std::strlen(row);
    if (n == 0) return;
    if (n >= sizeof(buf_)) {  // oversized row: flush order, then pass through
      flush();
      server_.sendContent(row, n);
      return;
    }
    if (used_ + n > sizeof(buf_)) flush();
    std::memcpy(buf_ + used_, row, n);
    used_ += n;
  }

  void flush() {
    if (used_ == 0) return;
    server_.sendContent(buf_, used_);
    used_ = 0;
  }

 private:
  WebServer& server_;
  char buf_[1024];
  size_t used_ = 0;
};

// "shot-20260615-0455" — sortable, locale-free, matches when the shot ran.
void shot_basename(const core::IShotStore& shots, uint32_t id, char* out,
                   size_t n) {
  int64_t unix_time = 0;
  core::ShotSummary page[16];
  for (int offset = 0;;) {
    const int got = shots.list(page, 16, offset);
    if (got == 0) break;
    offset += got;
    bool found = false;
    for (int i = 0; i < got; ++i)
      if (page[i].id == id) {
        unix_time = page[i].unix_time;
        found = true;
        break;
      }
    if (found) break;
  }
  if (unix_time == 0) {
    std::snprintf(out, n, "shot-%06lu", static_cast<unsigned long>(id));
    return;
  }
  const time_t t = static_cast<time_t>(unix_time);
  struct tm tm;
  localtime_r(&t, &tm);
  std::snprintf(out, n, "shot-%04d%02d%02d-%02d%02d", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

const char* link_name(core::Link l) {
  switch (l) {
    case core::Link::NeedsToken: return "needs_token";
    case core::Link::Disconnected: return "disconnected";
    case core::Link::Connecting: return "connecting";
    case core::Link::Connected: return "connected";
    default: return "unconfigured";
  }
}

const char* phase_name(core::ShotPhase p) {
  switch (p) {
    case core::ShotPhase::kBrewing: return "brewing";
    case core::ShotPhase::kSettling: return "settling";
    case core::ShotPhase::kReview: return "review";
    default: return "idle";
  }
}

const char* tf(bool b) { return b ? "true" : "false"; }

// Stream `in` as a JSON string body (no quotes) through the batcher. Bytes
// >= 0x80 pass through untouched, so valid UTF-8 stays valid -- unlike a
// fixed-buffer escape, nothing is ever cut mid-character.
void add_escaped(RowBatch& batch, const char* in) {
  char buf[264];
  size_t n = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p; ++p) {
    if (n > sizeof(buf) - 8) {
      buf[n] = '\0';
      batch.add(buf);
      n = 0;
    }
    const unsigned char c = *p;
    if (c == '"' || c == '\\') {
      buf[n++] = '\\';
      buf[n++] = static_cast<char>(c);
    } else if (c == '\n') {
      buf[n++] = '\\';
      buf[n++] = 'n';
    } else if (c < 0x20) {
      n += static_cast<size_t>(std::snprintf(buf + n, 8, "\\u%04x", c));
    } else {
      buf[n++] = static_cast<char>(c);
    }
  }
  buf[n] = '\0';
  batch.add(buf);
}

// Copy a form field into a fixed note field: line breaks become spaces, and a
// cut lands on a character boundary (never half a UTF-8 sequence).
void copy_field(char* dst, size_t cap, const String& src) {
  const char* p = src.c_str();
  const size_t len = src.length();
  size_t r = 0, w = 0;
  while (r < len) {
    const unsigned char c = static_cast<unsigned char>(p[r]);
    const size_t seq = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : 4;
    if (r + seq > len || w + seq + 1 > cap) break;
    for (size_t i = 0; i < seq; ++i) {
      const char ch = p[r + i];
      dst[w++] = (ch == '\n' || ch == '\r' || ch == '\t') ? ' ' : ch;
    }
    r += seq;
  }
  dst[w] = '\0';
}

constexpr int kPinTries = 5;
constexpr uint32_t kPinLockMs = 60 * 1000;

}  // namespace

void WebUi::begin(TokenSetup& setup, core::IShotStore& shots, core::IClock& clock,
                  const char* name, std::function<WebTheme()> theme) {
  setup_ = &setup;
  shots_ = &shots;
  clock_ = &clock;
  name_ = name;
  theme_ = std::move(theme);
  setup.attach(server_);

  server_.on("/", HTTP_GET, [this]() { handle_root(); });
  server_.on("/save", HTTP_POST, [this]() { setup_->handle_save(); });
  server_.on("/wifi", HTTP_POST, [this]() { setup_->handle_wifi(); });
  server_.on("/api/summary", HTTP_GET, [this]() { handle_summary(); });
  server_.on("/api/shots", HTTP_GET, [this]() { handle_shots(); });
  server_.on("/api/shot.csv", HTTP_GET, [this]() { handle_shot_csv(); });
  server_.on("/log", HTTP_GET, [this]() { handle_log(); });
  server_.on("/api/log", HTTP_GET, [this]() { handle_log(); });
  server_.on("/coredump", HTTP_GET, [this]() { handle_coredump(); });
  // The phone page (see the header for the list).
  server_.on("/api/live", HTTP_GET, [this]() { handle_live(); });
  server_.on("/api/settings", HTTP_GET, [this]() { handle_settings_get(); });
  server_.on("/api/settings", HTTP_POST, [this]() { handle_settings_post(); });
  server_.on("/api/control", HTTP_POST, [this]() { handle_control(); });
  server_.on("/api/pin", HTTP_POST, [this]() { handle_pin(); });
  server_.on("/api/shot/note", HTTP_POST, [this]() { handle_note(); });
  server_.on("/api/shot/delete", HTTP_POST, [this]() { handle_delete(); });
  server_.on("/api/export.csv", HTTP_GET, [this]() { handle_export(); });
  server_.on("/api/export_samples.csv", HTTP_GET, [this]() { handle_export_samples(); });
  server_.on("/api/update/check", HTTP_POST, [this]() { handle_update(false); });
  server_.on("/api/update/install", HTTP_POST, [this]() { handle_update(true); });
  server_.on("/api/backup.txt", HTTP_GET, [this]() { handle_backup(); });
  server_.on("/api/restore", HTTP_POST, [this]() { handle_restore(); });
  server_.on("/manifest.webmanifest", HTTP_GET, [this]() { handle_manifest(); });
  server_.on("/icon-192.png", HTTP_GET, [this]() { send_png(kIcon192, kIcon192Len); });
  server_.on("/icon-512.png", HTTP_GET, [this]() { send_png(kIcon512, kIcon512Len); });
  server_.on("/apple-touch-icon.png", HTTP_GET,
             [this]() { send_png(kAppleTouchIcon, kAppleTouchIconLen); });
  server_.onNotFound([this]() {
    // During a portal session, funnel everything to the setup page (phones
    // probe random URLs); otherwise a plain 404.
    if (setup_ != nullptr && setup_->active()) {
      server_.sendHeader("Location", "/");
      server_.send(302, "text/plain", "");
    } else {
      server_.send(404, "text/plain", "not found");
    }
  });
#if defined(BOARD_DISPLAY_DSI)
  // P4: a dedicated task so serving never stalls LVGL (see the header for
  // why the S3 boards must NOT do this). Checked create — a silent failure
  // here means "web interface simply absent" (seen once on the 4.3C).
  if (xTaskCreatePinnedToCore(task_entry, "web_ui", 8192, this, 2, nullptr,
                              0) != pdPASS) {
    core::logf("WebUi: FAILED to create server task (internal RAM?) — "
               "web interface unavailable\n");
  }
#endif
}

bool WebUi::ensure_bound() {
  // lwip's tcpip thread only exists once something has initialized WiFi;
  // binding earlier asserts ("Invalid mbox").
  if (bound_) return true;
  if (WiFi.getMode() == WIFI_OFF) return false;
  server_.begin();
  bound_ = true;
  core::logf("WebUi: server up on :80\n");
  return true;
}

void WebUi::poll() {
#if !defined(BOARD_DISPLAY_DSI)
  // S3: all serving happens here, on the main loop — LVGL pauses during a
  // request, which keeps the RGB panel's PSRAM bandwidth intact (the panel
  // visibly jitters if we serve concurrently; see the header).
  if (ensure_bound()) server_.handleClient();
#endif
}

void WebUi::task_entry(void* self) { static_cast<WebUi*>(self)->run(); }

void WebUi::run() {
  while (!ensure_bound()) vTaskDelay(pdMS_TO_TICKS(250));
  for (;;) {
    server_.handleClient();
    // handleClient returns immediately when idle; a short sleep keeps this
    // task near-zero-cost between requests.
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void WebUi::handle_root() {
  if (setup_ != nullptr && setup_->active()) {
    setup_->handle_root();  // the portal page (token or WiFi form)
    return;
  }
  // The app is ~100KB (gzipped at build time; the browser inflates it). Handing
  // all of it to send_P in one call bottoms out INTERNAL ram: that becomes a
  // single send() of 100KB, and since the source is memory-mapped flash the
  // stack has to stage the whole thing into DMA-capable internal buffers on its
  // way to the C6 over SDIO. Measured on the 5": internal free 121KB -> 15KB,
  // largest block 82KB -> 4KB, which is low enough that the DSI dirty sync
  // can't allocate its GDMA descriptor list and drops to a CPU copy.
  //
  // Slicing bounds what's in flight to kSlice, so payload size stops driving
  // peak ram. setContentLength with a REAL length keeps the WebServer's
  // _chunked flag false, so these appends carry no chunk framing — the bytes on
  // the wire are identical to the single-call version.
  constexpr size_t kSlice = 8 * 1024;
  server_.sendHeader("Content-Encoding", "gzip");
  server_.setContentLength(kWebAppGzLen);
  server_.send(200, "text/html", "");
  for (size_t off = 0; off < kWebAppGzLen; off += kSlice) {
    const size_t n = kWebAppGzLen - off < kSlice ? kWebAppGzLen - off : kSlice;
    server_.sendContent_P(reinterpret_cast<const char*>(kWebAppGz + off), n);
  }
}

void WebUi::handle_log() {
  // Snapshot the whole ring (mutex inside), then send in slices — same
  // peak-RAM reasoning as handle_root. The copy goes to PSRAM where the ring
  // itself lives; boards without PSRAM fall back to the (small) internal ring.
  const size_t cap = core::log_ring().capacity() + 1;
  char* buf = static_cast<char*>(
      heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) buf = static_cast<char*>(malloc(cap));
  if (buf == nullptr) {
    server_.send(503, "text/plain", "log unavailable (no memory)");
    return;
  }
  const size_t n = core::log_ring().snapshot_tail(buf, cap);

  // TRUNCATED BY DEFAULT. Serving runs on the main loop on RGB boards and the
  // display is frozen for the duration -- that freeze is deliberate (see
  // web_ui.h: serving while LVGL renders starves the panel's bounce buffer),
  // so the lever we have is to serve less. 4KB keeps it brief; `?full=1` still
  // gets the whole ring for a real diagnosis. The ring is a FIXED 64KB, so the
  // full response never grows beyond that however long the device has been up
  // (measured ~329 B/min, i.e. the ring holds roughly 3.3 hours).
  constexpr size_t kTailBytes = 4 * 1024;
  const bool full = server_.hasArg("full");
  size_t start = (!full && n > kTailBytes) ? n - kTailBytes : 0;
  if (start > 0) {  // don't begin mid-line
    while (start < n && buf[start - 1] != '\n') ++start;
  }

  char head[160];
  int head_len = 0;
  if (start > 0) {
    head_len = std::snprintf(
        head, sizeof(head),
        "--- truncated: showing the last %u of %u bytes. Full log: /log?full=1 "
        "(slower; the screen freezes while it is served) ---\n",
        static_cast<unsigned>(n - start), static_cast<unsigned>(n));
    if (head_len < 0) head_len = 0;
  }

  constexpr size_t kSlice = 8 * 1024;
  server_.setContentLength(static_cast<size_t>(head_len) + (n - start));
  server_.send(200, "text/plain; charset=utf-8", "");
  if (head_len > 0) server_.sendContent(head, static_cast<size_t>(head_len));
  for (size_t off = start; off < n; off += kSlice) {
    const size_t chunk = n - off < kSlice ? n - off : kSlice;
    server_.sendContent(buf + off, chunk);
  }
  free(buf);
}

void WebUi::handle_coredump() {
  // Remote crash diagnosis: the panic handler writes an ELF coredump to the
  // `coredump` flash partition (enabled in the stock sdkconfig on every
  // board); this serves that image so a user anywhere can download it and
  // send it in — decode with `espcoredump.py info_corefile --core-format raw`
  // against the release's .elf (the debug-symbols archive on the GitHub
  // release).
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH)
  if (server_.hasArg("erase")) {
    const esp_err_t err = esp_core_dump_image_erase();
    server_.send(err == ESP_OK ? 200 : 500, "text/plain",
                 err == ESP_OK ? "coredump erased" : esp_err_to_name(err));
    return;
  }
  size_t addr = 0, size = 0;
  if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0) {
    server_.send(404, "text/plain", "no crash dump stored");
    return;
  }
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  if (part == nullptr || addr < part->address ||
      addr - part->address + size > part->size) {
    server_.send(500, "text/plain", "coredump partition mismatch");
    return;
  }
  const size_t base = addr - part->address;
  constexpr size_t kSlice = 8 * 1024;  // same peak-RAM reasoning as handle_log
  char* buf = static_cast<char*>(
      heap_caps_malloc(kSlice, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) buf = static_cast<char*>(malloc(kSlice));
  if (buf == nullptr) {
    server_.send(503, "text/plain", "coredump unavailable (no memory)");
    return;
  }
  // Name the download after the CRASHED build, not the running one: the dump
  // outlives upgrades, so stamping fw::kVersion here could label a v0.7.0
  // crash as v0.7.1. The dump itself records the crashing app's ELF-SHA
  // fingerprint (first 8 hex chars, per CONFIG_APP_RETRIEVE_LEN_ELF_SHA) —
  // correct by construction; each .elf in the release's debug-symbols
  // archive ships a matching .appsha sidecar to pair them up.
  char sha[APP_ELF_SHA256_SZ] = "unknown";
  auto* sum = static_cast<esp_core_dump_summary_t*>(
      heap_caps_malloc(sizeof(esp_core_dump_summary_t),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (sum == nullptr)
    sum = static_cast<esp_core_dump_summary_t*>(
        malloc(sizeof(esp_core_dump_summary_t)));
  if (sum != nullptr) {
    if (esp_core_dump_get_summary(sum) == ESP_OK)
      std::snprintf(sha, sizeof(sha), "%s",
                    reinterpret_cast<const char*>(sum->app_elf_sha256));
    free(sum);
  }
  char dispo[80];
  std::snprintf(dispo, sizeof(dispo), "attachment; filename=coredump-%s.bin",
                sha);
  server_.sendHeader("Content-Disposition", dispo);
  server_.setContentLength(size);
  server_.send(200, "application/octet-stream", "");
  for (size_t off = 0; off < size; off += kSlice) {
    const size_t n = size - off < kSlice ? size - off : kSlice;
    if (esp_partition_read(part, base + off, buf, n) != ESP_OK) break;
    server_.sendContent(buf, n);
  }
  free(buf);
#else
  server_.send(404, "text/plain", "coredump not supported in this build");
#endif
}

void WebUi::handle_summary() {
  const WebTheme t = theme_ ? theme_() : WebTheme{};
  const core::StorageInfo si = shots_->storage();
  const core::ShotStats st = shots_->stats(clock_->now_unix());
  // coredump: stored crash-dump size in bytes (0 = none). Drives the page
  // header's "Crash dump" link, so it only appears when there's a dump.
  size_t cd_size = 0;
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH)
  {
    size_t cd_addr = 0;
    if (esp_core_dump_image_get(&cd_addr, &cd_size) != ESP_OK) cd_size = 0;
  }
#endif
  char buf[736];
  std::snprintf(
      buf, sizeof(buf),
      "{\"name\":\"%s\",\"version\":\"%s\",\"coredump\":%u,"
      "\"host\":\"%s\",\"notes\":%s,\"remote\":%s,"
      "\"theme\":{\"bg\":%lu,\"rail\":%lu,\"card\":%lu,\"text\":%lu,"
      "\"muted\":%lu,\"accent\":%lu,\"ok\":%lu,\"warn\":%lu,\"alert\":%lu},"
      "\"storage\":{\"total\":%llu,\"free\":%llu,\"full\":%s},"
      "\"stats\":{\"total\":%d,\"life\":%.1f,\"d30\":%.1f,\"since\":%lld}}",
      name_, fw::kVersion, static_cast<unsigned>(cd_size),
      network_ != nullptr ? network_->web_name() : "",
      tf(shots_->supports_notes() && shots_->available()), tf(remote_ != nullptr),
      static_cast<unsigned long>(t.bg),
      static_cast<unsigned long>(t.rail), static_cast<unsigned long>(t.card),
      static_cast<unsigned long>(t.text), static_cast<unsigned long>(t.muted),
      static_cast<unsigned long>(t.accent), static_cast<unsigned long>(t.ok),
      static_cast<unsigned long>(t.warn), static_cast<unsigned long>(t.alert),
      static_cast<unsigned long long>(si.total_bytes),
      static_cast<unsigned long long>(si.free_bytes),
      si.full ? "true" : "false", st.total,
      static_cast<double>(st.acc_lifetime_pct),
      static_cast<double>(st.acc_30d_pct),
      static_cast<long long>(shots_->stats_since()));
  server_.send(200, "application/json", buf);
}

void WebUi::handle_shots() {
  // Chunked: constant memory no matter how long the history is.
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  RowBatch batch(server_);
  batch.add("{\"shots\":[");
  constexpr int kPage = 16;
  core::ShotSummary page[kPage];
  core::ShotNote note;
  int offset = 0;
  bool first = true;
  for (;;) {
    const int n = shots_->list(page, kPage, offset);
    if (n == 0) break;
    offset += n;
    for (int i = 0; i < n; ++i) {
      const core::ShotSummary& s = page[i];
      std::snprintf(row_, sizeof(row_),
                    "%s{\"id\":%lu,\"unix\":%lld,\"duration_ms\":%lu,"
                    "\"target_g\":%.1f,\"final_g\":%.1f,\"avg_gps\":%.2f,"
                    "\"mode\":\"%s\",\"wired\":%s",
                    first ? "" : ",", static_cast<unsigned long>(s.id),
                    static_cast<long long>(s.unix_time),
                    static_cast<unsigned long>(s.duration_ms),
                    static_cast<double>(s.target_g),
                    static_cast<double>(s.final_g),
                    static_cast<double>(s.avg_gps), mode_name(s.mode),
                    s.wired ? "true" : "false");
      batch.add(row_);
      if (shots_->note(s.id, &note)) {
        std::snprintf(row_, sizeof(row_), ",\"rating\":%d,\"bean\":\"", note.rating);
        batch.add(row_);
        add_escaped(batch, note.bean);
        batch.add("\",\"note\":\"");
        add_escaped(batch, note.text);
        batch.add("\"");
      }
      batch.add("}");
      first = false;
    }
  }
  batch.add("]}");
  batch.flush();
  server_.sendContent("");  // end of chunked body
}

void WebUi::handle_shot_csv() {
  const uint32_t id = static_cast<uint32_t>(server_.arg("id").toInt());
  // Regenerated from the record via the shared format helpers — no filesystem
  // coupling, and it works for any store implementation.
  //
  // PSRAM: a ShotRecord is ~7KB and plain `new` would take that from the
  // internal heap, which is the scarce pool here (ui/app.cpp keeps its two
  // staging records in the LVGL pool for the same reason). Falls back to the
  // default heap on boards without PSRAM.
  void* mem = heap_caps_malloc(sizeof(core::ShotRecord),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (mem == nullptr) mem = malloc(sizeof(core::ShotRecord));
  auto* rec = mem != nullptr ? new (mem) core::ShotRecord : nullptr;
  if (rec == nullptr || !shots_->read(id, *rec)) {
    if (rec != nullptr) rec->~ShotRecord();
    free(mem);
    server_.send(404, "text/plain", "no such shot");
    return;
  }
  char base[40], disp[80];
  shot_basename(*shots_, id, base, sizeof(base));
  std::snprintf(disp, sizeof(disp), "attachment; filename=%s.csv", base);
  server_.sendHeader("Content-Disposition", disp);
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "text/csv", "");
  {
    RowBatch batch(server_);
    batch.add(core::kShotSamplesHeader);
    char row[48];
    for (int i = 0; i < rec->n_samples; ++i) {
      core::format_shot_sample_row(row, sizeof(row), rec->samples[i]);
      batch.add(row);
    }
  }  // batch flushes here, before the terminating empty chunk
  server_.sendContent("");
  rec->~ShotRecord();
  free(rec);
}


// --- phone page ----------------------------------------------------------------

void WebUi::send_json(int code, const char* body) {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json", body);
}

// Every change from the phone needs the PIN shown on the device (Settings >
// Tondo > WiFi). Five wrong tries lock the gated routes for a minute, which
// puts all 10,000 PINs about 33 hours of guessing away.
bool WebUi::authorized() {
  if (network_ == nullptr || remote_ == nullptr) {
    send_json(503, "{\"error\":\"remote control unavailable\"}");
    return false;
  }
  const uint32_t now = millis();
  if (pin_locked_until_ms_ != 0 && static_cast<int32_t>(pin_locked_until_ms_ - now) > 0) {
    send_json(429, "{\"error\":\"Too many wrong PINs. Try again in a minute.\"}");
    return false;
  }
  pin_locked_until_ms_ = 0;
  const char* pin = network_->web_pin();
  if (pin[0] != '\0' && server_.arg("pin") == pin) {
    pin_failures_ = 0;
    return true;
  }
  if (++pin_failures_ >= kPinTries) {
    pin_failures_ = 0;
    pin_locked_until_ms_ = now + kPinLockMs;
    if (pin_locked_until_ms_ == 0) pin_locked_until_ms_ = 1;
    core::logf("WebUi: PIN locked for a minute after %d wrong tries\n", kPinTries);
  }
  send_json(403, "{\"error\":\"Wrong PIN\"}");
  return false;
}

void WebUi::handle_pin() {
  if (authorized()) send_json(200, "{\"ok\":true}");
}

void WebUi::handle_live() {
  if (remote_ == nullptr) {
    send_json(503, "{\"error\":\"live view unavailable\"}");
    return;
  }
  const core::RemoteLive l = remote_->live();
  // The running shot: everything after `since` of the series the caller has
  // (`shot`); a different series (a new shot) is sent from the start.
  uint32_t shot_id = 0;
  int total = 0;
  remote_->shot_samples(0, nullptr, 0, &shot_id, &total);
  int from = server_.hasArg("since") ? server_.arg("since").toInt() : 0;
  if (!server_.hasArg("shot") ||
      static_cast<uint32_t>(server_.arg("shot").toInt()) != shot_id || from > total)
    from = 0;

  server_.sendHeader("Cache-Control", "no-store");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  {
    RowBatch batch(server_);
    std::snprintf(
        row_, sizeof(row_),
        "{\"seq\":%lu,\"name\":\"%s\",\"version\":\"%s\",\"f\":%s,"
        "\"link\":\"%s\",\"on\":%s,\"heating\":%s,\"brewing\":%s,"
        "\"brew\":{\"t\":%.1f,\"set\":%.1f},"
        "\"steam\":{\"t\":%.1f,\"set\":%.1f,\"on\":%s,\"level\":%d},"
        "\"scale\":{\"paired\":%s,\"connected\":%s,\"g\":%.1f,\"batt\":%d},",
        static_cast<unsigned long>(l.seq), network_ != nullptr ? network_->web_name() : "",
        fw::kVersion, tf(l.settings[static_cast<int>(core::RemoteSetting::kFahrenheit)] != 0),
        link_name(l.link), tf(l.on), tf(l.heating), tf(l.brewing),
        static_cast<double>(l.brew_c), static_cast<double>(l.brew_set),
        static_cast<double>(l.steam_c), static_cast<double>(l.steam_set), tf(l.steam_on),
        l.steam_level, tf(l.scale_paired), tf(l.scale_connected),
        static_cast<double>(l.weight_g), l.scale_batt);
    batch.add(row_);
    std::snprintf(
        row_, sizeof(row_),
        "\"update\":{\"available\":%s,\"checking\":%s,\"ok\":%s,\"seq\":%d,"
        "\"version\":\"%s\"},"
        "\"shot\":{\"phase\":\"%s\",\"mode\":\"%s\",\"ms\":%lu,\"g\":%.1f,"
        "\"target\":%.1f,\"id\":%lu,\"total\":%d,\"from\":%d,\"pts\":[",
        tf(l.update_available), tf(l.update_checking), tf(l.update_check_ok),
        l.update_check_seq, l.update_version, phase_name(l.phase),
        core::shot_mode_name(l.mode), static_cast<unsigned long>(l.shot_ms),
        static_cast<double>(l.shot_g), static_cast<double>(l.target_g),
        static_cast<unsigned long>(shot_id), total, from);
    batch.add(row_);
    core::ShotSample pts[32];
    bool first = true;
    for (int at = from;;) {
      uint32_t id = 0;
      const int n = remote_->shot_samples(at, pts, 32, &id, nullptr);
      if (n == 0 || id != shot_id) break;
      for (int i = 0; i < n; ++i) {
        char p[40];
        std::snprintf(p, sizeof(p), "%s[%lu,%.1f]", first ? "" : ",",
                      static_cast<unsigned long>(pts[i].t_ms),
                      static_cast<double>(pts[i].weight_g));
        batch.add(p);
        first = false;
      }
      at += n;
    }
    batch.add("]}}");
  }
  server_.sendContent("");
}

void WebUi::handle_settings_get() {
  if (remote_ == nullptr) {
    send_json(503, "{\"error\":\"settings unavailable\"}");
    return;
  }
  const core::RemoteLive l = remote_->live();
  server_.sendHeader("Cache-Control", "no-store");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  {
    RowBatch batch(server_);
    std::snprintf(row_, sizeof(row_),
                  "{\"name\":\"%s\",\"version\":\"%s\",\"ready\":%s,\"can_dim\":%s,"
                  "\"sound\":%s,\"notes\":%s,\"values\":{",
                  network_ != nullptr ? network_->web_name() : "", fw::kVersion,
                  tf(l.seq != 0), tf(l.can_dim), tf(l.has_sound),
                  tf(shots_->supports_notes() && shots_->available()));
    batch.add(row_);
    for (int i = 0; i < core::kRemoteSettingCount; ++i) {
      std::snprintf(row_, sizeof(row_), "%s\"%s\":%g", i == 0 ? "" : ",",
                    core::remote_setting_key(static_cast<core::RemoteSetting>(i)),
                    static_cast<double>(l.settings[i]));
      batch.add(row_);
    }
    batch.add("},\"melodies\":[");
    for (int i = 0; i < core::ready_melody_count(); ++i) {
      batch.add(i == 0 ? "\"" : ",\"");
      add_escaped(batch, core::ready_melody_name(i));
      batch.add("\"");
    }
    batch.add("],\"update_notes\":\"");
    add_escaped(batch, remote_->update_notes().c_str());
    batch.add("\"}");
  }
  server_.sendContent("");
}

void WebUi::handle_settings_post() {
  if (!authorized()) return;
  const String key = server_.arg("key");
  if (key == "name") {
    if (network_->set_web_name(server_.arg("value").c_str())) {
      send_json(200, "{\"ok\":true}");
    } else {
      send_json(400,
                "{\"error\":\"Use 1-24 letters, digits or hyphens (not at the ends).\"}");
    }
    return;
  }
  core::RemoteCmd c;
  c.kind = core::RemoteCmdKind::kSetting;
  if (!core::remote_setting_from_key(key.c_str(), &c.setting) || !server_.hasArg("value")) {
    send_json(400, "{\"error\":\"unknown setting\"}");
    return;
  }
  c.value = server_.arg("value").toFloat();
  if (remote_->post(c)) send_json(200, "{\"ok\":true}");
  else send_json(503, "{\"error\":\"busy, try again\"}");
}

void WebUi::handle_control() {
  if (!authorized()) return;
  const String action = server_.arg("action");
  const float v = server_.arg("value").toFloat();
  core::RemoteCmd c;
  c.value = v;
  // There is deliberately no "shot", "flush" or "backflush" here: the group
  // only ever runs with someone at the machine.
  if (action == "power") {
    c.kind = core::RemoteCmdKind::kPower;
  } else if (action == "brew") {
    if (v < 80.0f || v > 105.0f) {
      send_json(400, "{\"error\":\"brew temperature out of range\"}");
      return;
    }
    c.kind = core::RemoteCmdKind::kBrewTemp;
  } else if (action == "steam") {
    c.kind = core::RemoteCmdKind::kSteamEnabled;
  } else if (action == "steam_level") {
    if (v < 0 || v > 2) {
      send_json(400, "{\"error\":\"steam level is 0-2\"}");
      return;
    }
    c.kind = core::RemoteCmdKind::kSteamLevel;
  } else {
    send_json(400, "{\"error\":\"unknown action\"}");
    return;
  }
  if (remote_->post(c)) send_json(200, "{\"ok\":true}");
  else send_json(503, "{\"error\":\"busy, try again\"}");
}

void WebUi::handle_note() {
  if (!authorized()) return;
  core::ShotNote n;
  n.id = static_cast<uint32_t>(server_.arg("id").toInt());
  const long r = server_.arg("rating").toInt();
  n.rating = r < 0 ? 0 : (r > 5 ? 5 : static_cast<int>(r));
  copy_field(n.bean, sizeof(n.bean), server_.arg("bean"));
  copy_field(n.text, sizeof(n.text), server_.arg("note"));
  if (shots_->set_note(n)) send_json(200, "{\"ok\":true}");
  else send_json(404, "{\"error\":\"Couldn't save the note (no such shot, or no card)\"}");
}

void WebUi::handle_delete() {
  if (!authorized()) return;
  const uint32_t id = static_cast<uint32_t>(server_.arg("id").toInt());
  if (shots_->remove(id)) send_json(200, "{\"ok\":true}");
  else send_json(404, "{\"error\":\"Couldn't delete that shot\"}");
}

// Every shot, one row each, with its note: the index's columns plus
// rating,bean,note. Chunked, so the history's length doesn't matter.
void WebUi::handle_export() {
  server_.sendHeader("Content-Disposition", "attachment; filename=tondo-shots.csv");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "text/csv", "");
  {
    RowBatch batch(server_);
    batch.add("id,iso8601_local,unix,duration_ms,target_g,final_g,avg_gps,mode,wired,"
              "rating,bean,note\n");
    constexpr int kPage = 16;
    core::ShotSummary page[kPage];
    core::ShotNote note;
    for (int offset = 0;;) {
      const int n = shots_->list(page, kPage, offset);
      if (n == 0) break;
      offset += n;
      for (int i = 0; i < n; ++i) {
        int w = core::format_shot_index_row(row_, sizeof(row_), page[i]);
        if (w <= 0) continue;
        if (row_[w - 1] == '\n') row_[--w] = '\0';
        batch.add(row_);
        if (!shots_->note(page[i].id, &note)) {
          note = core::ShotNote{};
          note.id = page[i].id;
        }
        // The notes row is "id,rating,"bean","note"\n": drop its id column.
        if (core::format_shot_note_row(esc_, sizeof(esc_), note) > 0) {
          const char* rest = std::strchr(esc_, ',');
          batch.add(rest != nullptr ? rest : ",0,\"\",\"\"\n");
        } else {
          batch.add(",0,\"\",\"\"\n");
        }
      }
    }
  }
  server_.sendContent("");
}

// Every shot's samples in one long table (shot_id first), for a spreadsheet
// or a notebook. Reads one record at a time into PSRAM.
void WebUi::handle_export_samples() {
  void* mem = heap_caps_malloc(sizeof(core::ShotRecord), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (mem == nullptr) mem = malloc(sizeof(core::ShotRecord));
  if (mem == nullptr) {
    server_.send(503, "text/plain", "no memory");
    return;
  }
  auto* rec = new (mem) core::ShotRecord;
  server_.sendHeader("Content-Disposition",
                     "attachment; filename=tondo-shot-samples.csv");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "text/csv", "");
  {
    RowBatch batch(server_);
    batch.add("shot_id,t_ms,weight_g,flow_gps\n");
    constexpr int kPage = 16;
    core::ShotSummary page[kPage];
    for (int offset = 0;;) {
      const int n = shots_->list(page, kPage, offset);
      if (n == 0) break;
      offset += n;
      for (int i = 0; i < n; ++i) {
        if (!shots_->read(page[i].id, *rec)) continue;
        for (int k = 0; k < rec->n_samples; ++k) {
          const int w = std::snprintf(row_, sizeof(row_), "%lu,",
                                      static_cast<unsigned long>(page[i].id));
          core::format_shot_sample_row(row_ + w, sizeof(row_) - static_cast<size_t>(w),
                                       rec->samples[k]);
          batch.add(row_);
        }
      }
    }
  }
  server_.sendContent("");
  rec->~ShotRecord();
  free(mem);
}

void WebUi::handle_update(bool install) {
  if (install) {
    if (!authorized()) return;
  } else if (remote_ == nullptr) {
    send_json(503, "{\"error\":\"updates unavailable\"}");
    return;
  }
  core::RemoteCmd c;
  c.kind = install ? core::RemoteCmdKind::kInstall : core::RemoteCmdKind::kCheckUpdate;
  if (install && !remote_->live().update_available) {
    send_json(409, "{\"error\":\"No update to install. Check first.\"}");
    return;
  }
  if (remote_->post(c)) send_json(200, "{\"ok\":true}");
  else send_json(503, "{\"error\":\"busy, try again\"}");
}

// The settings file, exactly as Settings > Tondo > Backup writes it to the
// card. PIN-gated: with wifi=1 / token=1 it holds the WiFi password and the
// machine's pairing token in plain text.
void WebUi::handle_backup() {
  if (!authorized()) return;
  std::string text, err;
  int n = 0;
  if (!settings_backup_text(server_.arg("wifi") == "1", server_.arg("token") == "1", &text,
                            &n, &err)) {
    server_.send(500, "text/plain", err.c_str());
    return;
  }
  char dispo[80];
  std::snprintf(dispo, sizeof(dispo), "attachment; filename=%s-settings.txt",
                network_->web_name());
  server_.sendHeader("Content-Disposition", dispo);
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "text/plain; charset=utf-8", text.c_str());
}

// Replace the settings with an uploaded backup, then restart: the same
// REPLACE semantics as restoring from the card (per-unit keys, the PIN among
// them, survive). App does the restart, after the response has gone out.
void WebUi::handle_restore() {
  if (!authorized()) return;
  const String& text = server_.arg("text");
  std::string body(text.c_str(), text.length());
  std::string err;
  int n = 0;
  if (!settings_backup_restore_text(body, &n, &err)) {
    std::snprintf(row_, sizeof(row_), "{\"error\":\"%s\"}", err.c_str());
    send_json(400, row_);
    return;
  }
  std::snprintf(row_, sizeof(row_), "{\"ok\":true,\"restored\":%d}", n);
  send_json(200, row_);
  core::RemoteCmd c;
  c.kind = core::RemoteCmdKind::kReboot;
  if (!remote_->post(c)) {
    delay(500);
    esp_restart();  // queue jammed: the settings are in, restart regardless
  }
}

void WebUi::handle_manifest() {
  server_.sendHeader("Cache-Control", "max-age=86400");
  server_.send(200, "application/manifest+json",
               "{\"name\":\"Tondo\",\"short_name\":\"Tondo\",\"start_url\":\"/\","
               "\"display\":\"standalone\",\"background_color\":\"#000000\","
               "\"theme_color\":\"#000000\",\"icons\":["
               "{\"src\":\"/icon-192.png\",\"sizes\":\"192x192\",\"type\":\"image/png\","
               "\"purpose\":\"any maskable\"},"
               "{\"src\":\"/icon-512.png\",\"sizes\":\"512x512\",\"type\":\"image/png\","
               "\"purpose\":\"any maskable\"}]}");
}

void WebUi::send_png(const uint8_t* data, size_t len) {
  constexpr size_t kSlice = 8 * 1024;  // same peak-RAM reasoning as handle_root
  server_.sendHeader("Cache-Control", "max-age=86400");
  server_.setContentLength(len);
  server_.send(200, "image/png", "");
  for (size_t off = 0; off < len; off += kSlice) {
    const size_t n = len - off < kSlice ? len - off : kSlice;
    server_.sendContent_P(reinterpret_cast<const char*>(data + off), n);
  }
}

}  // namespace platform
