# Tondo web app

The phone page the device serves at `http://tondo.local` (mDNS; the name is
a setting) — a single-file React + Material UI app, gzipped and embedded in
the firmware as `include/platform_esp32/webapp_dist.h`. Four tabs:

- `LiveTab.jsx` — polls `/api/live` once a second (5 s in the background);
  power / brew / steam go to `/api/control`. Never a shot: the device has no
  route for one.
- `HistoryTab.jsx` — `/api/shots` (notes ride in each row), compare, notes
  (`/api/shot/note`), delete, exports.
- `TrendsTab.jsx` — computed here from the shot list.
- `SettingsTab.jsx` — `/api/settings`, updates, backup download / restore.

Every POST carries the PIN shown on the device (`api.js` keeps it in
localStorage; a 403 forgets it). The device-side routes and their threading
are documented in `include/platform_esp32/web_ui.h`.

`public/` holds the home-screen icons (drawn by `gen_icons.py`, committed);
`gen_header.py` embeds them next to the page, and the device serves them with
`/manifest.webmanifest` for "Add to Home Screen". There is no service worker:
browsers only allow one over https.

It styles itself from the DEVICE'S ACTIVE THEME at load (`/api/summary`
carries the palette), so the page always matches the screen.

## Building

You don't, normally: the embedded header is a **generated build artifact, not
committed**, and every device target depends on it —

    make build      # or make flash, make build-p4-5, ...

rebuilds it whenever anything in this directory changes, and the release
workflow does the same. That's what keeps the page a board serves from drifting
from the source here. To build it on its own:

    make webapp     # == tools/webapp/build.sh

**This makes node a dev dependency of any device build** (`brew install node`;
any recent LTS). `make sim` doesn't need it — the simulator has no web server.
If you invoke `pio run -e <env>` directly instead of going through `make`,
build the header first or the firmware won't compile.

`node_modules/`, `dist/` and the generated header are all git-ignored.

## Development

    npm install
    npm run dev     # vite dev server on localhost

The dev server has no device behind it, so `/api/*` calls fail — proxy them to a
real board in `vite.config.js` when working on data-driven parts.
