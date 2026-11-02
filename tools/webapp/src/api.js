// Talking to the device. Reads are open; every change carries the PIN shown
// on the device (Settings > Tondo > WiFi > Phone PIN), kept in this browser's
// localStorage once it has worked. A 403 forgets it, so the page asks again.

const PIN_KEY = 'tondo-pin';

export function storedPin() {
  try {
    return localStorage.getItem(PIN_KEY) || '';
  } catch {
    return '';
  }
}

export function savePin(pin) {
  try {
    if (pin) localStorage.setItem(PIN_KEY, pin);
    else localStorage.removeItem(PIN_KEY);
  } catch {
    /* private mode: the PIN lasts this page only */
  }
}

export async function getJson(path) {
  const r = await fetch(path, { cache: 'no-store' });
  if (!r.ok) throw new Error(`${path}: ${r.status}`);
  return r.json();
}

export class PinError extends Error {}

// POST form fields (+ the PIN). Resolves the JSON body; rejects with
// PinError on a wrong/missing PIN, Error(message) otherwise.
export async function post(path, fields = {}, pin = storedPin()) {
  const body = new URLSearchParams({ ...fields, pin });
  const r = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body,
  });
  let data = {};
  try {
    data = await r.json();
  } catch {
    /* empty */
  }
  if (r.status === 403) {
    savePin('');
    throw new PinError(data.error || 'Wrong PIN');
  }
  if (!r.ok) throw new Error(data.error || `Request failed (${r.status})`);
  return data;
}

// Parse a shot's samples CSV (t_ms,weight_g,flow_gps).
export async function getSamples(id) {
  const r = await fetch(`/api/shot.csv?id=${id}`);
  if (!r.ok) return [];
  const text = await r.text();
  return text
    .trim()
    .split('\n')
    .slice(1)
    .map((l) => {
      const [t, w, f] = l.split(',').map(Number);
      return { t, w, f };
    })
    .filter((s) => Number.isFinite(s.t));
}
