# Claude Code project notes

**Tondo** — firmware for a La Marzocco Micra BLE remote on Waveshare's round
ESP32-P4 3.4C (this fork); upstream is Apollo 2 (mlsorensen/Apollo2), which
targets rectangular Waveshare ESP32 touch boards. See "Fork releases" for
the name and what kept "Apollo".
Read README.md "Developer documentation" first — the architecture section is
accurate and the layering rules there are hard rules:

- `include/core/` + `src/core/` must stay free of LVGL/Arduino/NimBLE/SDL.
  The BLE protocol code (Micra link, Bookoo scale) lives in `src/core/` over
  the `core::ble::ICentral` port; only transports (`nimble_central.cpp`) are
  per-platform. New platforms (Pi/BlueZ, Pico/btstack) implement ICentral,
  never touch protocol code.
- The UI depends only on `core::` interfaces. It compiles unchanged on-device
  and in the host simulator (`make sim` → renders/*.png — the fastest way to
  check UI work).
- DOCS SYNC RULE: when features, settings, screens, or boards change, update
  in the same change: MANUAL.md prose + its "Where everything lives" tree +
  TOCs, docs/HARDWARE.md, docs/WIRING.md, README.md, and the screenshots.
  IN THIS FORK the user-facing docs describe the ROUND 3.4C only — its one
  released board (see "Fork releases"); upstream's boards are not documented
  here. Every screenshot is a round render: the sim also writes a clean copy
  of each round frame to renders/glass/, and `make docs-img`
  (tools/annotate_docs.py + docs/img/manual/manifest.json, `"round": true`
  entries) cuts it to the circle, scales it to 600 px and draws the
  callouts; the README's docs/img/*.png come from the same manifest
  (`"out": "../x.png"`). Callout boxes are in 800x800 render pixels —
  re-measure them if the layout moved (the script warns when a render's size
  changes). The Stats > Info shot carries the uptime, so it changes on every
  run; commit it only when the page itself changed.
- Board differences live in `include/platform_esp32/board_config.h` blocks +
  feature macros; driver code never hardcodes pins.

## OTA self-update (v0.11+)

Two separate parts: a CHECK/notify port (`update_check.*`) and an INSTALL that
runs in a dedicated early-boot mode (`install_mode.*`). Full rationale +
measurements: the `ota-p4-solution` memory. The short version of WHY install is
its own boot mode: a bulk download over the hosted-radio SDIO link needs a
~64 KB CONTIGUOUS internal-DMA buffer, and the app's graph-optimized DSI display
(use_dma2d + async double-buffer) fragments that pool to ~16 KB → the download
asserts `sdio_rx_get_buffer`; and flash writes disable the cache, stalling the
DSI's PSRAM framebuffer scan → the panel strobes. Both go away when the full
display isn't up.

- Artifact contract: `<site>/<tag>/firmware/app/<kUpdateSlug>.bin` (app-only
  image). The parts/ files are NOT usable for OTA — they bundle otadata with
  the app. CI asserts kUpdateSlug (board_config.h) == the matrix `board` field.
- Check (update_check.cpp): a short-lived task fetches releases.json + notes.txt
  via esp_http_client + `crt_bundle_attach` (the cert bundle in the core libs;
  the Arduino NetworkClientSecure wrapper CANNOT reach it). mbedTLS is pointed
  at PSRAM (use_psram_for_tls) so the small fetch runs with BLE up. Cadence Off/
  On boot/Daily (Config::update_check_mode). Notify only — never installs.
- Install (install_mode.cpp): "Install now" stashes the version in NVS
  (Config::pending_install) and reboots. VERY EARLY in setup() — before the full
  display / BLE / app — main runs install_mode::run(): a LIGHTWEIGHT display
  (display.cpp `install_panel_*`: same per-board DSI init but num_fbs=2, no
  use_dma2d, no async sync) shows a progress UI, downloads the whole image into
  PSRAM (no flash writes → clean), then blanks the backlight and writes
  PSRAM→flash (screen glitches during flash writes regardless) and reboots.
  Per display: DSI uses the light panel above; RGB (native-WiFi S3, no DMA-pool
  problem) uses the normal full Display and just blanks the backlight for the
  flash write. No-op only on the SPI 2-inch dev board (no real update UI there).
  Install mode inherits the RTC and only NTP-syncs as a failsafe — NOT for
  the certificate: the prebuilt core's mbedTLS has MBEDTLS_HAVE_TIME_DATE off,
  so cert validity dates are never checked (verified 2026-09-19); the sync is
  a "network really up" proof, like the check's SNTP gate.
- BETA CHANNEL (v0.14+): two indexes on the site. `releases.json` is
  RELEASES ONLY and its format is frozen — every fielded device reads it, and
  firmware older than the pre-release-aware parser reads "0.14.0-beta.1" as
  0.14.0 (plain sscanf takes the numbers and ignores the tail), so a beta in
  that file installs itself on stable devices. `releases-beta.json` merges
  releases + pre-releases, newest first, and is what a device with
  Config::beta_channel ("updbeta") fetches instead — ONE fetch either way, and
  a stable release newer than the last beta still wins. 404 on it falls back to
  the stable list. Ordering lives in two places that must agree:
  semver_newer() in update_check.cpp and order_key() in the workflow's pages
  job (a release outranks every beta of the same number; beta.2 outranks
  beta.1). Only `-beta.N` is understood — any other pre-release form parses as
  "not newer" and is ignored. PROMOTING a beta is a REBUILD at the real tag,
  never a copy of the artifacts: kVersion is compiled in and CI asserts it
  equals the tag, so a copied binary reports the beta version forever and
  re-prompts (the v0.12.3 failure). Betas accumulate on gh-pages (keep_files);
  prune by hand if the list ever gets silly.
- NVS RULE (settings compatibility): a release may only ADD keys to the `micra`
  namespace — never change the meaning, type or value RANGE of a key an older
  shipped build already reads. Every getter is isKey()-guarded with a default,
  so an unknown key is inert; but stored enums index fixed arrays (kCadence[]),
  so a widened value is an out-of-bounds read in a binary that can no longer be
  patched. This is not hypothetical for betas: rollback puts an older image
  back UNATTENDED. New semantics = new key + read the old one as a fallback
  (updchk->updmode, ssstyle->ssstyle2, rdychimev->rdychimeu).
- Rollback: BOOTLOADER_APP_ROLLBACK_ENABLE=y in all cores. main.cpp overrides
  `verifyRollbackLater()` -> true and marks the image valid after 60 s of
  healthy loop() — an image that bootloops is auto-reverted. Consequence:
  bootloader/partition-table changes CANNOT ship via OTA (app slot only).
- P4 16MB-FLASH-BOUNDARY RULE (found the hard way, see the
  `ota-p4-16mb-boundary` memory): the P4's 2nd-stage bootloader mis-reads flash
  ABOVE 0x1000000 (16MB) during its boot-time image verify (3-byte addressing
  wraps), so an OTA'd app that spills past 16MB is rejected as "invalid segment
  length" and rolls back — even though the flash content is byte-perfect (the
  write + the running app's reads are fine; only the bootloader's read wraps).
  FIX: `boards/p4_ota_under16.csv` (7MB app slots) keeps BOTH app slots entirely
  under 16MB (app1 ends ~14MB); all P4 envs use it via the base env. Never let a
  P4 app partition or its image cross 16MB. This is a partition-table change, so
  existing P4 units need a one-time web-flash to adopt it (then OTA works). S3
  boards are 16MB flash — no >16MB region, unaffected.
- C6 auto-update (c6_update.cpp): on every P4 boot, if the on-board ESP32-C6
  esp-hosted slave is older than the host lib, flash the embedded matching image
  (`firmware_blobs/c6_slave.bin`, committed, embedded on all P4 envs) to the C6
  over SDIO and reboot. Fixes the stale-factory-slave boot-WiFi delay. Bump the
  blob when the pinned platform's esp-hosted host version changes.
- Proving ground: the `esp32-p4-dltest` env (src/dltest/) is the standalone
  firmware the install mode was developed + verified in; keep it.

## Settings backup to the card (v0.14+)

Settings live in NVS, which does NOT travel with the SD card the shot history
is on — swap hardware and every shot survives while every preference is lost.
`Settings > Tondo > Backup` copies the whole `micra` namespace to
`/Tondo/settings.txt` and back. Shape of it:

- Port `core::ISettingsBackup` (info/request_backup/request_restore/state).
  The DEVICE `ShotStore` implements it as a second interface: the writer task
  already owns the mount and all its hard-won policy, so a settings file is
  just another job kind on that queue (`JobKind::kBackup/kRestore`). No second
  mount, no new task, no new mutex. The UI only sees the port.
- `config_backup.cpp` does the NVS<->file work: keys are ENUMERATED
  (`nvs_entry_find/next/info`), never listed by hand, so a setting added later
  is carried without anyone remembering this file. Type-tagged lines
  (`i32:bright=100`, `str:tz=...`, `blob:tgtg=<hex>` — Preferences stores
  floats as 4-byte blobs) so values round-trip exactly.
- RESTORE IS A REPLACE, not a merge: hold the per-unit keys, `nvs_erase_all`,
  apply the file. A key the file doesn't carry is then ABSENT and every
  isKey()-guarded getter reads its compiled default — which is what makes an
  older backup land exactly like an upgrade (the legacy fallbacks ssstyle ->
  ssstyle2 etc. do the rest). Then a full reboot (`App::set_reboot_handler` —
  the existing restart handler deliberately only resyncs an RGB panel).
- PER-UNIT KEYS never leave the board and survive the erase: `padsense`,
  `paddrive` (repair knobs for a damaged pad), `otainst` (install boot flag),
  `lastunix` (clock seed), `_init`.
- The two credentials — `wifipass` (with `ssid`/`wifi_en`: an SSID with no PSK
  is a join that can never succeed) and `token` — are OPT-OUT switches in the
  confirm modal, because the file is plain text on a card that leaves the
  machine. `mac`/`name` always travel: they identify the machine, not open it.
- A backup written by NEWER firmware is refused (`core::semver_newer`, shared
  with update_check.cpp — do not fork it): an older build indexes stored enums
  into fixed arrays, so a widened value would be an out-of-bounds read. Same
  reasoning as the NVS RULE below.
- Boards without a card slot pass `nullptr` for the port and the page is absent.

## Ready alert (App::start_ready_alert, 2026-10-26)

The end of a warm-up on the glass, for an owner who doesn't use the speaker:
fires on the SAME latch as the chime (`core::ReadyChime::update` returning
true in `App::update_heating` — once per turn-on, independent of the chime
settings). It wakes the screen (`lv_display_trigger_activity` + an immediate
`screensaver_tick`), covers lv_layer_top with theme `ok()` green + a big
`lv_line` check (its points live in `ready_check_pts_`: lv_line keeps the
POINTER) + "Ready" + live temps, and breathes the backlight from a 40 ms
lv_timer via `IDisplaySettings::set_backlight_live` — live only. Never use
`set_brightness()` for an effect: it persists to NVS (flash wear at 25 Hz).
`ready_alert_poll` (every refresh) clears it when the machine stops being
Ready (link/power/heating), a shot starts, or after 10 min; a tap clears it
(the cover swallows that tap); `screensaver_tick` holds the idle screen off
while it shows. Costs only while shown: 5 LVGL objects + that timer, no
buffers; nothing redraws for the breath. Sim: `round_ready_alert_800x800`
(the fake boilers are posed at their set points, or the alert clears
itself), manual shot `home-ready.png`.

## Scale battery on round Home (2026-10-30)

The paired scale's level in the weight capsule: build_round_home creates
`HomeWidgets::scale_batt_label` inside it (the same pointer rectangular Home
puts beside the scale status, icon only there), and update_home writes
"<LV_SYMBOL_BATTERY_*> NN%" when `w.round_glass` — red at
`ScaleFeatures::battery_low_pct` (per model: 15; the Aku 20, its gauge's last
step). `App::scale_battery_poll` (every refresh) shows ONE toast per
connection at that mark, pending while the shot phase isn't kIdle, the saver
is up, or a modal / the ready alert / another toast is showing; a reconnect
or a charge 10 points past the mark re-arms it. `ScaleLink::set_connected`
drops `battery_valid_` on both edges, so a scale charged while away can't come
back reading its old level and warn. Costs: one LVGL label (scale paired);
the toast is the existing one. Sim: `round_home_scale_lowbatt_800x800`
(FakeScale::set_battery), manual shot `home-scale-battery.png`.

## Milk by weight (round Home, 2026-10-31)

Home's Milk button (round only, scale paired; locked out like Tare without a
link or mid-shot) opens the Milk sheet: App::open_milk_sheet, on the dial's
layer and root (`dial_`), so close_dial closes it. Drinks (preset ml of cold
milk) and milks (g/ml) are APPEND-ONLY tables in `core/milk.h` (NVS-indexed:
`milkdrk`, `milktyp`, one `milkml<d>` per drink, 0 = the preset; readers clamp
an unknown index to the default). The drink/milk capsules open a FULL LIST
(`milk_open_list`: an opaque veil on `dial_` + a glass card of rows, each
with its ml / density, the current one bright; a tap outside closes it
unchanged) -- tap-to-cycle hid what the choices were (owner, 2026-10-31);
rows dispatch through `on_milk_pick<I>` (8 slots, static_assert). Target =
ml x density; on target within max(3 %, 3 g). Opening calls
`IBrewController::set_scale_borrowed(true)`: the unwired detector stays
disarmed (a pour reads like a shot -- host-tested) and an armed/delayed/
running auto-flush is cancelled (the pitcher sits under the group).
close_dial returns the scale. So the sheet must not linger: it closes when
a shot goes in flight (the paddle still runs one) and after 5 minutes with
no tap and no >= 2 g change (`milk_poll`, from refresh), and holds the idle
screen off meanwhile. The ring/numerals follow the scale per
loop (`milk_tick` from pump_scale_chart, redrawing only on a new seq; arc
colour written only on a tone change). The scale-battery toast waits while
any sheet is up. Costs: four LVGL objects on Home (column, button, icon,
caption); the sheet's ~20 objects (+ ~15 for an open list) exist only while
open; the milk glyph in the three Lucide fonts (regenerated from the cached
lucide-static 1.61.0; the other glyphs' bitmaps are unchanged, only the
offsets after it move). Sim: `round_milk_800x800` / `round_milk_done_800x800`
(round_sheet 3 = open + Tare, FakeScale::set_weight), `round_milk_drinks_` /
`round_milk_types_800x800` (4 / 5 = with a list open), manual shots
`home-milk.png`, `home-milk-list.png`.

## Failed-allocation flight recorder (crash_record.*, 2026-09-12)

Core dumps here hold task stacks only (CONFIG_ESP_COREDUMP_CAPTURE_DRAM is off
in the prebuilt libs and can't be turned on), so a heap starvation crash — the
P4 hosted radio's `sdio_rx_get_buffer` assert, the S3's esp-aes/TLS failures —
left no memory evidence. `platform::crash_record` hooks the heap's
failed-allocation callback (runtime API), takes a census of the pool that
refused the request (requested size + caps + function, totals, used blocks by
EXACT size, largest free holes) into <1 KB of RTC/LP memory (static_assert), and the next boot
prints it as `memfail:` lines in the log ring. Attribution is by size
signature (HEAP_TASK_TRACKING is unavailable), which names the big fixed-size
buffers and not small generic blocks. Dev serial commands: `memfail` forces an
impossible allocation to exercise it, `reboot` soft-resets (RTC survives; a
power cycle does not).

## Memory budget rule (owner, 2026-09-10)

**Never introduce more RAM use without review and a full explanation.** That
means: no task-stack increase, no new task, no larger buffer, no new
static/heap allocation on any device env — internal RAM, DMA-capable RAM or
PSRAM — without first stating what it costs (bytes, which pool, on which
boards), why the cheaper alternative doesn't work, and getting the owner's
OK. The S3 boards run a few KB from starvation (esp-aes/TLS and WiFi have
both failed for lack of internal RAM); a "harmless" 1.5 KB stack bump is
exactly how that happens. Prefer restructuring (move the work to a task that
already has the room, log from loop() instead of a small task, reuse an
existing buffer) over adding memory. Applies to fixes as much as features.

## Release checklist (every tag, no exceptions)

1. `include/version.h` — bump `fw::kVersion` to the tag (minus the `v`). The
   device compares THIS to releases.json; forget it and the new image reports
   the old version and re-prompts forever (v0.12.3, 2026-09-10). CI now fails
   the build if the binary's kVersion != the tag, but bump it first anyway.
2. `CHANGELOG.md` — real notes under `## vX.Y.Z` (the workflow extracts them).
   **Concise (owner, 2026-09-12): a bolded lead-in plus one or two sentences
   per bullet, ~50 words at the very most.** Say what the user sees.
   Mechanism, history and measurements belong in the commit message.
   **NEVER invent a version heading for unreleased work.** Notes for work that
   isn't tagged yet go under `## Unreleased` at the top; the release commit
   renames that heading to the tag. A `## vX.Y.Z` heading with no matching
   GitHub Release is a vestigial version (we've shipped several — v0.13.1 was
   the last), and the workflow warns if `## Unreleased` is still there at
   release time.
3. `make build-release` green (or `make build-all` for a platform change).
4. Commit, push the default branch (`master` in this fork — upstream's is
   `main`), then `gh workflow run firmware-release.yml -f tag=vX.Y.Z`
   (CI creates the tag + Release; never build release binaries locally).
5. **Approve the deploy.** The build runs unattended, then the `release` and
   `pages` jobs wait in the `release` environment for the owner's click
   ("Review deployments" on the run page; GitHub emails when it's waiting).
   Nothing reaches gh-pages — where every device self-updates from — or the
   Releases page until then. This is the OTA supply-chain gate (2026-09-19):
   the workflow token is read-only, only the gated environment holds the
   PAT that can write (secret `RELEASE_TOKEN`, fine-grained, this repo,
   Contents read+write — rotate it before it expires), and repo rulesets let
   only an admin push gh-pages or create a `v*` tag, so a workflow edit can't
   grant itself a way around it. Applies to betas too, on purpose: beta
   devices install straight from `releases-beta.json`.

**Pushing a beta** is the same five steps with the tag `vX.Y.Z-beta.N` and
`fw::kVersion` set to `X.Y.Z-beta.N`. CI publishes it as a GitHub pre-release,
keeps it out of `releases.json` (so only beta-channel devices and the flasher's
"Include pre-releases" box see it), and falls back to the `## vX.Y.Z` CHANGELOG
section if there is no `## vX.Y.Z-beta.N` one. **Promoting** it: on the same
commit, set kVersion to `X.Y.Z`, roll the beta notes up under `## vX.Y.Z`, and
run the workflow at `vX.Y.Z`. Devices on the beta see the release as newer
(release > any beta of that number) and update once.

## Git conventions

- Commit as the fork's owner, `jeewanghuman` — copy the name and address
  from this fork's own commits (`git log --author=jeewanghuman -1`).
  Upstream's `marcus@turboio.com` belongs to mlsorensen/Apollo2, and the
  repo-local identity in a cloud session is a placeholder, so pass the
  identity explicitly (`git -c user.name=... -c user.email=... commit`).
  Do NOT add "Co-Authored-By: Claude" or other AI-signature trailers.

## Round glass (P4-WIFI6-Touch-LCD-3.4C, 800x800)

The 3.4C is the P4-4.3's electronics behind an 800x800 ROUND panel; board
block `BOARD_WAVESHARE_P4_WIFI6_3_4C` (both silicon envs share it; the -rev3
env adds `-DBOARD_P4_SILICON_REV3`), panel table `BOARD_DSI_PANEL_JD9365_RD34`
(Waveshare's 3.4C vendor table — NOT the 4C's). The board defines
`BOARD_DISPLAY_ROUND`, which main.cpp turns into `ScreenProfile::round`.

- The UI is NOT redesigned per screen. `ui::round_band()` is the 0.8D x 0.6D
  rectangle whose corners sit exactly on the circle (640x480 at 800); every tab
  is built into a band-sized body, and `is_compact`/`is_xl` read the band
  (`ui::layout_width/height`), so the 3.4C runs the ordinary wide layout. The
  caps hold the tray (`build_round_tray`, top) and the tab buttons (pill row,
  bottom). Full-screen overlays call `fit_overlay_to_glass` so their
  lv_pct-sized cards land in the band; the saver bounces inside the band.
- New code that sizes from `screen_.width/height` must use the band instead
  on round glass, or it will draw off the glass. The sim's `round_*_800x800`
  renders are masked to the circle (red edge, off-glass dimmed) — check them.
- LIQUID GLASS (the whole round UI). Home is purpose-built for the circle
  (`round_home.cpp`, into the FULL Home page): idle view (brew/steam bezel
  arcs, thin Outfit numerals, steam capsule, round Standby/Flush, + weight
  capsule, Tare and the shot pill with a scale) and, with a scale, a shot
  view while a shot runs/settles/reviews (300 deg progress ring fed at the
  scale rate via `round_shot_progress` from pump_scale_chart, weight as the
  numerals, timer, flow graph in a glass window, the shot pill as Reset).
  Tapping the numerals opens the temperature dial (lv_scale ticks + a
  draggable lv_arc, -/+ in 0.5 C, Set), the steam capsule the steam sheet
  (on/off + 3 levels) — both App methods on the top layer, writing through
  the same dirty-flag paths as the Settings steppers (`dial_commit`,
  `steam_choose_*`). SETTINGS (`ui::round_glass()` in settings_tab.cpp) keeps its
  lv_menu tree and SettingsWidgets wiring but builds into a 520-wide column
  (y 116..640) of glass pill rows (fixed 66 px — flex only centres controls in
  a definite height), a round glass back chevron, amber switches/sliders
  (`round_controls_walk`). No Theme row on round: App pins the Midnight
  palette there (`kRoundThemeIndex`; the stored theme is left untouched).
  No "Oscilloscope graph" row either: it only styles the idle live sweep,
  and round Home shows a graph only during a shot, as the shot plot
  (`scope_graph_switch` is nullptr; App null-guards it). Scale > Device
  settings chips: read-only rows use LV_STATE_USER_1 (styled at build) —
  App must never rewrite a chip's bg_opa, or the round translucent pill
  goes solid white behind white text (fixed 2026-10-24).
  Micra > Controls shows brew/steam as single rows that OPEN the Home dial /
  steam sheet (`brew_open_row`/`steam_open_row`) — on round the stepper +
  steam-switch pointers in SettingsWidgets are nullptr, so App null-guards
  them. LVGL's perf-overlay label (bottom-right = off the glass) is moved to
  the top centre on round (`apply_perf_overlay`). STATS on round is its own full-page build
  (`build_round_stats`): a glass segmented capsule; Brew/Steam as the latest
  reading (48 px) + "Brew boiler • set 93.0°", an ordinary time-across /
  temperature-up chart in a 660x300 glass window over the circle's widest
  part, and a `[-] 30 min [+]` capsule (`stats_round_refresh` fills the
  labels). The lv_chart still holds App's data but in TENTHS on round
  (`StatsWidgets::value_scale`, App multiplies when it feeds it); its own
  drawing is off and `round_chart_draw_cb` paints everything — no buffer —
  fitting the axis to the readings + set point (`nice_axis`: 1/2/5/10...
  steps, min span 8 C / 14 F, works in F), 20 px labels, dashed set line,
  no-data bands, glow trace + bead. (It was a RADIAL chart — time = angle,
  temperature = radius — until 2026-10-24: hard to read on the panel.)
  History as metric bubbles over a glass list (no month filter:
  history_filter_list is null) with URL and SD stacked, Info as one tall
  glass card. The Home shot graph is a 600x180 lens with 20 px axis labels;
  its canvas (LVGL's pool, the content box x 2 B) is 496x124 = 120 KB, vs
  170 KB at 800x480 and up to ~680 KB on the big rectangular panels (sim,
  2026-10-24). Painted crema on dark glass (home_tab.cpp's flow palette);
  the shot card's graph takes crema/blue (shot_card.cpp). All three read
  `ui::round_glass()` (screen.h; App::build sets it beside the UI scale) —
  the one round flag for helpers that don't have the ScreenProfile. Anything else (modal cards, rows App adds
  later) is made glass by `ui::glass::glassify_tree` (theme-bg fills -> transparent, card/rail
  fills -> translucent + 1 px white outline; canvases' parents stay opaque),
  re-run at the END of each refresh on the active tab + modal so rows built
  that tick convert before they are drawn; it also turns every button into a
  pill.
  The backdrop is PLAIN BLACK, on the SCREEN behind the tabview on every
  tab; the arcs (`rg_ring`) only on Home (`App::sync_round_backdrop`). (It
  was a generated blurred "crema" image until 2026-10-22 — on the real panel
  it read as muddy and busy behind the numerals and graph; owner dropped it.
  Don't bring a gradient backdrop back without asking.) The tab dock lives INSIDE the
  tab bar at index 0 (tabview finds buttons by class) so it hides with it.
  `ui/glass.*`: flat fills only — gradients band in RGB565. And NO drop
  shadows or brighter top "specular" rim on glass pieces (removed
  2026-10-25 after the owner saw lines on the buttons): a black shadow is
  invisible on the black backdrop, while over a glass card RGB565 steps its
  soft edge into a hard dark one, and LVGL 9.6 clips children to the
  parent's coords + the PARENT's ext draw size (LV_OBJ_FLAG_OVERFLOW_VISIBLE
  alone doesn't help), so a shadow in a content-sized row is cut square. The
  sim reproduces these — zoom into renders/glass/ before trusting a glass
  effect. The only shadows left are deliberate glows: the bezel-arc and
  dial knobs, the stats bead, the clock orb. Same trap for borders: the
  default theme's selected-tab style is border_side BOTTOM (its underline),
  so a rim on the round tab bubble needs LV_BORDER_SIDE_FULL or it draws a
  line under the pill (fixed 2026-10-25).
  TYPE + ICONS (2026-10-26): round text is Outfit Regular — `ui::font_dp`
  returns `font_outfit_{14,16,20,24,28,36,48}` when `round_glass()` (font_dp(12)
  rounds to 14), each FALLING BACK to the same-size Montserrat for LV_SYMBOL_*
  glyphs, and App::build sets the round screen + top layer's text font so
  unstyled labels follow; rectangular boards keep Montserrat. Every Outfit
  face (hero included) has TABULAR digits — the generators point 0-9 at
  Outfit's `.tf` glyphs, because lv_font_conv can't apply `tnum` and the
  proportional '1' is half a '0' (numbers shimmied sideways as they changed).
  Icons on Home's actions, the tab dock and the dial are Lucide (ISC,
  src/ui/fonts/LICENSE-Lucide.txt) via `include/ui/icons.h` (`ui::icon::k*`,
  `font_lucide_{24,28,32}`) — only the glyphs listed there are in the fonts;
  add a codepoint to ICONS in tools/gen_ui_fonts.sh AND icons.h. Regenerate:
  tools/gen_ui_fonts.sh (text + icons), tools/gen_hero_font.sh (numerals).
  MOTION + STATE (round_home.cpp): arcs GLIDE to new values (lv_anim, 600 ms
  ease-out, custom exec cb — lv_anim_start doesn't de-dupe those, so
  glide_arc deletes first; the FIRST value is set outright so a fresh build
  and the sim's single frame show real state, and the sweep plays on
  connect); brew numerals ROLL through the tenths on moves >= 0.5 deg;
  numerals tinted heating/ready/idle, arcs dimmed in standby, shot ring
  green from 95 % (style writes only on change: any local style write
  invalidates the whole arc). Faint lv_scale graduations inside both arcs
  (`rg_brew_ticks`/`rg_steam_ticks`). build_round_home resets the glide/
  roll state. No curved captions (lv_arclabel spacing, see the file header)
  and no sheet/dial entrance animation (a large moving area every frame).
  Costs (owner-visible, see Memory budget rule): no backdrop buffer (black
  fill); flash only for type — hero font ~40 KB, the seven Outfit text faces
  ~117 KB, the three Lucide icon fonts ~10 KB (measured from the compiled
  objects, 2026-10-26); ~40 extra LVGL objects on Home (+2 for the tick
  scales), the dial/sheet only while open; motion allocates only LVGL's
  transient lv_anim entries. Not yet measured on hardware: frame times (the
  glassify walk, the numeral roll).
  CLOCK IDLE SCREEN (`kSaverClock`, round only — the cycle skips it and a
  restored Clock falls back to Lion on rectangular boards): its own screen
  like the lion (`App::start_clock_saver` / `clock_saver_tick`, 10 Hz):
  faint bezel tracks, weekday + hero-font clock with a +-8 px 10-min sway,
  and a glass orb (brew temp + state, amber shadow as the glow) lapping an
  ellipse every 4 min — burn-in movement. Stored WITHOUT widening any old
  key: `ssclock` (bool) rides on ssstyle2 = Logo with ssart untouched, so an
  older build reads "Logo" and bounces the previous artwork (NVS RULE).
  The lion still bounces inside the band on round.
- HW bring-up (rev v1.3 unit, 2026-10): boots, panel + GT911 + hosted radio
  up, UI visible. Backlight is ACTIVE-LOW like the 4.3 (normal polarity =
  dark panel + POWERON reset loop), and it goes DARK at Brightness <= 30%
  (owner, 2026-10-25): the PWM biases the AP3032's feedback node, so it sets
  LED current, which hits zero between 30% and 40%. The block's
  BOARD_BACKLIGHT_FLOOR_PERCENT 40 makes Display::set_brightness map 1..100
  onto 40..100 (0 stays off), which keeps kSaverDimPercent's idle screens lit
  too. Orientation (default MADCTL) and the
  touch mapping (swap XY + mirror Y, as committed) are correct as-is.
  Not yet confirmed on the unit: the paddle, audio, SD. PADDLE PINS (moved
  2026-10-26): sense GPIO20 / drive GPIO21 / GND = J8 pins 27/29/31, side by
  side and printed "20 21 GND" on the board — Waveshare's XC schematic and
  its silkscreen page agree (rendered from the PDF; J8's net labels sit
  ABOVE their wires, and Waveshare's pin 1 is at the opposite end from a
  Raspberry Pi's). The older note here (J8 7/9/11 = 52/GND/51) was wrong:
  52 is pin 5, 51 pin 11, pin 9 GPIO32. 52/51 have no GND beside them, and
  GPIO39-48 (46/47/48 on J8) take their I/O supply from VDDPST_5 = the
  on-chip LDO VO4, which ShotStore's ensure_bus() only enables when it first
  mounts the card: never put the paddle, or anything live from boot, there.
  No battery circuit: the block defines
  `BOARD_NO_BATTERY`, battery.cpp reports `BatteryState::hw = false`, and
  the UI drops the tray power indicator and the Stats > Info Runtime row
  (the sim poses this for every round render via FakeBattery::set_hw).

## Fork releases (jeewanghuman/Tondo)

THIS REPO IS A FORK of mlsorensen/Apollo2 whose only product is the round
3.4C, named TONDO (renamed from "Apollo 2" on 2026-10-24; the repo was
`Micra-display` until then). Everything user-facing says Tondo: the Settings
section, Stats > Info header, modal text, the History web page, the flasher,
release notes and docs. Kept on purpose: the credit to Apollo 2 (README,
flasher, MANUAL's backup notes), upstream's CHANGELOG history, and upstream's
hardware/paddle-bridge ("Apollo Link" board, prebuilt gerbers, and its
silkscreen source tools/logo/apollo-silkscreen.svg). REMOVED on purpose
(owner, 2026-10-26): upstream's Apollo idle-screen artwork and the
Alternate style — Lion is the only artwork (`ssart` 1/2 are retired: read
as Lion, never reused; see config.cpp). Merging upstream will conflict
wherever it touches that saver code; keep them out. The SD
folder is `/Tondo` (core::kShotDirName); ShotStore::try_mount's
adopt_legacy_dir() renames a card's `/Apollo2` to `/Tondo` once, only when
`/Tondo` doesn't exist (core::kLegacyShotDirName). Backups say "# Tondo
settings backup" — restore keys on `format=`, so Apollo-era files still load.
The repo's name is `Tondo` (capital T), and every URL (OTA site, flasher,
github.com links, the update-modal hint) must match that case exactly —
Pages paths may be case-sensitive, and the firmware's kSiteBase can't be
patched on a unit that can no longer reach its update site.
Its OTA site is `https://jeewanghuman.github.io/Tondo`
(`kSiteBase` in update_check.cpp AND install_mode.cpp — keep them equal; the
update-modal hint in app.cpp names it too). The release workflow's matrix
builds ONLY `esp32-p4-micra-3-4c` (slug `p4-wifi6-touch-lcd-3.4c`, rev v1.x —
the owner's unit is v1.3); the web flasher (site/index.html) has one card and
maps a "(rev3)" banner to no image. Add the `-3-4c-rev3` row + a card only if
a rev3 unit appears. The upstream "Support matrix" below describes UPSTREAM's
four boards; here they are not published. Release mechanics are unchanged
(checklist below): the `release` environment (owner approval) + its
`RELEASE_TOKEN` secret, Pages serving gh-pages. A unit flashed before
2026-10-23 still points at upstream and needs one USB flash to switch. Pulling
upstream changes = merge upstream/main, then release from here. The fork's
default branch is `master`, and releases are run from it: round work lands on
a branch and is merged into master before a release. `make build-release`
here = the 3.4C + sim. The user-facing docs (README, MANUAL,
docs/HARDWARE.md, docs/WIRING.md) describe the 3.4C only (DOCS SYNC RULE).
In platformio.ini / board_config.h the 3.4C is marked RELEASED; the other
blocks' RELEASED markers are upstream's, left as-is for clean merges.

Case: `hardware/3d-prints/esp32-p4-3.4c-shell.scad` (source of truth; the
.3mf files are exports of it). Round cup + back cover held by 4 radial
M2.5 screws; the cover carries Apollo 2's mount interface (2 x M4,
75 mm apart, 3.7 mm pilots — measured from tondo-magnet-mount.3mf), so the
existing magnet mount and stand fit unchanged. Waveshare publishes only the
115 mm outline: `body_depth`, `usb_angle`, `usb_from_front` are UNVERIFIED
guesses until the owner measures the board. Re-export after changing them:
`openscad -D 'part="shell"' -o shell.stl ...` (and "cover", "gauge").

## Support matrix (READ BEFORE TOUCHING ANY BOARD LIST)

UPSTREAM's matrix. In this fork only the round 3.4C is released (see "Fork
releases"); this section is kept as the record of upstream's boards and for
merging upstream changes. Read "INTERNAL-ONLY" for the 3.4C below as
upstream's status, not this fork's.

FOUR boards are released and advertised. Nothing else is, on purpose:

| env | product | role | slug |
|---|---|---|---|
| `esp32-p4-micra-5` | P4-WIFI6-Touch-LCD-5 | mount-on-Micra, the default pick | `p4-wifi6-touch-lcd-5` |
| `esp32-p4-micra-43` | P4-WIFI6-Touch-LCD-4.3 | mount-on-Micra, smaller glass | `p4-wifi6-touch-lcd-4.3` |
| `esp32-s3-micra-4-3c` | S3-Touch-LCD-4.3C BOX | mount-on-Micra, nothing to build | `s3-touch-lcd-4.3c` |
| `esp32-p4-micra-x-8` | P4-WIFI6-Touch-LCD-X 8" | counter-top companion | `p4-wifi6-touch-lcd-x-8` |

Positioning (keep the docs saying this): the **P4-5 is the recommendation** for
mounting on the machine — it just needs a printed shell and, for Auto shot, a
DIY opto cable. The **P4-4.3 is THE SAME BOARD with smaller glass** — the -5 env
literally `extends` the -43 env, and the only board_config deltas are panel
controller, DSI timings, reset/backlight polarity, touch rst/int and UI scale.
Everything functional (P4NRW32, C6 radio, ES8311 audio, battery path, SD, paddle
pins) is byte-identical. **Choose between them on screen size and a small price
difference — nothing else.** The **4.3C earns its place by removing BOTH DIY
steps**: it is the only supported board that is a finished box *and* has
opto-isolators on-board (three wires into screw terminals). It is the slower
part; that is the trade. The **X 8"** is the counter-top box.

Every OTHER env — `esp32-s3-micra` (2"), `-7b`, `-4-3b`, `-x-7`, `-x-10-1`,
`-3-4c` / `-3-4c-rev3` (the round 3.4C, see "Round glass" below) — is
**INTERNAL-ONLY**: it still builds, is still maintained,
and is deliberately absent from the release matrix, the web flasher and every
user-facing doc. They are kept as the record of how those boards differ, and as
dev hardware (the 7" X box is the owner's desk unit). **Do not add one back to
README.md, docs/HARDWARE.md, site/index.html or the CI matrix without asking.**
Nothing is deleted to de-advertise a board — only de-listed.

The lists that must stay in sync when this changes: the
`.github/workflows/firmware-release.yml` matrix, `site/index.html` (board cards
*and* `BOARD_PATTERNS`), README.md ("Which board?", the 3D-print table, the
flash/`pio run` lists), docs/HARDWARE.md, and the `[RELEASED]`/`[INTERNAL-ONLY]`
markers in platformio.ini + board_config.h.

- **OTA on de-listed boards: knowingly broken, do not "fix" it.** `releases.json`
  is global, so an internal-only build still sees "update available" and its
  install 404s and rolls back harmlessly. Accepted because none of these boards
  are expected to be in anyone's hands — they get flashed over USB. Revisit only
  if one actually sees use. No `kOtaPublished` flag, no UI gating.
- **Coming: rev v3 production silicon for the P4 boards (BOTH the 5 and the
  4.3).** Samples are expected; the owner will buy a v3 P4-4.3 to test at least
  once, and the v3 P4-5 is expected to validate the pattern for the family.
  **The product list stays FOUR — the silicon split doubles the IMAGES, not the
  boards.** Waveshare does not change the SKU, so a buyer cannot tell which
  silicon they got, and surfacing it as a separate product in the README/board
  table would be wrong. Call it out in the DEVELOPER docs only; the flasher
  handles selection. Shape of it, per P4 product:
    * a SECOND board block + env + board json (`chip_variant "esp32p4"`,
      `BOARD_P4_SILICON_REV3` defined — already the macro display.cpp uses for
      the DSI PHY's XTAL PLL reference), slug `<plain-slug>-rev3`.
    * the PLAIN slug keeps meaning rev v1.x ("es") — never rename it, fielded
      units self-update from that path.
    * the flasher keeps ONE card per product and chooses the image: Detect
      reads the `REV=` field the identify banner already emits
      (`src/device/main.cpp`, efuse major*100+minor, >= 300 = rev3). A first
      install on a blank board can't be probed, so the card needs an explicit
      silicon choice there.
  **Picking wrong is recoverable and that is fine** — the board just won't come
  up until the other image is flashed; nothing is damaged. Don't over-engineer
  the guard-rails for it.

## Boards / build

Board targets are `<chip>-<panel>` after the Waveshare product names.
Released in this fork: `build-p4-3-4c` (`build-p4-3-4c-rev3` for rev3
silicon, unpublished). Upstream's released targets (`build-p4-5`,
`build-p4-4-3`, `build-s3-4-3c`, `build-p4-x-8`) and its internal-only ones
(`make build` = the 2-inch S3, `build-s3-7b`, `build-s3-4-3b`, `build-p4-x-7`,
`build-p4-x-10-1`) all still build. Matching `flash-*` targets
auto-detect the port and can probe a running board's serial banner (pre-rename
names like `build-p4`/`flash-7b` remain as aliases). Two sweeps:
`make build-release` (the 3.4C + sim — run this before pushing a platform
change or cutting a release) and `make build-all` (every env + sim).

WHEN to run full builds: `make build-all` must pass before you PUSH or cut a
RELEASE of a platform change — not on every edit. During debug/development stay
on `make sim` (fast, no node, no device contention) and at most the ONE device
env you're actually testing on. Full sweeps mid-investigation are slow, contend
for the shared `.pio` dir (see the build-workflow notes about flashing), and
tell you nothing you don't already know until the fix is finished.

The firmware embeds the History web page as a GENERATED, git-ignored header
(`include/platform_esp32/webapp_dist.h`). Every device build/flash target
depends on it, so `make build` rebuilds it from `tools/webapp/` when stale —
that needs **node** (a real dev dependency; `make sim` does not need it). Bare
`pio run` does NOT generate it: run `make webapp` first. Never commit the
header or hand-edit `tools/webapp/dist/`.

### ESP32-P4-WIFI6-Touch-LCD-5 (env `esp32-p4-micra-5`) — HW-VERIFIED, the owner's daily-driver board and the default recommendation

Electronically the P4 4.3 (same radio/audio/battery/paddle wiring — everything
in that section applies, including the rev v1.x chip_variant); only the panel
differs: 5" 720x1280 HX8394 over the same 2-lane DSI. Panel deltas, all from
Waveshare's BSP (esp32_p4_wifi6_touch_lcd_5 + esp_lcd_hx8394): different DCS
init table (display.cpp, selected by BOARD_DSI_PANEL_HX8394), reset asserts
HIGH (kLcdRstActiveHigh), backlight LEDC is normal polarity (kBacklightActiveLow
= false) with NO boost-enable pin, GT911 rst/int not wired to the P4 (probe
only), 58 MHz DPI / 700 Mbps lanes. UI: `BOARD_UI_SCALE 1.5f` renders the wide
800x480 layout at 1.5x via ui::dp()/ui::font_dp() (see include/ui/screen.h) —
scale 1.0 boards are bit-identical, verified against baseline renders.

### ESP32-P4-WIFI6-Touch-LCD-X 7"/8"/10.1" (envs `esp32-p4-micra-x-7` / `-x-8` / `-x-10-1`) — 7" verified on HW (2026-08-29: boot, display, touch, hosted link, paddle sense); 8"/10.1" NOT yet

RELEASE STATUS: the **8" is the released counter-top product**; the **7" is
INTERNAL-ONLY** (the owner's desk/dev unit — de-listed 2026-09-10, not a
capability judgement) and the 10.1" has never been released. Only the 8"
appears in CI, the web flasher and the docs.

The finished-box (all-in-one HMI) family. Electronics = the P4 4.3/5 (same
I2C 7/8, GT911 probe-only, battery GPIO20 ÷3, ES8311 + PA GPIO53, SD 39-44 on
LDO4, paddle 51/52 on the 40-pin header — GND/52/51 run consecutively in one
pin column like the 5). Deltas, all verified against the X schematic +
Waveshare BSP (waveshareteam/Waveshare-ESP32-components,
bsp/esp32_p4_wifi6_touch_lcd_x):

- **Silicon: X boards exist in BOTH revision generations — PER UNIT, not per
  size.** The user's 7" (bought 2026-08) is **rev v1.3**; the user's 8" is
  **rev v3.2** (verified in the countertop-display sister project, 2026-09).
  The boards jsons currently all use `esp32p4_es`, which boots the 7" but
  made the v0.9.0 x-8 image WRONG for the real 8": on rev3 the wrong variant
  boot-loops with `CHIP_LP_WDT_RESET` before printing anything — looks like a
  dead board. FIXED post-0.9.0: the x-8 and x-10-1 jsons now use `"esp32p4"`
  (400 MHz real; display.cpp selects the XTAL DSI-PHY PLL ref for those envs
  and the 8" JD9365 table carries the MADCTL 180° glass flip). The 7" json
  stays `esp32p4_es`. Always `esptool chip-id` a new unit first.
- **X-8 bring-up learnings (from the sister project, HW-verified there):**
  (1) rev3 DSI PHY: the legacy `MIPI_DSI_PHY_CLK_SRC_DEFAULT` (PLL_F20M)
  aborts inside the HAL on rev3 — it needs `MIPI_DSI_PHY_PLLREF_CLK_SRC_
  DEFAULT` (XTAL). Now selected by the `BOARD_P4_SILICON_REV3` macro, which a
  board's block in board_config.h defines when its json is `chip_variant
  "esp32p4"` — keyed on silicon, not on panel, so a rev3 build of ANY P4 board
  gets it. (A runtime `efuse_hal_chip_revision() >= 300` check would also work
  but buys nothing: chip_variant is a link-time split, so rev1 and rev3 are
  separate binaries regardless.) (2) Our JD9365 8"
  table + 80 MHz/1500 Mbps timings worked first time (panel up in ~340 ms).
  HEADS-UP (user, 2026-09): the OTHER P4 boards (4.3/5) may eventually ship
  with v3 silicon too. For the RELEASED P4-5 this is now planned work — a
  sample is expected; see the "Support matrix" section for the shape of it
  (second env + json, slug `p4-wifi6-touch-lcd-5-rev3`, plain slug stays ES).
  Prep in place: the serial identify line reports `REV=` (efuse
  major*100+minor) so the web flasher's Detect can pick the right variant, and
  display.cpp's PHY-clock branch already keys on `BOARD_P4_SILICON_REV3` — the
  new board block just defines it.
  (3) The 8" box mounts its glass the OPPOSITE way up from the 7": same
  table renders upside down — fix is rotating 270° instead of 90° in the
  flush (a direction constant; touch mapping follows it). Our per-size touch
  #if was the right structure. (4) GT911 at 0x5D like the others; TP_INT
  unverified on the 8" (the 7" schematic routes it via R32 to GPIO33 —
  relevant if wake-on-touch is ever wanted). (5) Rev3 pads hold state
  through deep sleep (documented; untested) — the v1.x boards' pull-down
  mods shouldn't be needed there.
- Panels (native portrait, rotated like the other P4 DSI boards): 7" =
  720x1280 ILI9881C (80 MHz DPI, 1000 Mbps), 8" and 10.1" = 800x1280 JD9365
  (80 MHz, 1500 Mbps) — but the 8" and 10.1" glasses take DIFFERENT vendor
  init tables (the BSP's #if/#else), so each size is its own env/image.
  Tables live in display.cpp (BOARD_DSI_PANEL_ILI9881C / _JD9365 /
  _JD9365_10). Reset active-LOW (unlike the 5's HX8394).
- The 7" BOX MOUNTS ITS PANEL 180° from the P4-5 convention (camera on top =
  correct orientation; HW-verified). Fixed at the panel: both MADCTL writes
  in the ILI9881C table are 0x03 (GS|SS scan flip — both bits = true 180°,
  one alone would mirror), and the 7"'s touch flags toggle BOTH mirrors vs
  the P4-5 values (per-size #if in the X block). Careful editing that table:
  0x36 also appears as a page-1 GIP register mid-table — only the page-0
  writes are MADCTL. Expect the same 180° question on the 8"/10.1" at
  bring-up (check camera position; JD9365 has the same GS/SS bits).
- Backlight: LEDC GPIO26 normal polarity + AP3032 boost-enable GPIO23
  (kLcdBacklightEn — the 4.3 has one too, the 5 doesn't).
- UI scale: 7" = 1.5 (same logical 853x480 as the 5); 8"/10.1" = 1.6 →
  logical 800x500 — exact 800 width, the extra height feeds the flex-grow
  regions (sim renders at 1280x800 cover it).
- Extras we don't drive: ES7210 mic ADC, second USB OTG (GPIO24/25), camera.
- The 10.1" env exists and builds (`esp32-p4-micra-x-10-1`, its own JD9365_10
  table) but is deliberately UNRELEASED and unmarketed — no hardware to test
  it on, so it's kept out of the README, web flasher, and release matrix
  (owner's call 2026-08-29). Don't re-add it to those without asking.

### ESP32-S3-Touch-LCD-4.3C (env `esp32-s3-micra-4-3c`) — verified on HW

The 4.3B's RGB/GT911/RTC wiring plus the 7B-style register-based IO extension
(Waveshare CH32V003 @0x24) instead of the 4.3B's CH422G — which adds true PWM
backlight dimming (reg 0x05, inverted duty; vendor clamps at 95% to avoid
full-off) and battery monitoring via the expander's ADC (reg 0x06, ÷3 divider,
scale 3·3.3/1023 — not yet multimeter-calibrated; the 7B's same-family chip
needed 0.009632). Support is config-only: no new driver code. RGB timings are
the 4.3C demo's (pulse 4, porches 8/8, 16 MHz), not the 4.3B's.
(USB-vs-battery is now inferred from the battery-node voltage alone —
kUsbPowerVolts — on every board; HWCDC::isPlugged() is no longer used.)
Bring-up gotcha: scripted DTR/RTS toggling on the USB-CDC port can strand the
board in ROM download mode — black screen, silent serial, yet flashing still
works. Recover with the physical RST button; don't script reset dances.

Paddle (brew-by-weight, verified on HW): 3-wire harness — Micra white -> DO0,
paddle switch -> DI0, Micra black + paddle return -> shared GND. DI COM is the
*biased* side of the input opto (internally ~5V), so a dry contact must close
DI0 to GND — wiring it to DI COM does nothing. Drive = EXIO6 (active-low),
sense = EXIO0 (low = closed), direction mask 0xDE. GOTCHA: the expander can
ACK its init yet DROP the first direction-mask write (inputs read 0xFF
forever); paddle.cpp re-asserts the mask in begin() and every ~64th sense
poll — keep that if refactoring.

### ESP32-P4-WIFI6-Touch-LCD-4.3 (env `esp32-p4-micra-43`) — RELEASED, HW-verified

Re-added to the released set 2026-09-11 (a user has one). Display, touch,
BLE, **paddle, audio and the battery ADC are all confirmed working on
hardware** (owner, 2026-09-11) — the bring-up list below is kept for the
wiring detail, not as a to-do. No 4.3 hardware is kept here, so the P4-5
(same board, different glass) is the proxy for anything non-panel.

First non-S3 board: P4NRW32, 480x800 ST7701 over 2-lane MIPI-DSI (rotated to
landscape), GT911 touch, WiFi6/BLE via on-board ESP32-C6 over SDIO
(esp-hosted). Compiles; NOT yet validated on hardware. Key facts:

- NimBLE-Arduino does NOT support the P4 (maintainer statement, issue #906).
  This env uses `h2zero/esp-nimble-cpp` (same `NimBLE*` API) against the
  stock Arduino core's IDF NimBLE host, which ships NimBLE-over-hosted
  enabled (verified in the core's esp32p4/sdkconfig). `lib_compat_mode = off`
  is required. Do not "simplify" the P4 env to NimBLE-Arduino.
- Hosted SDIO pin defaults match the board exactly — no WiFi.setPins needed.
- The C6 runs factory esp-hosted slave firmware; `make flash-p4-4-3` never
  touches it. Slave updates (if ever needed): esp-hosted OTA from the P4, or
  the board's P1 header + esptool.

#### Silicon revision gotcha (hit + solved on first hardware, 2026-07)

ESP32-P4 chips exist in two binary-incompatible generations: pre-production
rev v1.x ("engineering sample" retroactively; sold in volume through 2025)
and mass-production rev v3.0+ (v2 was skipped). Our first board is **v1.3**
— check any new board with `esptool chip-id` (or espefuse `WAFER_VERSION_*`).

- Symptom on v1.x when built for the default (rev >= 3.0) target: the
  2nd-stage bootloader itself boot-loops with `Guru Meditation ... (Illegal
  instruction)` where PC == the bootloader entry address and MTVAL=0. Screen
  stays black; app code never runs. Easy to misread as a display problem —
  check serial first.
- Fix: `"chip_variant": "esp32p4_es"` in boards/esp32-p4-wifi6-43.json.
  The Arduino core ships PREBUILT `esp32p4_es` libs (rev v1.x memory map,
  NimBLE + esp-hosted enabled) and pioarduino selects them + the matching
  rev<3 linker templates off that field. Same fix as arduino-esp32
  PR #12341 (M5Stack Tab5). A rev >= 3.0 board needs `"esp32p4"` instead —
  one image cannot boot both generations. Dead ends we tried so the next
  session doesn't: platform downgrade to 54.03.21-2 (P4 BT compiled out) and
  hand-rolled `custom_sdkconfig` rev overrides (pioarduino's hybrid compile
  generates rev3 linker scripts regardless — builder bug).
- v1.x runs the CPU at 360MHz max (boot warning about 400MHz is expected).

#### Hosted BLE bring-up (the second trap)

`NimBLEDevice::init()` alone dies on this board: esp-nimble-cpp drops into
the IDF NimBLE host whose vhci layer single-shots the hosted-SDIO transport
init and aborts with `H_SDIO_DRV: card init failed`. The transport must be
brought up first through Arduino's hosted HAL — `hostedInitBLE()` from
`esp32-hal-hosted.h` (does esp_hosted_init + connect_to_slave + BT
controller RPC). main.cpp calls it before NimBLEDevice::init, guarded by
`CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE`. Verified on hardware: SDIO link up,
NimBLE host init clean. A `Req_GetCoprocessorFwVersion` RPC timeout error
at boot is harmless (factory C6 slave predates that RPC).
Waveshare's factory image + demo repo (waveshareteam/ESP32-P4-WIFI6-Touch-
LCD-4.3) is the known-good reference: flashing its FactoryOnly bin is the
fastest way to prove the C6/slave/wiring are healthy when debugging.

Hardware bring-up detail (2026-07-17: boots clean — hosted link, DSI panel,
GT911 all up; NimBLE host inits. Paddle/audio/ADC since confirmed, 2026-09-11):
1. Display: panel + LVGL come up (800x480). Arduino_GFX rotation=1 goes
   through a per-pixel rotated bitmap path — still check flow-graph fps;
   fall back to LVGL-side rotation if slow.
2. Touch: GT911 detected at 0x5D. swap/mirror flags in board_config.h are
   best-guess; serial logs one line per press for calibration.
3. BLE: host init verified; actual Micra/scale connections not yet tested.
   Known bug esp-hosted-mcu#180: scan results stall after ~60-90s of
   continuous scanning (our scans are short; reconnects are direct-by-MAC).
4. Paddle (brew-by-weight): native GPIOs on the header corner — GND, GPIO52,
   GPIO51 fit one 3-pin screw terminal. GPIO52 -> PC817 opto module IO (module
   input GND -> board GND); output side VCC left floating so OUT/GND are an
   isolated dry contact: OUT -> Micra white, output GND -> Micra black.
   Active-HIGH drive (IO high = contact closed) — opposite of the 4.3C's
   expander. GPIO51 <- paddle switch to board GND (INPUT_PULLUP, low =
   closed); the physical paddle touches only this board, never the Micra.
   Config-only (native-GPIO path in paddle.cpp). CONFIRMED working on HW.
5. Audio: config-only reuse of the 4.3C's ES8311 driver — BSP pins MCLK 13 /
   BCLK 12 / LRCLK 10 / DOUT 9, codec at 0x18 on the shared I2C bus, PA
   enable native GPIO53 active-high (BOARD_AUDIO_PA_IOEXT selects expander-vs-
   GPIO PA in sound.cpp). CONFIRMED working on HW.
6. Battery: WORKING — BAT_ADC GPIO20, divider ÷3 (confirmed: raw*3 == 4.20V
   LiPo CV level while charging). The bring-up calibration log that used to sit
   in battery.cpp behind BOARD_WAVESHARE_P4_WIFI6_43 is gone — divider settled. `HWCDC::isPlugged()` is non-functional on
   the P4's USB-Serial-JTAG (always false) — moot now that USB-vs-battery is
   voltage-only (>= kUsbPowerVolts) on all boards. Known hardware trait:
   plugging/unplugging USB with a battery attached FULLY POWER-CYCLES the
   board (reset reason: power-on) — the power path's VBUS<->boost switchover
   drops the rail; not fixable in firmware.
