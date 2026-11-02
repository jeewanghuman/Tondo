#pragma once

#include "core/display_settings.h"

// Host stand-in so the Display settings render in the simulator.

namespace host {

class FakeDisplaySettings : public core::IDisplaySettings {
 public:
  int brightness() const override { return brightness_; }
  void set_brightness(int percent) override { brightness_ = percent; }
  bool supports_brightness() const override { return true; }
  int screen_timeout_min() const override { return screen_timeout_min_; }
  void set_screen_timeout_min(int minutes) override { screen_timeout_min_ = minutes; }
  void set_screensaver(SaverMode) override {}  // no backlight to dim in the sim
  void set_backlight_live(int) override {}
  int screensaver_style() const override { return screensaver_style_; }
  void set_screensaver_style(int style) override { screensaver_style_ = style; }
  int theme() const override { return theme_; }
  void set_theme(int index) override { theme_ = index; }
  bool use_fahrenheit() const override { return fahrenheit_; }
  void set_use_fahrenheit(bool on) override { fahrenheit_ = on; }
  bool drop_negative_flow() const override { return drop_negative_flow_; }
  void set_drop_negative_flow(bool on) override { drop_negative_flow_ = on; }
  bool scope_graph() const override { return scope_graph_; }
  void set_scope_graph(bool on) override { scope_graph_ = on; }
  int flow_smooth() const override { return flow_smooth_; }
  void set_flow_smooth(int level) override { flow_smooth_ = level; }
  int shot_window_growth() const override { return shot_window_growth_; }
  void set_shot_window_growth(int mode) override { shot_window_growth_ = mode; }
  bool perf_overlay() const override { return perf_overlay_; }
  void set_perf_overlay(bool on) override { perf_overlay_ = on; }
  bool click_sound() const override { return click_sound_; }
  void set_click_sound(bool on) override { click_sound_ = on; }
  int ready_chime_volume() const override { return ready_chime_volume_; }
  void set_ready_chime_volume(int percent) override { ready_chime_volume_ = percent; }
  int ready_chime_melody() const override { return ready_chime_melody_; }
  void set_ready_chime_melody(int melody) override { ready_chime_melody_ = melody; }
  int milk_drink() const override { return milk_drink_; }
  void set_milk_drink(int index) override { milk_drink_ = index; }
  int milk_type() const override { return milk_type_; }
  void set_milk_type(int index) override { milk_type_ = index; }
  int milk_ml(int drink) const override {
    return (drink >= 0 && drink < 10) ? milk_ml_[drink] : 0;
  }
  void set_milk_ml(int drink, int ml) override {
    if (drink >= 0 && drink < 10) milk_ml_[drink] = ml;
  }
  bool shot_sounds() const override { return shot_sounds_; }
  void set_shot_sounds(bool on) override { shot_sounds_ = on; }
  bool milk_sounds() const override { return milk_sounds_; }
  void set_milk_sounds(bool on) override { milk_sounds_ = on; }
  bool alert_sounds() const override { return alert_sounds_; }
  void set_alert_sounds(bool on) override { alert_sounds_ = on; }

 private:
  int brightness_ = 80;
  int screen_timeout_min_ = 30;  // matches the device default
  int screensaver_style_ = 0;    // kSaverLion, the device default
  int theme_ = 0;
  bool fahrenheit_ = false;
  bool drop_negative_flow_ = true;
  bool scope_graph_ = true;  // matches the device default
  int flow_smooth_ = 1;      // light
  int shot_window_growth_ = 2;  // continuous, the device default
  bool perf_overlay_ = false;
  bool click_sound_ = true;
  int ready_chime_volume_ = 50;  // matches the device default
  int ready_chime_melody_ = 1;   // 1 = Blue (device default)
  int milk_drink_ = -1;          // never picked: Latte, whole milk, presets
  int milk_type_ = -1;
  int milk_ml_[10] = {};
  bool shot_sounds_ = true;  // event sounds: on, like the device
  bool milk_sounds_ = true;
  bool alert_sounds_ = true;
};

}  // namespace host
