#pragma once

#include <WebServer.h>

#include <functional>

#include "core/clock.h"
#include "core/remote.h"
#include "core/shot_store.h"
#include "platform_esp32/token_setup.h"

namespace platform {

class Network;

// The device's ONE web server (port 80), serving every HTTP surface on
// different routes so features can't fight over the socket:
//   /            the shot-history app (embedded React bundle) — or, while a
//                setup portal is active, that portal's page (token / WiFi)
//   /save /wifi  the setup portal's POST endpoints (delegated to TokenSetup)
//   /api/summary device name/version, ACTIVE THEME palette, storage, stats
//   /api/shots   the shot index as JSON (chunked; newest first)
//   /api/shot.csv?id=N   one shot's sample series (download)
//   /log         the in-RAM diagnostic log ring as plain text (also /api/log)
//   /coredump    the crash dump the panic handler saved to flash, as a file
//                download (404 when none); ?erase=1 clears the stored dump
// The phone page (http://<name>.local, mDNS via Network) adds, all through the
// core::RemoteBridge mailbox so nothing here touches App/LVGL/BrewController:
//   GET  /api/live?since=N  machine + scale + running shot (samples from N)
//   GET  /api/settings      the phone-changeable settings + choices
//   POST /api/settings      key=&value=                       [PIN]
//   POST /api/control       action=power|brew|steam|steam_level&value= [PIN]
//                           (never a shot, flush or backflush -- by design)
//   POST /api/pin           check a PIN (the page's unlock)
//   POST /api/shot/note     id=&rating=&bean=&note=           [PIN]
//   POST /api/shot/delete   id=                                [PIN]
//   GET  /api/export.csv    every shot + its note; /api/export_samples.csv
//                           every shot's samples in one long table
//   POST /api/update/check, /api/update/install                [install: PIN]
//   GET  /api/backup.txt?wifi=&token=   the settings file      [PIN]
//   POST /api/restore       text= (a backup file) -> replace + restart [PIN]
//   /manifest.webmanifest + icons: "Add to Home Screen"
// PIN: the X-Tondo-Pin header or a pin= field. Five wrong tries lock every
// PIN-gated route for a minute.
//
// Serving model is PER-BOARD, learned on hardware:
//   - P4 (DSI) boards: a dedicated FreeRTOS task, so a browser pulling the
//     bundle never stalls LVGL. The DSI panel is self-clocked and the radio
//     is a separate chip — concurrent serving costs nothing visible.
//   - S3 (RGB) boards: pumped from the MAIN LOOP. The RGB raster streams
//     continuously from PSRAM, which shares a bus with flash; serving the
//     flash-resident bundle WHILE LVGL renders starved the panel's bounce
//     buffer and the image visibly jumped around (seen on the 4.3C). The
//     loop-pump "freeze during a request" is protective there: it idles
//     LVGL's bus traffic for the duration.
// Thread notes (task mode): the shot store is mutex-guarded (it already
// serves the SD writer task); clock reads are plain time(); the theme
// callback reads nine palette words (a torn read during a theme switch
// mis-colors one response, harmlessly); the portal handlers write NVS
// (thread-safe) and MicraLink's setters, which already tolerate cross-task
// callers (the BLE task).
struct WebTheme {
  uint32_t bg, rail, card, text, muted, accent, ok, warn, alert;
};

class WebUi {
 public:
  // `theme` is read per request so the page always reflects the palette the
  // screen is showing right now. `name` must outlive this object.
  // Registers routes; on DSI boards also spawns the server task. In both
  // models the listening socket is opened lazily once WiFi is in ANY active
  // mode — binding before the network stack exists asserts inside lwip
  // ("Invalid mbox", seen on HW).
  void begin(TokenSetup& setup, core::IShotStore& shots, core::IClock& clock,
             const char* name, std::function<WebTheme()> theme);

  // The phone page's live view, remote control and PIN. Call before begin().
  // Without it (nullptrs) those routes answer 503 and the history still works.
  void set_remote(core::RemoteBridge* remote, Network* network) {
    remote_ = remote;
    network_ = network;
  }

  // Main-loop pump. No-op on DSI boards (the task owns the server there);
  // on S3 boards this is where all serving happens.
  void poll();

 private:
  static void task_entry(void* self);
  void run();        // DSI: server task loop
  bool ensure_bound();  // lazy socket bind once WiFi is up

  void handle_root();
  void handle_summary();
  void handle_shots();
  void handle_shot_csv();
  void handle_log();
  void handle_coredump();
  // Phone page.
  bool authorized();  // PIN check + lockout; answers 403/429 itself when not
  void send_json(int code, const char* body);
  void handle_live();
  void handle_settings_get();
  void handle_settings_post();
  void handle_control();
  void handle_pin();
  void handle_note();
  void handle_delete();
  void handle_export();
  void handle_export_samples();
  void handle_update(bool install);
  void handle_backup();
  void handle_restore();
  void handle_manifest();
  void send_png(const uint8_t* data, size_t len);

  WebServer server_{80};
  TokenSetup* setup_ = nullptr;
  core::IShotStore* shots_ = nullptr;
  core::IClock* clock_ = nullptr;
  const char* name_ = "Tondo";
  bool bound_ = false;  // listening socket open (needs the network stack)
  std::function<WebTheme()> theme_;
  core::RemoteBridge* remote_ = nullptr;
  Network* network_ = nullptr;
  int pin_failures_ = 0;
  uint32_t pin_locked_until_ms_ = 0;
  // Row scratch for the JSON/CSV bodies that carry note text (escaped text
  // can run 6x its bytes). A member, not a local: the server task has 8 KB.
  char row_[2048];
  char esc_[1600];
};

}  // namespace platform
