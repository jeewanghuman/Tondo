#pragma once

#include <string>

#include "core/network.h"

namespace host {

// Canned "connected to home WiFi" network for sim renders: reports a fixed SSID +
// IP so the Home WiFi glyph and the Settings WiFi section render in their normal
// state. Setters just remember, so previews can vary.
class FakeNetwork : public core::INetwork {
 public:
  core::NetState status() const override {
    return enabled_ ? core::NetState::Connected : core::NetState::Disabled;
  }
  const char* ssid() const override { return "HomeWiFi"; }
  const char* ip() const override { return enabled_ ? "192.168.1.42" : ""; }

  bool enabled() const override { return enabled_; }
  void set_enabled(bool on) override { enabled_ = on; }

  void begin_setup_portal() override {}
  void stop_setup_portal() override {}
  const char* setup_ssid() const override { return "Micra-Setup"; }
  const char* setup_url() const override { return "http://192.168.4.1"; }
  void forget() override {}

  const char* timezone() const override { return tz_.c_str(); }
  void set_timezone(const char* tz) override { tz_ = tz; }
  const char* ntp_server() const override { return ntp_.c_str(); }
  void set_ntp_server(const char* host) override { ntp_ = host; }
  bool ntp_enabled() const override { return ntp_en_; }
  void set_ntp_enabled(bool on) override { ntp_en_ = on; }
  int ntp_seconds_since_sync() const override { return ntp_since_s_; }
  const char* web_name() const override { return "tondo"; }
  const char* web_pin() const override { return pin_; }
  void new_web_pin() override { pin_[0] = pin_[0] == '9' ? '1' : static_cast<char>(pin_[0] + 1); }
  // Sim-only: pose an NTP sync (>= 0 = seconds since; -1 = none this session).
  void set_ntp_seconds_since_sync(int s) { ntp_since_s_ = s; }

 private:
  char pin_[5] = "4821";
  bool enabled_ = true;
  bool ntp_en_ = true;
  int ntp_since_s_ = -1;  // never synced: the previews show the manual pickers
  std::string tz_ = "AEST-10AEDT,M10.1.0,M4.1.0";  // Sydney, for previews
  std::string ntp_ = "pool.ntp.org";
};

}  // namespace host
