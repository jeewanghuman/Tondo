// Formatting shared by every tab.

export const hex = (v) => '#' + (v >>> 0).toString(16).padStart(6, '0');

export const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
  'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

export function fmtWhen(unix) {
  return new Date(unix * 1000).toLocaleString([], {
    month: 'numeric', day: 'numeric', year: 'numeric',
    hour: 'numeric', minute: '2-digit',
  });
}

export function fmtDay(unix) {
  return new Date(unix * 1000).toLocaleDateString([], { month: 'short', day: 'numeric' });
}

// How the shot was run (wording matches the device's labels).
const MODE_LABEL = { auto: 'Auto shot', detect: 'Detected', manual: 'Manual' };
const MODE_TAG = { auto: 'Auto', detect: 'Detect', manual: 'Manual' };
export const modeLabel = (s) => MODE_LABEL[s.mode] ?? MODE_LABEL.auto;
export const modeTag = (s) => MODE_TAG[s.mode] ?? MODE_TAG.auto;

export function monthKey(unix) {
  const d = new Date(unix * 1000);
  return d.getFullYear() * 100 + d.getMonth() + 1;
}

export function monthLabel(ym) {
  return `${MONTHS[(ym % 100) - 1]} ${Math.floor(ym / 100)}`;
}

export function shotFileBase(s) {
  const d = new Date(s.unix * 1000);
  const p = (v) => String(v).padStart(2, '0');
  return `shot-${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-`
    + `${p(d.getHours())}${p(d.getMinutes())}`;
}

export function fmtBytes(b) {
  if (b >= 1e9) return (b / 1e9).toFixed(1) + ' GB';
  return Math.round(b / 1e6) + ' MB';
}

export const diffOf = (s) => (s.target_g > 0 ? s.final_g - s.target_g : null);

// Temperatures travel in C; the device's unit setting decides the display.
export const toUnit = (c, f) => (f ? c * 9 / 5 + 32 : c);
export const unitLabel = (f) => (f ? '°F' : '°C');
export function fmtTemp(c, f, digits = 1) {
  if (c == null || !Number.isFinite(c)) return '--';
  return toUnit(c, f).toFixed(digits) + '°';
}

// Flow (g/s) from a weight series, smoothed over ~1 s so the scale's own
// noise doesn't turn into spikes. Points are {t (ms), w (g)}.
export function withFlow(points) {
  const out = [];
  for (let i = 0; i < points.length; i += 1) {
    const p = points[i];
    let j = i;
    while (j > 0 && p.t - points[j - 1].t <= 1000) j -= 1;
    const q = points[j];
    const dt = (p.t - q.t) / 1000;
    const f = dt > 0.2 ? Math.max(0, (p.w - q.w) / dt) : 0;
    out.push({ t: p.t, w: p.w, f });
  }
  return out;
}

export const STEAM_LEVELS_C = [126, 128, 131];
