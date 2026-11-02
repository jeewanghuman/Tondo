#pragma once

// The phone's view of the machine: a mailbox between the web server and the
// UI loop (RemoteBridge).
//
// IMPORTANT: stays free of LVGL/Arduino/BLE/SDL like the other core headers.
//
// WHY A MAILBOX. On the P4 the web server runs in its own FreeRTOS task, while
// App, LVGL and the BrewController are single-threaded on the UI loop. So the
// web side never calls them. Instead:
//   - App::refresh() PUBLISHES a flat snapshot (RemoteLive) every tick, and
//     pump_scale_chart() appends the running shot's samples, at most one per
//     100 ms. The web task only ever reads copies.
//   - The web task POSTS commands (RemoteCmd). App drains them each refresh
//     and applies each one through the same paths as the touch controls, so
//     the screen and the phone can never disagree.
// One std::mutex guards everything; every critical section is a copy.
//
// NEVER A SHOT. No command starts the group, a flush or a backflush: the
// machine must only run with someone standing at it. A shot can't be started
// from here by construction, because there is no command kind for it.

#include <cstdint>
#include <mutex>
#include <string>

#include "core/brew.h"
#include "core/machine.h"
#include "core/shot_store.h"

namespace core {

// The settings the phone can change. Keys are the wire names (/api/settings);
// the order here is internal only: nothing stores these numbers.
enum class RemoteSetting : uint8_t {
  kBrightness,     // 10..100, steps of 10
  kScreenTimeout,  // minutes: 0 (never), 1, 5, 15, 30
  kIdleScreen,     // IDisplaySettings::SaverStyle
  kFahrenheit,     // bool
  kClock24h,       // bool
  kClickSound,     // bool
  kShotSounds,     // bool
  kMilkSounds,     // bool
  kAlertSounds,    // bool
  kChimeVolume,    // 0..100
  kChimeMelody,    // 0 = off, 1..ready_melody_count(), kReadyMelodyRandom
  kTargetG,        // the shot's target weight, 5..120 g
  kAutoFlush,      // seconds: 0 (off), 3, 6, 9, 15
  kFlushDelay,     // seconds: 3, 6, 9, 15
  kReviewHold,     // seconds, 5..120 in steps of 5
  kUpdateCadence,  // 0 off, 1 on boot, 2 daily, 3 hourly
  kBetaUpdates,    // bool
  kCount
};
inline constexpr int kRemoteSettingCount = static_cast<int>(RemoteSetting::kCount);

const char* remote_setting_key(RemoteSetting s);
bool remote_setting_from_key(const char* key, RemoteSetting* out);

enum class RemoteCmdKind : uint8_t {
  kPower,         // value: 1 = on, 0 = standby
  kBrewTemp,      // value: set point, C (clamped by App)
  kSteamEnabled,  // value: 1 / 0
  kSteamLevel,    // value: 0..2
  kSetting,       // setting + value
  kCheckUpdate,   // run a live update check
  kInstall,       // install the update the last check found
  kReboot,        // restart, after the phone restored settings into NVS
};

struct RemoteCmd {
  RemoteCmdKind kind = RemoteCmdKind::kPower;
  RemoteSetting setting = RemoteSetting::kBrightness;
  float value = 0;
};

// Everything the Live and Settings pages show. Flat and copyable.
struct RemoteLive {
  uint32_t seq = 0;  // bumped by every publish (0 = the UI hasn't run yet)
  // Machine.
  Link link = Link::Unconfigured;
  bool on = false;
  bool heating = false;  // a boiler still short of its set point
  bool brewing = false;  // the machine reports a shot in progress
  float brew_c = 0, brew_set = 0;
  float steam_c = 0, steam_set = 0;
  bool steam_on = false;
  int steam_level = 0;  // 0..2 (index into kSteamLevelsC)
  // Scale.
  bool scale_paired = false, scale_connected = false;
  float weight_g = 0;
  int scale_batt = -1;  // percent, -1 = unknown
  // Shot.
  ShotPhase phase = ShotPhase::kIdle;
  ShotMode mode = ShotMode::kManual;
  uint32_t shot_ms = 0;
  float shot_g = 0;  // the shot's grams, as Home shows them
  float target_g = 0;
  // Updates.
  bool update_available = false, update_checking = false;
  bool update_check_ok = false;
  int update_check_seq = 0;
  char update_version[24] = "";
  // Board features the Settings page needs.
  bool can_dim = false, has_sound = false;
  // Current value of every RemoteSetting, by index.
  float settings[kRemoteSettingCount] = {};
};

class RemoteBridge {
 public:
  static constexpr int kQueueDepth = 8;
  static constexpr int kShotCap = ShotRecord::kSampleCap;  // 60 s at 10 Hz
  static constexpr uint32_t kSampleSpacingMs = 100;

  // UI loop.
  void publish(const RemoteLive& s);
  void set_update_notes(const std::string& notes);
  void shot_begin();  // a new shot: clears the series, bumps shot_id()
  void shot_sample(uint32_t t_ms, float grams);  // thinned to kSampleSpacingMs
  bool take(RemoteCmd* out);  // next posted command, oldest first

  // Web side.
  RemoteLive live() const;
  std::string update_notes() const;
  // Copies up to `max` samples starting at index `from` of the current shot.
  // Returns how many; `shot_id` / `total` describe the series so a poller can
  // ask only for what it hasn't seen (and start over when the id changes).
  int shot_samples(int from, ShotSample* out, int max, uint32_t* shot_id,
                   int* total) const;
  bool post(const RemoteCmd& c);  // false when the queue is full

 private:
  mutable std::mutex mutex_;
  RemoteLive live_;
  std::string notes_;
  uint32_t shot_id_ = 0;
  int n_samples_ = 0;
  ShotSample samples_[kShotCap];
  RemoteCmd queue_[kQueueDepth];
  int q_head_ = 0, q_len_ = 0;
};

// JSON string body for `in` (no surrounding quotes): escapes quotes,
// backslashes and control characters; truncates to fit `n` (always
// terminated, never splits an escape). Returns the length written.
size_t json_escape(const char* in, char* out, size_t n);

}  // namespace core
