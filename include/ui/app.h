#pragma once

#include <cstdint>
#include <functional>

#include "core/battery.h"
#include "core/brew.h"
#include "core/clock.h"
#include "core/display_settings.h"
#include "core/history.h"
#include "core/machine.h"
#include "core/network.h"
#include "core/provisioner.h"
#include "core/settings_backup.h"
#include "core/update_check.h"
#include "core/ready_chime.h"
#include "core/scale.h"
#include "core/scale_provisioner.h"
#include "core/shot_store.h"
#include "core/sound.h"
#include "ui/home_tab.h"
#include "ui/screen.h"
#include "ui/settings_tab.h"
#include "ui/stats_tab.h"

// The on-screen application: the tab shell plus each tab's content, built into
// the active LVGL screen and laid out for a screen profile. Holds references to
// the core ports (IMachine for control/state, IProvisioner for setup) and
// nothing more — it never sees the concrete transport.
//
// Usage: build() once, then refresh() on a timer to reflect the latest state.

namespace ui {

class App {
 public:
  ~App();

  void build(core::IMachine& machine, core::IProvisioner& provisioner,
             core::IBattery& battery, core::IDisplaySettings& display,
             core::IClock& clock, core::IHistory& history, core::IScale& scale,
             core::IScaleProvisioner& scale_provisioner, core::IBrewController& brew,
             core::INetwork& network, core::ISound& sound, core::IShotStore& shots,
             const ScreenProfile& screen, core::IUpdateSource* updates,
             core::ISettingsBackup* backup);
  // NOTE: `updates` is deliberately NOT defaulted. It used to be, and the
  // internal layout-rebuild call omitted it -- so every rebuild silently set
  // updates_ = nullptr, hiding the Info page's "Check for updates" button and
  // the Settings cadence row, and no-oping manual checks, until the next boot.
  // Rebuilds fire on theme changes and scale connect/disconnect, so it looked
  // random. A defaulted parameter CHANGED an existing caller's behaviour
  // instead of breaking the build; requiring it makes the compiler catch this.
  // Pass nullptr explicitly if a caller genuinely has no update source.
  // `backup` is not defaulted for the same reason: null hides Tondo > Backup,
  // which is right on the boards without a card slot and a silent regression
  // everywhere else.

  // Reflect the latest machine state and scan results in the UI (no I/O).
  void refresh();

  // True while the idle screensaver is showing (no touch for the screen-timeout).
  // The device loop uses this to pause BLE reconnect scanning + run the daily
  // update check only when idle (both free/spare internal RAM when it matters).
  bool screensaver_active() const { return screensaver_on_; }

  // Drain the scale's native flow-rate stream into the Home flow graph. Called
  // every device-loop iteration (much faster than refresh()) so the line plots
  // the scale's real sample rate smoothly. Cheap no-op when nothing is pending.
  void pump_scale_chart();

  // Called (once, after a sustained reading) when the pack drops to/below
  // cutoff_volts on battery — the device wires this to deep sleep.
  void set_low_battery_handler(float cutoff_volts, std::function<void()> on_critical);

  // Switch the active tab by index (0=Home, 1=Settings, 2=Stats). Mainly for
  // the simulator to render a specific tab.
  void show_tab(int index);
  // Round glass only: the temperature dial (tap the brew numerals) and the
  // steam sheet (tap the steam capsule). Public so the sim can pose them.
  void open_brew_dial();
  void open_steam_sheet();
  void close_dial();
  void dial_step(int half_degrees);    // -/+ on the dial (0.5 deg steps)
  void dial_from_arc();                // the dial's ring was dragged
  void dial_commit();                  // Set
  void steam_choose_level(int level);  // steam sheet: level 0..2
  void steam_choose_enabled(bool on);  // steam sheet: on / off
  // The Milk sheet (round Home's Milk button, scale paired): weigh milk into
  // the steam pitcher for a drink -- its volume times the milk's density
  // (core/milk.h). Same layer and close path as the dial (dial_).
  void open_milk_sheet();
  void milk_open_list(bool drinks); // tap the drink / milk capsule: every choice
  void milk_pick(int index);        // a row of that list
  void milk_close_list();           // tap outside the list: no change
  void milk_step_ml(int dir);       // -/+ by core::kMilkMlStep (remembered per drink)
  void milk_tare();                 // the sheet's Tare
  // Round-glass Home: show its screen-level backdrop only while Home is the
  // active tab (called on every tab change; a no-op elsewhere).
  void sync_round_backdrop();

  // Bound to UI events:
  void toggle_power();         // power button
  void tare_scale();           // Home "Tare" button
  // Scale > Settings "On the scale" rows: advance descriptor slot `index` to
  // its next option (writes to the scale; refused offline or mid-shot).
  void cycle_scale_device_setting(int index);
  void toggle_flow_units();    // Home graph unit button (g/s <-> g)
  void start_scan();           // Settings "Scan" button
  void save_scanned(int index);  // a result row in the Settings list
  void forget();               // Settings "Forget" button
  void toggle_connection();    // Settings "Connect"/"Disconnect" button
  void start_scale_scan();        // Scale page "Scan" button
  void save_scanned_scale(int index);  // a result row in the Scale scan list
  void forget_scale();            // Scale page "Forget" button
  void toggle_scale_connection(); // Scale page "Connect"/"Disconnect"
  void target_adjust(int dir);    // Scale target weight +/- (grams)
  void open_token_setup();     // Settings "Setup" -> token-entry modal
  void start_token_setup();    // modal "Enter token" -> WiFi portal + instructions
  void cancel_token_setup();   // WiFi-modal "Cancel"
  void set_wifi_enabled(bool on);  // Device "WiFi" enable switch
  void start_wifi_setup();     // Device "Set up WiFi" -> AP portal + instructions
  void cancel_wifi_setup();    // WiFi-setup-modal "Cancel"
  void forget_wifi();          // Device "Forget" (WiFi) button
  void timezone_select(int index);  // Device timezone dropdown
  void set_ntp_enabled(bool on);    // Device "Auto time (NTP)" switch
  void dismiss_modal();        // token-modal "Cancel"
  void select_settings_section(int section);  // Settings segmented selector
  void on_settings_page_shown();  // lv_menu navigated; re-seed time on the Device page
  void brew_adjust(int dir, bool half);  // Brew +/- (half: 0.5 snap, long-press)
  void boiler_adjust(int dir);           // Boiler level +/-
  void steam_set_enabled(bool on);       // steam boiler on/off switch
  void brightness_adjust(int dir);       // Display brightness +/-
  void cycle_screen_timeout();           // Device "Screen dim": Off / 1 / 5 / 15 / 30 min
  void cycle_screensaver_style();        // Device "Idle screen": Lion / Dim / Off / Clock
  void screensaver_tick();               // idle-dim poll (from an lv_timer, ~4 Hz)
  void saver_anim_tick();                // bouncing-logo step (from its lv_timer)
  void pose_screensaver();               // sim: force the saver on for a render
  // Ready alert: the end of a warm-up shown on the glass, for when nobody
  // hears the chime (start_ready_alert). Stopped by a tap, a shot, the
  // machine leaving Ready, or kReadyAlertMs.
  void stop_ready_alert();
  void ready_pulse_tick();               // backlight breath step (its lv_timer)
  void pose_ready_alert();               // sim: show it for a render
  void open_update_modal();              // update-available notice (also sim pose)
  void open_checking_modal();            // "checking..." spinner during a live check
  void open_no_update_modal();           // "up to date" notice (forced check, boot)
  void skip_update();                    // modal "Skip this version"
  void cycle_update_cadence();           // "Check for updates": Off/Boot/Daily/Hourly
  void set_beta_updates(bool on);        // "Beta updates" switch (pre-releases)
  void manual_update_check();            // Stats > Info button -> live check
  void update_result_poll();             // watch check_seq(); show the outcome
  void sync_beta_row();                  // show/hide "Beta updates" with the cadence
  bool update_ui_active() const { return modal_ != nullptr; }
  // "Install now" -> device main stashes the version and reboots into the
  // early-boot install mode (the download can't run with the full display up).
  void set_install_handler(std::function<void(const std::string&)> h) {
    install_handler_ = std::move(h);
  }
  void request_install();                // modal "Install now"
  // Settings > Tondo > Backup. Both directions confirm first: the backup modal
  // spells out that the card ends up holding the pairing token in plain text
  // (and, unless the switch is turned off, the WiFi password); the restore modal
  // spells out that the file REPLACES every setting and the device restarts.
  void open_backup_modal();               // "Back up to card" -> confirm + switches
  void set_backup_include_wifi(bool on);  // that modal's two credential switches
  void set_backup_include_token(bool on);
  void confirm_backup();                 // modal "Back up"
  void open_restore_modal();             // "Restore from card" -> confirm / refusal
  void confirm_restore();                // modal "Restore"
  void backup_result_poll();             // watch state(); swap in the outcome modal
  void pose_backup_result();             // sim: skip the working notice's floor
  void reboot_after_restore();           // the restore modal's hold timer fired
  void hour_select(int idx);             // Time & date dropdowns: selection ->
  void minute_select(int idx);           // clock/date write (hour idx == hour,
  void month_select(int idx);            // month/day 1-based, year offset from
  void day_select(int idx);              // kClockBaseYear; day clamps + its
  void year_select(int idx);             // option list resizes with the month)
  void set_clock_24h(bool on);           // Device "24-hour" switch
  void set_use_fahrenheit(bool on);      // Device "Fahrenheit" switch
  void set_drop_negative_flow(bool on);  // Scale "Drop negative g/s" switch
  void set_scope_graph(bool on);         // Scale "Oscilloscope graph" switch
  void set_perf_overlay(bool on);        // LVGL FPS/CPU overlay (persisted)
  void toggle_perf_overlay();            // long-press on Stats > Info's Firmware row
  void set_click_sound(bool on);         // Device > Sound "Button sounds" switch
  void set_shot_sounds(bool on);         // Device > Sound: the event-sound switches
  void set_milk_sounds(bool on);         // (core::CueGroup); turning one on
  void set_alert_sounds(bool on);        // auditions a sound from its group
  // Sound "Chime volume": linear 0-100 (0 = Off), slider + [-]/[+]. sound.cpp
  // maps it onto a dB curve so the steps are even by ear.
  void set_ready_chime_vol(int percent, bool audition);
  void ready_chime_vol_adjust(int dir);  // one step per [-]/[+] tap
  void cycle_ready_melody();             // Micra "Chime melody": Off/Blue/Pink/...
  void theme_select(int index);          // Device theme roller selection
  void apply_pending_theme();            // deferred rebuild (from lv_async_call)
  void apply_layout_rebuild();           // deferred rebuild after scale pair/forget
  void select_stats_section(int section); // Stats segmented selector
  void set_history_filter(int year_month); // History month filter: 0 = all,
                                           // else year*100 + month (1-12)
  void open_shot_card(uint32_t id);       // History row tap -> full-screen card
  void open_delete_shot_modal();          // shot-card trash tap -> confirm dialog
  void confirm_delete_shot();             // modal "Delete": remove + refresh
  void open_reset_stats_modal();          // metric-card tap -> confirm dialog
  void confirm_reset_stats();             // modal "Reset": set the marker
  void open_log_modal();                  // Info "Diagnostic log" row -> viewer
  void shot_button();  // shot-mode toggle, or Reset while a shot is in review
  // Settings "Restart display" handler: wired by device main to an RGB panel
  // DMA resync (falls back to esp_restart on other boards); no-op in the sim.
  void set_restart_handler(std::function<void()> h) { restart_handler_ = std::move(h); }
  void restart_device() { if (restart_handler_) restart_handler_(); }
  // A FULL soft reboot. Separate from restart_device() on purpose: that one
  // heals an RGB panel in place and never reboots, which is right for the
  // "Restart display" row and wrong after a settings restore.
  void set_reboot_handler(std::function<void()> h) { reboot_handler_ = std::move(h); }
  void reboot_device() { if (reboot_handler_) reboot_handler_(); }
  void start_clean_lock();  // Settings "Lock display for cleaning": 30 s touch lockout
  void clean_lock_tick();   // countdown update (from an lv_timer, 4 Hz)
  // Backflush cleaning (Settings > Micra): a full-screen mode that prompts for
  // the blind filter, then runs core's pulse sequence with a live cycle
  // readout. Cancel stops it; Back stops it AND leaves.
  void open_backflush();
  void close_backflush();
  void backflush_go();      // "Go" / "Run again"
  void backflush_cancel();  // "Cancel": stop, stay on the screen
  void backflush_tick();    // readout update (from an lv_timer, 4 Hz)
  void toggle_manual_flush();  // Home "Flush"/"Stop" button
  void review_hold_adjust(int dir);  // Scale settings: review-hold stepper (5s steps)
  void detect_lead_in_adjust(int dir);  // Scale settings: detect lead-in stepper (1s steps)
  void cycle_flow_smooth();          // Scale settings: Off/Light/Medium/Strong
  void cycle_shot_window_growth();   // Scale settings: Snap/Smooth/Continuous
  void cycle_flush();                // Micra settings "Auto flush": Off / 3 s / 6 s
  void cycle_flush_delay();          // Micra settings "Flush delay": 3 / 6 / 9 / 15 s
  void set_auto_connect(bool on) {   // Micra settings: connect to saved machine at boot
    if (provisioner_ != nullptr) provisioner_->set_auto_connect(on);
  }
  void set_wired_paddle(bool on);    // Micra settings: paddle harness vs shot detector
  // Transient auto-dismissing message over the current tab (the tabview stays
  // visible — unlike open_modal). Used for the "shot refused, no scale"
  // feedback; works on every layout incl. compact (which has no shot button
  // to flash). Tap or timeout dismisses.
  void show_toast(const char* msg);
  void dismiss_toast();
  void scale_battery_poll();  // the low-battery toast for the paired scale (refresh)
  // Sim-only pose: seed the unwired capture ring with a synthetic pre-shot
  // baseline + pour ramp and run the mid-shot handoff as if the detector just
  // confirmed — renders the back-filled live shot plot deterministically.
  void pose_unwired_midshot(uint32_t shot_ms = 9000);
  void zoom_step(int dir);                // Stats time-axis zoom: -1 in, +1 out
  void commit_temp_edits();              // write pending temp edits (on exit)

 private:
  void update_settings_view();
  void update_scale_view();   // refresh the Scale page (connection + target)
  void update_stats_view();   // refill the chart / info from history
  void refresh_log_modal();   // re-snapshot the ring into the open log modal
  void update_history_view(); // History section: guidance / metrics / list
  // Assemble + save a ShotRecord at the kReview freeze (no-op without storage
  // or a real date). Call AFTER finish/review_shot_plot rebased the ring.
  void capture_shot_record(const core::BrewSnapshot& bsnap,
                           const core::ScaleSnapshot& snap);
  void update_temp_panels(const core::MachineSnapshot& state);
  void sync_home_setpoints(bool connected);  // mirror set-points to the Home steppers
  void update_battery_runtime(const core::BatteryState& b);  // track drain for the estimate
  void seed_time_controls();   // load the clock into the time/date dropdowns
  // Time & date: hide the Time/Date pickers (show a note instead) once Auto
  // time has set the clock this session — NTP would overwrite a manual pick.
  void sync_time_rows();
  void apply_date_selection(); // clamp day, resize its options, write set_date
  // Re-derive the inferred Heating state (hysteresis bit = heating_) and
  // start/stop the status-dot pulse. Call once per machine-snapshot pass,
  // before update_home.
  void update_heating(const core::MachineSnapshot& state);
  void rebuild();             // tear down + rebuild the UI (e.g. after a theme change)
  void request_layout_rebuild(int section);  // defer a rebuild, returning to `section`
  lv_obj_t* open_modal(const char* title, const char* body);  // returns the card
  lv_obj_t* open_modal_card(const char* title);  // card + title only (no body)
  lv_obj_t* modal_button_row(lv_obj_t* card);    // side-by-side button container
  // Portal instructions: WIFI: join QR + the manual ssid/url steps.
  lv_obj_t* open_join_modal(const char* title, const char* ssid, const char* url,
                            const char* then_line);
  void close_modal();
  void show_token_modal(bool fetch_failed);  // token-entry prompt (Enter token / Cancel)
  void show_wifi_modal();   // WiFi instructions + Cancel
  void show_wifi_setup_modal();  // WiFi-credential portal instructions + Cancel
  core::NetState net_status() const {
    return network_ != nullptr ? network_->status() : core::NetState::Disabled;
  }
  bool scale_connect_enabled() const {
    return scale_provisioner_ != nullptr && scale_provisioner_->connect_enabled();
  }

  core::IMachine* machine_ = nullptr;
  core::IProvisioner* provisioner_ = nullptr;
  core::IBattery* battery_ = nullptr;
  core::IDisplaySettings* display_ = nullptr;
  core::IClock* clock_ = nullptr;
  core::IHistory* history_ = nullptr;
  core::IScale* scale_ = nullptr;
  core::IScaleProvisioner* scale_provisioner_ = nullptr;
  core::IBrewController* brew_ = nullptr;
  core::INetwork* network_ = nullptr;
  core::IUpdateSource* updates_ = nullptr;  // optional; null = no update UI
  core::ISettingsBackup* backup_ = nullptr;  // optional; null = no Backup page
  bool backup_include_wifi_ = true;   // the backup modal's switches (per-open)
  bool backup_include_token_ = true;
  bool backup_pending_ = false;      // a request is out; poll for its outcome
  lv_obj_t* backup_wifi_switch_ = nullptr;   // "Include WiFi network" (modal-owned)
  lv_obj_t* backup_token_switch_ = nullptr;  // "Include pairing token"
  lv_obj_t* backup_note_ = nullptr;          // what an opted-out backup leaves out
  lv_timer_t* reboot_timer_ = nullptr;       // holds the restored notice on screen
  uint32_t backup_started_ms_ = 0;           // when the working modal went up
  void update_backup_note();                 // rewrite that line from the switches
  int update_last_seq_ = -1;         // last seen check_seq(); -1 = not baselined
  bool manual_check_pending_ = false;  // a user-initiated check awaits its result
  bool update_notice_pending_ = false;  // an auto notice deferred under the saver
  uint32_t manual_check_started_ = 0;  // lv_tick when the manual check began
  static constexpr uint32_t kManualCheckTimeoutMs = 30000;  // give up + report
  std::function<void(const std::string&)> install_handler_;  // "Install now"
  core::ISound* sound_ = nullptr;
  core::IShotStore* shots_ = nullptr;
  bool click_sound_on_ = true;  // cached from IDisplaySettings (checked per press)
  int ready_chime_vol_ = 80;    // cached likewise, 0..100 (0 = chime off)
  int ready_chime_mel_ = 1;     // cached likewise: 0 = off, 1.. = ready melody
  lv_obj_t* tabview_ = nullptr;
  ScreenProfile screen_{};          // stored so we can rebuild on a theme change
  lv_obj_t* modal_ = nullptr;       // current overlay modal, if open
  // Round glass: the Settings / Stats tab bodies glassify_tree re-runs on
  // (rows built later — scan results, history — start out opaque).
  lv_obj_t* glass_bodies_[2] = {nullptr, nullptr};
  // Round glass: the brew temperature dial / steam sheet (one at a time, on
  // the top layer). dial_target_c_ is the dial's pending value in Celsius.
  lv_obj_t* dial_ = nullptr;
  lv_obj_t* dial_value_ = nullptr;
  lv_obj_t* dial_set_label_ = nullptr;
  lv_obj_t* dial_arc_ = nullptr;
  float dial_target_c_ = 93.0f;
  lv_obj_t* steam_btns_[5] = {};    // steam sheet: On, Off, level 1..3
  void steam_sheet_refresh();       // re-tone the sheet's buttons in place
  void dial_refresh_labels();
  void hide_home_for_sheet();
  // Milk sheet: open iff milk_arc_ != nullptr. Opening lends the scale away
  // from the shot machinery (IBrewController::set_scale_borrowed) and
  // close_dial gives it back.
  lv_obj_t* milk_arc_ = nullptr;       // progress ring to the target weight
  lv_obj_t* milk_weight_ = nullptr;    // live grams (hero numerals)
  lv_obj_t* milk_target_ = nullptr;    // "of 175 g · 1.030 g/ml"
  lv_obj_t* milk_status_ = nullptr;    // what to do next
  lv_obj_t* milk_drink_label_ = nullptr;
  lv_obj_t* milk_type_label_ = nullptr;
  lv_obj_t* milk_ml_label_ = nullptr;
  lv_obj_t* milk_list_ = nullptr;      // the drink/milk list over the sheet, if open
  bool milk_list_drinks_ = false;      // which list it is
  int milk_drink_ = 0;                 // core::kMilkDrinks index
  int milk_type_ = 0;                  // core::kMilkTypes index
  int milk_ml_ = 0;                    // that drink's amount, ml
  bool milk_tared_ = false;            // the sheet's Tare used since opening
  bool milk_close_played_ = false;     // milk sounds: once per pour (reset by
  bool milk_target_played_ = false;    // Tare or a new target), so a reading
  bool milk_over_played_ = false;      // wobbling across a band can't repeat
  bool milk_seen_ = false;             // drawn since the last change of inputs
  bool milk_connected_ = false;        // scale link as last drawn
  uint32_t milk_seq_ = 0;              // scale seq as last drawn
  int milk_tone_ = -1;                 // ring/status colour as last written
  float milk_last_g_ = 0.0f;           // weight at the last movement
  uint32_t milk_moved_ms_ = 0;         // lv_tick of the last movement or tap
  void milk_refresh_labels();          // picks/amount/target text, then a redraw
  int milk_ml_for(int drink) const;    // a drink's amount: remembered, else preset
  void milk_tick();                    // weight -> ring + numerals (each loop; cheap)
  void milk_poll();                    // refresh: close on a shot or when left idle
  // Log-viewer modal (live tail): refreshed from core::log_ring() by a timer
  // while open; all three are torn down together in close_modal().
  lv_obj_t* log_box_ = nullptr;
  lv_obj_t* log_label_ = nullptr;
  lv_timer_t* log_timer_ = nullptr;
  uint32_t config_mode_seen_ = 0;   // last machine config-mode event toasted (edge-detect)
  bool wifi_setup_shown_ = false;   // token-over-WiFi instructions modal is open
  bool wifi_portal_shown_ = false;  // WiFi-credential setup modal is open
  uint32_t scale_readout_tick_ = 0;     // paces the shot-timer redraw (~10 Hz)
  float last_weight_g_ = 0.0f;          // last weight drawn (redraw on change only)
  bool last_scale_connected_ = false;
  core::ShotPhase shot_phase_ = core::ShotPhase::kIdle;  // last seen (graph reset/freeze edges)
  uint32_t brew_reject_seen_ = 0;  // last review_reject_seq (flash Reset on change)
  uint32_t scale_refuse_seen_ = 0;  // last scale_refuse_seq (toast on change)
  // Scale battery notice (scale_battery_poll): one toast per connection at the
  // model's low mark; pending while a shot runs or the screen is asleep.
  bool scale_batt_warned_ = false;
  bool scale_batt_pending_ = false;
  lv_obj_t* toast_ = nullptr;          // transient message card (lv_layer_top)
  lv_timer_t* toast_timer_ = nullptr;  // its auto-dismiss timer
  uint32_t unwired_shot_t0_ = 0;   // lv_tick of the detected shot's retro start
                                   // (captured on the kBrewing edge; drives the
                                   // shot-aligned review repaint)
  bool stop_hint_seen_ = false;    // last stop_hint (flash the pill on the rise)
  bool shot_near_played_ = false;  // "nearly there" sounded for this shot
  // Screensaver: dims the backlight after the configured idle time; any touch
  // restores it (LVGL's inactivity clock resets on input). Timer survives
  // rebuilds — it belongs to the app, not the widget tree.
  lv_timer_t* screensaver_timer_ = nullptr;
  bool screensaver_on_ = false;
  // Saver cover on lv_layer_top (swallows the waking tap): black + the
  // recolored lion drifting DVD-style (Logo), transparent (Dim), black (Off);
  // torn down on wake.
  lv_obj_t* saver_layer_ = nullptr;     // top-layer cover: Dim (transparent) and Off (black)
  lv_obj_t* saver_screen_ = nullptr;    // the artworks' own black screen (loaded in place of the UI)
  lv_obj_t* saver_prev_screen_ = nullptr;  // the UI screen to load back on wake
  lv_obj_t* saver_img_ = nullptr;       // null in the Dim and Off styles
  lv_timer_t* saver_anim_ = nullptr;
  // Clock idle screen (round glass; start_clock_saver): it reuses
  // saver_screen_ / saver_anim_, and saver_orb_ != null marks it running.
  lv_obj_t* saver_clock_ = nullptr;
  lv_obj_t* saver_day_ = nullptr;
  lv_obj_t* saver_orb_ = nullptr;
  lv_obj_t* saver_orb_temp_ = nullptr;
  lv_obj_t* saver_orb_state_ = nullptr;
  // Ready alert (start_ready_alert): a full-screen cover on lv_layer_top,
  // only while shown. The check mark's points live here because lv_line keeps
  // the pointer rather than copying them.
  void start_ready_alert();
  void ready_alert_poll(const core::MachineSnapshot& state);
  lv_obj_t* ready_alert_ = nullptr;
  lv_obj_t* ready_alert_temps_ = nullptr;
  lv_timer_t* ready_pulse_ = nullptr;   // backlight breath, where it can dim
  uint32_t ready_alert_since_ = 0;      // lv_tick at start
  int ready_pulse_last_ = -1;           // last level written (skip repeats)
  lv_point_precise_t ready_check_pts_[3] = {};
  uint32_t saver_clock_t0_ = 0;
  void start_clock_saver();
  void clock_saver_tick();
  int saver_vx_ = 0, saver_vy_ = 0;     // px per anim tick
  int saver_color_i_ = 0;               // palette index; advances on bounce
  void start_screensaver(int style);  // core::IDisplaySettings::SaverStyle
  void set_saver_art();               // size + source the bouncing lion
  // Where the saver art bounces: the whole screen, or the band on round glass.
  ui::Band saver_box() const { return ui::round_band(screen_); }
  void stop_screensaver();
  // Cleaning lock: a full-screen opaque overlay on the top layer swallows all
  // touch input for kCleanLockSecs while a countdown shows time remaining.
  void end_clean_lock();
  static constexpr int kCleanLockSecs = 30;
  lv_obj_t* clean_lock_overlay_ = nullptr;
  lv_obj_t* clean_lock_count_ = nullptr;  // the big remaining-seconds label
  lv_timer_t* clean_lock_timer_ = nullptr;
  uint32_t clean_lock_t0_ = 0;   // lv_tick at lock start
  int clean_lock_shown_s_ = -1;  // last rendered value (redraw on change only)
  // Backflush mode. The SEQUENCE lives in BrewController (so it can't outlive
  // or be orphaned by this UI); these are just the screen's widgets + which of
  // the four screens is showing.
  enum BackflushState { kBackflushPrompt = 0, kBackflushRunning, kBackflushDone,
                        kBackflushAborted };
  lv_obj_t* bf_overlay_ = nullptr;
  lv_obj_t* bf_msg_ = nullptr;
  lv_obj_t* bf_cycle_label_ = nullptr;   // "Cycle 3 of 10"
  lv_obj_t* bf_phase_label_ = nullptr;   // "Running 3s" / "Pause 2s"
  lv_obj_t* bf_go_btn_ = nullptr;
  lv_obj_t* bf_go_label_ = nullptr;      // "Go" / "Run again"
  lv_obj_t* bf_cancel_btn_ = nullptr;
  lv_obj_t* bf_back_btn_ = nullptr;
  lv_timer_t* bf_timer_ = nullptr;
  int bf_state_ = kBackflushPrompt;
  bool theme_rebuild_pending_ = false;  // coalesce rapid theme cycling into one rebuild
  bool layout_rebuild_pending_ = false; // coalesce a scale pair/forget rebuild
  int rebuild_section_ = kSectionDeviceDisplay;  // Settings section to return to
                                                 // after rebuild() (theme button)
  HomeWidgets home_{};
  SettingsWidgets settings_{};
  StatsWidgets stats_{};

  // Battery runtime estimate: a ~10-min sliding window of percent-over-time; the
  // drain rate (oldest-in-window vs now) extrapolates the remaining runtime. The
  // window is cleared while on USB power / no battery (no estimate).
  core::BatteryState battery_state_{};  // latest read (for Stats > Info)
  struct BattSample {
    uint32_t t_ms;
    int pct;
  };
  static constexpr int kBattHist = 12;  // ~11 min at 1 sample/min
  BattSample batt_hist_[kBattHist] = {};
  int batt_hist_count_ = 0;
  int batt_hist_head_ = 0;
  uint32_t batt_last_sample_ms_ = 0;
  char batt_runtime_text_[24] = "-";  // cached estimate, recomputed every ~5 s
  uint32_t batt_runtime_calc_ms_ = 0;

  // Low-battery cutoff -> deep sleep (handler provided by the device).
  std::function<void()> batt_low_handler_;
  std::function<void()> restart_handler_;  // Settings > Device "Restart" (device only)
  std::function<void()> reboot_handler_;   // full reboot after a settings restore
  float batt_cutoff_volts_ = 0.0f;
  int batt_low_count_ = 0;  // consecutive sub-cutoff reads (debounce)

  // Inferred Heating state (see core::derive_heating); doubles as the
  // hysteresis "previous result" bit.
  bool heating_ = false;
  // Warm-up-complete latch behind the ready chime. A member (not a local) so
  // it survives refreshes AND theme rebuilds — its whole job is remembering
  // that this warm-up has already been announced.
  core::ReadyChime ready_chime_;
  core::SteamReadyChime steam_ready_;  // steam switched on later in a session
  // Event sounds (core::CueGroup switches, cached from IDisplaySettings).
  bool shot_sounds_on_ = true;
  bool milk_sounds_on_ = true;
  bool alert_sounds_on_ = true;
  // play_cue: through the cue's group switch, at the chime volume (0 = all of
  // them silent), `scale_pct` of it for the small ticks.
  void play_cue(core::Cue cue, int scale_pct = 100);
  void link_sounds_poll();             // refresh: scale/machine link edges
  bool scale_link_known_ = false, scale_link_was_ = false;
  bool machine_link_known_ = false, machine_link_was_ = false;

  // Shot-history view state. shot_view_ backs the open shot-card modal (the
  // card's graph paints from it on every redraw, so it must outlive the
  // modal); shot_capture_ stages save(). ~7 KB each — allocated from the
  // LVGL pool (PSRAM on device) in build(), NOT members: as members of a
  // global App they'd sit in internal .bss, and internal RAM is the S3
  // boards' scarcest resource. The built-rows fingerprint avoids rebuilding
  // the list every refresh tick.
  core::ShotRecord* shot_view_ = nullptr;
  core::ShotRecord* shot_capture_ = nullptr;
  int hist_built_count_ = -1;   // count() the rows were last built from
  int hist_built_filter_ = -1;  // filter the rows were last built from
  uint32_t pending_delete_id_ = 0;  // shot the delete-confirm modal is about
};

}  // namespace ui
