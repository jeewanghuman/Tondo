#include "platform_esp32/config.h"

#include "core/display_settings.h"

#include <Preferences.h>
#include <cmath>
#include <cstdio>

// NVS is initialized by the Arduino-ESP32 core at boot, so we just open the
// namespace per call (cheap, and avoids holding the handle open).
namespace {
constexpr const char* kNamespace = platform::kConfigNamespace;  // see config.h
constexpr char kMacKey[] = "mac";
constexpr char kNameKey[] = "name";
constexpr char kTokenKey[] = "token";
constexpr char kBrightnessKey[] = "bright";
constexpr char kScreenTimeoutKey[] = "scrtimeout";
constexpr char kSaverStyleKey[] = "ssstyle";   // legacy: 0=Logo, 1=Blank
constexpr char kSaverStyle2Key[] = "ssstyle2";  // 0=Logo, 1=Dim, 2=Off
// Logo artwork: 0=Lion. 1=Apollo and 2=Alternate are RETIRED (removed in this
// fork, 2026-10-26): this build never reads the key and only ever writes 0.
// Never reuse 1 or 2 -- older builds still show those two for them.
constexpr char kSaverArtKey[] = "ssart";
// The Clock idle screen (round glass). Its OWN key, not a new ssstyle2/ssart
// value: older builds index those into fixed arrays (NVS add-only rule). A
// build that predates it never reads it, and with ssstyle2 = Logo and ssart
// untouched it bounces whatever artwork was chosen before -- a sane rollback.
constexpr char kSaverClockKey[] = "ssclock";
constexpr char kSkippedUpdateKey[] = "skipver";
constexpr char kPendingInstallKey[] = "otainst";  // boot-flag install target version
constexpr char kUpdateCheckKey[] = "updchk";   // legacy bool (pre-mode)
constexpr char kUpdateModeKey[] = "updmode";
// Beta channel opt-in. A NEW key rather than a new update_check_mode value on
// purpose: an older firmware (an OTA rollback puts one back unattended) indexes
// its stored cadence straight into a fixed array, so widening that key's range
// would be an out-of-bounds read in a binary we can no longer patch. An unknown
// key, by contrast, is simply never read. Only ever ADD keys here.
constexpr char kBetaChannelKey[] = "updbeta";
constexpr char kClock24Key[] = "clock24";
constexpr char kThemeKey[] = "theme";
constexpr char kFahrenheitKey[] = "fahr";
constexpr char kDropNegFlowKey[] = "dropnegf";
constexpr char kScopeGraphKey[] = "scopegraph";
constexpr char kPerfOverlayKey[] = "perfovl";
constexpr char kClickSoundKey[] = "clicksnd";
constexpr char kReadyChimeKey[] = "rdychime";    // legacy on/off, migrated below
constexpr char kReadyChimeVolKey[] = "rdychimev";  // legacy LINEAR-AMPLITUDE %
constexpr char kReadyChimeUiKey[] = "rdychimeu";   // current: perceptual 0-100
constexpr char kReadyMelodyKey[] = "rdymel";     // warm-up tune: 0 off, 1.. melody
// Milk sheet: indices into core/milk.h's APPEND-ONLY tables, and one ml key
// per drink ("milkml0".."milkml9" -- the drink index is its last digit).
constexpr char kMilkDrinkKey[] = "milkdrk";
constexpr char kMilkTypeKey[] = "milktyp";
constexpr char kMilkMlKeyPrefix[] = "milkml";
constexpr char kShotSoundsKey[] = "sndshot";    // event-sound switches (bools,
constexpr char kMilkSoundsKey[] = "sndmilk";    // default on)
constexpr char kAlertSoundsKey[] = "sndalert";
// Out of the box: loud enough to carry, quiet enough that the first warm-up
// after a flash doesn't startle anyone. 80 on the perceptual scale is the same
// loudness the old linear 50 % produced (see migrate_chime_volume).
constexpr int kReadyChimeDefaultVol = 80;
// The dB span the volume setting covers — MUST match kChimeRangeDb in
// sound.cpp, which is what actually applies the curve. Only used to convert
// pre-v0.13 stored values.
constexpr float kReadyChimeRangeDb = 30.0f;

// Convert a legacy LINEAR-amplitude percent to the perceptual 0-100 scale that
// replaced it, preserving how loud the device actually was: the old value was
// used as raw amplitude, so its level in dB is 20*log10(old/100), and the new
// scale is that dB span mapped linearly onto 0-100.
int migrate_chime_volume(int legacy_percent) {
  if (legacy_percent <= 0) return 0;
  if (legacy_percent >= 100) return 100;
  const float db = 20.0f * std::log10(legacy_percent / 100.0f);
  const int v = static_cast<int>(lroundf(100.0f + db / kReadyChimeRangeDb * 100.0f));
  return v < 1 ? 1 : v > 100 ? 100 : v;  // never silence a device that wasn't
}
constexpr char kScaleMacKey[] = "smac";
constexpr char kScaleNameKey[] = "sname";
constexpr char kTargetKey[] = "tgtg";
constexpr char kShotModeKey[] = "shotmode";  // LEGACY bool (armed) — migration only
constexpr char kShotMode3Key[] = "shotmd";   // tri-state core::ShotMode as int
constexpr char kOvershootKey[] = "ovshoot";
constexpr char kHintOvershootKey[] = "hintovs";  // unwired stop-hint lag (separate: includes reaction time)
constexpr char kLastUnixKey[] = "lastunix";
constexpr char kReviewHoldKey[] = "revhold";
constexpr char kAutoConnectKey[] = "autoconn";
constexpr char kWiredPaddleKey[] = "wiredpad";
constexpr char kFlushKey[] = "flushs";  // post-shot auto-flush seconds (0 = off)
constexpr char kPadSenseKey[] = "padsense";  // per-UNIT paddle sense GPIO override
constexpr char kPadDriveKey[] = "paddrive";  // per-UNIT paddle drive GPIO override
constexpr char kFlushDelayKey[] = "flushd";  // cup-off -> flush pause seconds
constexpr char kLeadInKey[] = "leadin";      // detect-mode preinfusion lead-in seconds
constexpr char kFlowSmoothKey[] = "flowsmth";
constexpr char kShotXGrowKey[] = "shotxgrow";  // shot-graph X-window growth style
constexpr char kWifiEnKey[] = "wifi_en";
constexpr char kWifiSsidKey[] = "ssid";
constexpr char kWifiPassKey[] = "wifipass";
constexpr char kTzKey[] = "tz";
constexpr char kNtpKey[] = "ntp";
constexpr char kNtpEnKey[] = "ntp_en";
constexpr char kWebNameKey[] = "webname";
constexpr char kWebPinKey[] = "webpin";
}  // namespace

namespace platform {

// Read a string key, returning "" if the namespace or key doesn't exist yet —
// without the ERROR log Preferences::getString() emits on a missing key.
namespace {
std::string read_key(const char* key) {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return "";  // namespace absent
  std::string out;
  if (p.isKey(key)) out = p.getString(key, "").c_str();
  p.end();
  return out;
}
}  // namespace

void Config::begin() {
  // Opening read-write creates the namespace if absent, so later read-only
  // begins (brightness/theme/etc.) don't log "nvs_open failed: NOT_FOUND" on a
  // fresh device. The sentinel key guarantees the namespace persists.
  Preferences p;
  if (p.begin(kNamespace, /*readOnly=*/false)) {
    if (!p.isKey("_init")) p.putBool("_init", true);
    p.end();
  }
}

std::string Config::mac() const { return read_key(kMacKey); }

std::string Config::name() const { return read_key(kNameKey); }

std::string Config::token() const { return read_key(kTokenKey); }

void Config::save(const std::string& mac, const std::string& name) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kMacKey, mac.c_str());
  p.putString(kNameKey, name.c_str());
  p.end();
}

void Config::set_token(const std::string& token) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kTokenKey, token.c_str());
  p.end();
}

void Config::clear() {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.remove(kMacKey);
  p.remove(kNameKey);
  p.remove(kTokenKey);  // wipe the secret too
  p.end();
}

std::string Config::scale_mac() const { return read_key(kScaleMacKey); }

std::string Config::scale_name() const { return read_key(kScaleNameKey); }

void Config::save_scale(const std::string& mac, const std::string& name) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kScaleMacKey, mac.c_str());
  p.putString(kScaleNameKey, name.c_str());
  p.end();
}

void Config::clear_scale() {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.remove(kScaleMacKey);
  p.remove(kScaleNameKey);
  p.end();
}

float Config::target_weight_g() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 36.0f;
  const float v = p.isKey(kTargetKey) ? p.getFloat(kTargetKey, 36.0f) : 36.0f;
  p.end();
  return v;
}

int64_t Config::last_unix() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 0;
  const int64_t v = p.isKey(kLastUnixKey) ? p.getLong64(kLastUnixKey, 0) : 0;
  p.end();
  return v;
}

void Config::set_last_unix(int64_t t) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putLong64(kLastUnixKey, t);
  p.end();
}

float Config::overshoot_g() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 2.0f;
  const float v = p.isKey(kOvershootKey) ? p.getFloat(kOvershootKey, 2.0f) : 2.0f;
  p.end();
  return v;
}

void Config::set_overshoot_g(float grams) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putFloat(kOvershootKey, grams);
  p.end();
}

float Config::hint_overshoot_g() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 2.0f;
  const float v = p.isKey(kHintOvershootKey) ? p.getFloat(kHintOvershootKey, 2.0f) : 2.0f;
  p.end();
  return v;
}

void Config::set_hint_overshoot_g(float grams) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putFloat(kHintOvershootKey, grams);
  p.end();
}

int Config::flow_smooth() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 1;
  const int v = p.isKey(kFlowSmoothKey) ? p.getInt(kFlowSmoothKey, 1) : 1;
  p.end();
  return v;
}

void Config::set_flow_smooth(int level) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kFlowSmoothKey, level);
  p.end();
}

int Config::shot_window_growth() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 2;
  const int v = p.isKey(kShotXGrowKey) ? p.getInt(kShotXGrowKey, 2) : 2;
  p.end();
  return v;
}

void Config::set_shot_window_growth(int mode) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kShotXGrowKey, mode);
  p.end();
}

bool Config::auto_connect() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kAutoConnectKey) ? p.getBool(kAutoConnectKey, true) : true;
  p.end();
  return v;
}

void Config::set_auto_connect(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kAutoConnectKey, on);
  p.end();
}

bool Config::wired_paddle() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.isKey(kWiredPaddleKey) ? p.getBool(kWiredPaddleKey, false) : false;
  p.end();
  return v;
}

void Config::set_wired_paddle(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kWiredPaddleKey, on);
  p.end();
}

int Config::flush_s() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 0;
  const int v = p.isKey(kFlushKey) ? p.getInt(kFlushKey, 0) : 0;
  p.end();
  return v;
}

void Config::set_flush_s(int seconds) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kFlushKey, seconds);
  p.end();
}

int Config::paddle_sense_pin() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return -1;
  const int v = p.isKey(kPadSenseKey) ? p.getInt(kPadSenseKey, -1) : -1;
  p.end();
  return v;
}

void Config::set_paddle_sense_pin(int pin) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  if (pin < 0) {
    p.remove(kPadSenseKey);
  } else {
    p.putInt(kPadSenseKey, pin);
  }
  p.end();
}

int Config::paddle_drive_pin() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return -1;
  const int v = p.isKey(kPadDriveKey) ? p.getInt(kPadDriveKey, -1) : -1;
  p.end();
  return v;
}

void Config::set_paddle_drive_pin(int pin) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  if (pin < 0) {
    p.remove(kPadDriveKey);
  } else {
    p.putInt(kPadDriveKey, pin);
  }
  p.end();
}

int Config::flush_delay_s() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 3;
  const int v = p.isKey(kFlushDelayKey) ? p.getInt(kFlushDelayKey, 3) : 3;
  p.end();
  return v;
}

void Config::set_flush_delay_s(int seconds) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kFlushDelayKey, seconds);
  p.end();
}

int Config::review_hold_s() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 30;
  const int v = p.isKey(kReviewHoldKey) ? p.getInt(kReviewHoldKey, 30) : 30;
  p.end();
  return v;
}

void Config::set_review_hold_s(int seconds) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kReviewHoldKey, seconds);
  p.end();
}

int Config::detect_lead_in_s() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 3;
  const int v = p.isKey(kLeadInKey) ? p.getInt(kLeadInKey, 3) : 3;
  p.end();
  return v;
}

void Config::set_detect_lead_in_s(int seconds) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kLeadInKey, seconds);
  p.end();
}

int Config::shot_mode() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 2;  // kDetect
  int v;
  if (p.isKey(kShotMode3Key)) {
    v = p.getInt(kShotMode3Key, 2);
  } else {
    // Migrate the legacy bool: armed meant kAuto on a wired rig, kDetect
    // otherwise; off meant kManual. Written back on the next set_shot_mode.
    const bool armed = p.isKey(kShotModeKey) ? p.getBool(kShotModeKey, true) : true;
    const bool wired = p.isKey(kWiredPaddleKey) ? p.getBool(kWiredPaddleKey, false) : false;
    v = armed ? (wired ? 1 : 2) : 0;
  }
  p.end();
  return (v < 0 || v > 2) ? 2 : v;
}

void Config::set_shot_mode(int mode) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kShotMode3Key, mode);
  p.end();
}

void Config::set_target_weight_g(float grams) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putFloat(kTargetKey, grams);
  p.end();
}

int Config::brightness() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 100;
  const int v = p.isKey(kBrightnessKey) ? p.getInt(kBrightnessKey, 100) : 100;
  p.end();
  return v;
}

void Config::set_brightness(int percent) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kBrightnessKey, percent);
  p.end();
}

int Config::screen_timeout_min() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 30;
  const int v = p.isKey(kScreenTimeoutKey) ? p.getInt(kScreenTimeoutKey, 30) : 30;
  p.end();
  return v;
}

void Config::set_screen_timeout_min(int minutes) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kScreenTimeoutKey, minutes);
  p.end();
}

std::string Config::web_name() const {
  const std::string n = read_key(kWebNameKey);
  return n.empty() ? "tondo" : n;
}

void Config::set_web_name(const std::string& name) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kWebNameKey, name.c_str());
  p.end();
}

std::string Config::web_pin() const { return read_key(kWebPinKey); }

void Config::set_web_pin(const std::string& pin) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kWebPinKey, pin.c_str());
  p.end();
}

std::string Config::skipped_update() const { return read_key(kSkippedUpdateKey); }

void Config::set_skipped_update(const std::string& version) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kSkippedUpdateKey, version.c_str());
  p.end();
}

int Config::update_check_mode() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 1;
  int v = 1;  // default: boot-time only
  if (p.isKey(kUpdateModeKey)) {
    v = p.getInt(kUpdateModeKey, 1);
  } else if (p.isKey(kUpdateCheckKey)) {
    // Migrate the short-lived boolean: off stays off, on becomes boot-only.
    v = p.getBool(kUpdateCheckKey, true) ? 1 : 0;
  }
  p.end();
  return v;
}

void Config::set_update_check_mode(int mode) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kUpdateModeKey, mode);
  p.end();
}

bool Config::beta_channel() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.getBool(kBetaChannelKey, false);
  p.end();
  return v;
}

void Config::set_beta_channel(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kBetaChannelKey, on);
  p.end();
}

std::string Config::pending_install() const { return read_key(kPendingInstallKey); }

void Config::set_pending_install(const std::string& version) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kPendingInstallKey, version.c_str());
  p.end();
}

int Config::screensaver_style() const {
  // The UI's SaverStyle index (Lion/Dim/Off/Clock) is THREE keys here:
  //   ssstyle2  0 = Logo, 1 = Dim, 2 = Off -- every shipped build reads this,
  //             and its range is frozen (NVS add-only rule).
  //   ssart     which Logo artwork: 0 = Lion; 1 (Apollo) and 2 (Alternate)
  //             are retired, so this build ignores it (any value is Lion).
  //             Older builds still show them, so a rollback brings the old
  //             choice back.
  //   ssclock   Clock, riding on ssstyle2 = Logo (see kSaverClockKey).
  // "Dim" was inserted at ssstyle2's index 1 when the third style landed,
  // which would have flipped existing "Blank" users (legacy ssstyle index 1)
  // to Dim. So the value moved to ssstyle2 and the legacy key migrates: old 1
  // (Blank) -> 2 (Off). Same pattern as update_check_mode's bool->int move.
  using core::IDisplaySettings;
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return IDisplaySettings::kSaverLion;
  int style = 0;
  if (p.isKey(kSaverStyle2Key)) {
    style = p.getInt(kSaverStyle2Key, 0);
  } else if (p.isKey(kSaverStyleKey)) {
    style = p.getInt(kSaverStyleKey, 0) == 1 ? 2 : 0;
  }
  const bool clock = p.isKey(kSaverClockKey) && p.getBool(kSaverClockKey, false);
  p.end();
  if (style == 1) return IDisplaySettings::kSaverDim;
  if (style == 2) return IDisplaySettings::kSaverOff;
  if (clock) return IDisplaySettings::kSaverClock;  // Clock rides on "Logo"
  return IDisplaySettings::kSaverLion;  // whatever ssart holds: Lion is the only artwork
}

void Config::set_screensaver_style(int style) {
  using core::IDisplaySettings;
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  // The Clock flag is only ever true alongside ssstyle2 = Logo; every other
  // choice clears it.
  p.putBool(kSaverClockKey, style == IDisplaySettings::kSaverClock);
  if (style == IDisplaySettings::kSaverDim) {
    p.putInt(kSaverStyle2Key, 1);
  } else if (style == IDisplaySettings::kSaverOff) {
    p.putInt(kSaverStyle2Key, 2);
  } else if (style == IDisplaySettings::kSaverClock) {
    // Logo for every older build, which then bounces the remembered artwork.
    p.putInt(kSaverStyle2Key, 0);
  } else {
    // Lion: Logo for the old key, and the lion for builds that still have a
    // choice of artwork. Dim/Off/Clock leave ssart alone.
    p.putInt(kSaverStyle2Key, 0);
    p.putInt(kSaverArtKey, 0);
  }
  p.end();
}

bool Config::clock_24h() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kClock24Key) ? p.getBool(kClock24Key, true) : true;
  p.end();
  return v;
}

void Config::set_clock_24h(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kClock24Key, on);
  p.end();
}

int Config::theme() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 0;
  const int v = p.isKey(kThemeKey) ? p.getInt(kThemeKey, 0) : 0;
  p.end();
  return v;
}

void Config::set_theme(int index) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kThemeKey, index);
  p.end();
}

bool Config::use_fahrenheit() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.isKey(kFahrenheitKey) ? p.getBool(kFahrenheitKey, false) : false;
  p.end();
  return v;
}

void Config::set_use_fahrenheit(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kFahrenheitKey, on);
  p.end();
}

bool Config::drop_negative_flow() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kDropNegFlowKey) ? p.getBool(kDropNegFlowKey, true) : true;
  p.end();
  return v;
}

void Config::set_drop_negative_flow(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kDropNegFlowKey, on);
  p.end();
}

bool Config::scope_graph() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.isKey(kScopeGraphKey) ? p.getBool(kScopeGraphKey, true) : true;
  p.end();
  return v;
}

void Config::set_scope_graph(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kScopeGraphKey, on);
  p.end();
}

bool Config::perf_overlay() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.isKey(kPerfOverlayKey) ? p.getBool(kPerfOverlayKey, false) : false;
  p.end();
  return v;
}

void Config::set_perf_overlay(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kPerfOverlayKey, on);
  p.end();
}

bool Config::click_sound() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kClickSoundKey) ? p.getBool(kClickSoundKey, true) : true;
  p.end();
  return v;
}

void Config::set_click_sound(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kClickSoundKey, on);
  p.end();
}

int Config::ready_chime_volume() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return kReadyChimeDefaultVol;
  int v = kReadyChimeDefaultVol;
  if (p.isKey(kReadyChimeUiKey)) {
    v = p.getInt(kReadyChimeUiKey, kReadyChimeDefaultVol);
  } else if (p.isKey(kReadyChimeVolKey)) {
    // Migrate the linear-amplitude percent this setting used before the scale
    // became perceptual: convert so the chime stays exactly as loud as it was.
    v = migrate_chime_volume(p.getInt(kReadyChimeVolKey, 50));
  } else if (p.isKey(kReadyChimeKey)) {
    // Migrate the on/off key this setting shipped as first: off stays off, on
    // keeps the full level it used to play at rather than dropping to the new
    // default — migration preserves what a device was doing.
    v = p.getBool(kReadyChimeKey, true) ? 100 : 0;
  }
  p.end();
  return v < 0 ? 0 : v > 100 ? 100 : v;
}

void Config::set_ready_chime_volume(int percent) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kReadyChimeUiKey, percent < 0 ? 0 : percent > 100 ? 100 : percent);
  p.end();
}

int Config::ready_chime_melody() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 1;
  const int v = p.isKey(kReadyMelodyKey) ? p.getInt(kReadyMelodyKey, 1) : 1;
  p.end();
  // Clamped generously here (the UI clamps to the real melody count) so a
  // stale value from a future firmware can't go negative.
  return v < 0 ? 0 : v;
}

void Config::set_ready_chime_melody(int melody) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kReadyMelodyKey, melody < 0 ? 0 : melody);
  p.end();
}

namespace {
int read_int(const char* key, int fallback) {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return fallback;
  const int v = p.isKey(key) ? p.getInt(key, fallback) : fallback;
  p.end();
  return v;
}

void write_int(const char* key, int v) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(key, v);
  p.end();
}

// "milkml<d>" for drink d, or false past the one-digit range.
bool milk_ml_key(int drink, char (&key)[12]) {
  if (drink < 0 || drink > 9) return false;
  std::snprintf(key, sizeof(key), "%s%d", kMilkMlKeyPrefix, drink);
  return true;
}

bool read_bool(const char* key, bool fallback) {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return fallback;
  const bool v = p.isKey(key) ? p.getBool(key, fallback) : fallback;
  p.end();
  return v;
}

void write_bool(const char* key, bool v) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(key, v);
  p.end();
}
}  // namespace

bool Config::shot_sounds() const { return read_bool(kShotSoundsKey, true); }
void Config::set_shot_sounds(bool on) { write_bool(kShotSoundsKey, on); }
bool Config::milk_sounds() const { return read_bool(kMilkSoundsKey, true); }
void Config::set_milk_sounds(bool on) { write_bool(kMilkSoundsKey, on); }
bool Config::alert_sounds() const { return read_bool(kAlertSoundsKey, true); }
void Config::set_alert_sounds(bool on) { write_bool(kAlertSoundsKey, on); }

int Config::milk_drink() const { return read_int(kMilkDrinkKey, -1); }
void Config::set_milk_drink(int index) { write_int(kMilkDrinkKey, index); }
int Config::milk_type() const { return read_int(kMilkTypeKey, -1); }
void Config::set_milk_type(int index) { write_int(kMilkTypeKey, index); }

int Config::milk_ml(int drink) const {
  char key[12];
  return milk_ml_key(drink, key) ? read_int(key, 0) : 0;
}

void Config::set_milk_ml(int drink, int ml) {
  char key[12];
  if (milk_ml_key(drink, key)) write_int(key, ml);
}

bool Config::wifi_enabled() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.isKey(kWifiEnKey) ? p.getBool(kWifiEnKey, false) : false;
  p.end();
  return v;
}

void Config::set_wifi_enabled(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kWifiEnKey, on);
  p.end();
}

std::string Config::wifi_ssid() const { return read_key(kWifiSsidKey); }

std::string Config::wifi_password() const { return read_key(kWifiPassKey); }

void Config::save_wifi(const std::string& ssid, const std::string& password) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kWifiSsidKey, ssid.c_str());
  p.putString(kWifiPassKey, password.c_str());  // plaintext, as ESP-IDF's own wifi store
  p.end();
}

void Config::clear_wifi() {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.remove(kWifiSsidKey);
  p.remove(kWifiPassKey);
  p.end();
}

std::string Config::timezone() const {
  const std::string tz = read_key(kTzKey);
  return tz.empty() ? "UTC0" : tz;
}

void Config::set_timezone(const std::string& tz) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kTzKey, tz.c_str());
  p.end();
}

std::string Config::ntp_server() const {
  const std::string host = read_key(kNtpKey);
  return host.empty() ? "pool.ntp.org" : host;
}

void Config::set_ntp_server(const std::string& host) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kNtpKey, host.c_str());
  p.end();
}

bool Config::ntp_enabled() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kNtpEnKey) ? p.getBool(kNtpEnKey, true) : true;
  p.end();
  return v;
}

void Config::set_ntp_enabled(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kNtpEnKey, on);
  p.end();
}

}  // namespace platform
