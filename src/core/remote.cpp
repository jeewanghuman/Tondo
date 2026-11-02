#include "core/remote.h"

#include <cstdio>
#include <cstring>

namespace core {

namespace {

constexpr const char* kKeys[kRemoteSettingCount] = {
    "brightness",  "screen_timeout", "idle_screen", "fahrenheit",
    "clock_24h",   "click_sound",    "shot_sounds", "milk_sounds",
    "alert_sounds", "chime_volume",  "chime_melody", "target_g",
    "auto_flush",  "flush_delay",    "review_hold", "update_cadence",
    "beta_updates",
};

}  // namespace

const char* remote_setting_key(RemoteSetting s) {
  const int i = static_cast<int>(s);
  return (i >= 0 && i < kRemoteSettingCount) ? kKeys[i] : "";
}

bool remote_setting_from_key(const char* key, RemoteSetting* out) {
  if (key == nullptr) return false;
  for (int i = 0; i < kRemoteSettingCount; ++i) {
    if (std::strcmp(key, kKeys[i]) == 0) {
      if (out != nullptr) *out = static_cast<RemoteSetting>(i);
      return true;
    }
  }
  return false;
}

void RemoteBridge::publish(const RemoteLive& s) {
  std::lock_guard<std::mutex> lk(mutex_);
  const uint32_t seq = live_.seq + 1;
  live_ = s;
  live_.seq = seq == 0 ? 1 : seq;
}

void RemoteBridge::set_update_notes(const std::string& notes) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (notes_ != notes) notes_ = notes;
}

void RemoteBridge::shot_begin() {
  std::lock_guard<std::mutex> lk(mutex_);
  ++shot_id_;
  n_samples_ = 0;
}

void RemoteBridge::shot_sample(uint32_t t_ms, float grams) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (n_samples_ >= kShotCap) return;
  if (n_samples_ > 0 && t_ms < samples_[n_samples_ - 1].t_ms + kSampleSpacingMs)
    return;
  samples_[n_samples_++] = ShotSample{t_ms, grams, 0.0f};
}

bool RemoteBridge::take(RemoteCmd* out) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (q_len_ == 0) return false;
  if (out != nullptr) *out = queue_[q_head_];
  q_head_ = (q_head_ + 1) % kQueueDepth;
  --q_len_;
  return true;
}

RemoteLive RemoteBridge::live() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return live_;
}

std::string RemoteBridge::update_notes() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return notes_;
}

int RemoteBridge::shot_samples(int from, ShotSample* out, int max,
                               uint32_t* shot_id, int* total) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if (shot_id != nullptr) *shot_id = shot_id_;
  if (total != nullptr) *total = n_samples_;
  if (from < 0) from = 0;
  int n = 0;
  for (int i = from; i < n_samples_ && n < max; ++i) out[n++] = samples_[i];
  return n;
}

bool RemoteBridge::post(const RemoteCmd& c) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (q_len_ >= kQueueDepth) return false;
  queue_[(q_head_ + q_len_) % kQueueDepth] = c;
  ++q_len_;
  return true;
}

size_t json_escape(const char* in, char* out, size_t n) {
  if (n == 0) return 0;
  size_t w = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in);
       in != nullptr && *p != '\0'; ++p) {
    char esc[8];
    size_t len = 0;
    const unsigned char c = *p;
    if (c == '"' || c == '\\') {
      esc[0] = '\\';
      esc[1] = static_cast<char>(c);
      len = 2;
    } else if (c == '\n') {
      std::memcpy(esc, "\\n", 2);
      len = 2;
    } else if (c < 0x20 || c == 0x7f) {
      len = static_cast<size_t>(std::snprintf(esc, sizeof(esc), "\\u%04x", c));
    } else if (c >= 0x80) {
      // A whole UTF-8 sequence or nothing, so a cut never leaves half a
      // character behind (browsers would show a replacement glyph).
      size_t seq = (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
      size_t have = 1;
      while (have < seq && (p[have] & 0xc0) == 0x80) ++have;
      if (have != seq) {  // malformed: drop the lead byte
        continue;
      }
      if (w + seq + 1 > n) break;
      std::memcpy(out + w, p, seq);
      w += seq;
      p += seq - 1;
      continue;
    } else {
      esc[0] = static_cast<char>(c);
      len = 1;
    }
    if (w + len + 1 > n) break;
    std::memcpy(out + w, esc, len);
    w += len;
  }
  out[w] = '\0';
  return w;
}

}  // namespace core
