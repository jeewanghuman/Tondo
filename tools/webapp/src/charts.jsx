// The SVG charts: the shot graph (one shot, a running shot, or two shots
// overlaid for comparison) and the small bar/line charts on Trends. Drawn in
// the device's palette, so the page always matches the screen.
import React from 'react';

const niceStep = (v) => {
  const steps = [1, 2, 3, 4, 5, 6, 8, 10, 15, 20, 30, 40, 50, 60, 80, 100];
  for (const s of steps) if (v <= s) return s;
  return Math.ceil(v / 50) * 50;
};
// Chart text in viewBox units: the 640-wide charts draw at roughly half size
// on a phone, so 18 lands near 9-10 px there and stays readable.
const FS = 18;
const tick = (v) => (Number.isInteger(v) ? v : +v.toFixed(2));

// samples: [{t ms, w g, f g/s}]; compare: the same, drawn dashed underneath.
// minSeconds keeps a running shot's time axis from jumping around early.
export function ShotChart({ samples, compare, target, theme, minSeconds = 0, id }) {
  const W = 640, H = 320, L = 52, R = 56, T = 34, B = 30;
  const pw = W - L - R, ph = H - T - B;
  const all = [...(samples || []), ...(compare || [])];
  if (all.length < 2) return null;
  const tEnd = Math.max(minSeconds * 1000, ...all.map((s) => s.t), 1000);
  const wmax = niceStep(Math.max(...all.map((s) => s.w), target || 0, 1) * 1.05);
  const fmax = niceStep(Math.max(...all.map((s) => s.f), 0.5) * 1.05);
  const x = (t) => L + (t / tEnd) * pw;
  const yw = (w) => T + ph - (Math.max(w, 0) / wmax) * ph;
  const yf = (f) => T + ph - (Math.max(f, 0) / fmax) * ph;
  const path = (pts, fn, key) =>
    pts.map((s, i) => `${i ? 'L' : 'M'}${x(s.t).toFixed(1)},${fn(s[key]).toFixed(1)}`).join('');
  const grid = [0, 1, 2, 3, 4];
  const have = samples && samples.length >= 2;
  const last = have ? samples[samples.length - 1] : null;
  const base = `L${x(last ? last.t : 0)},${T + ph}L${L},${T + ph}Z`;
  return (
    <svg id={id} viewBox={`0 0 ${W} ${H}`} style={{ width: '100%', height: 'auto', display: 'block' }}>
      {grid.map((i) => (
        <line key={i} x1={L} x2={W - R} y1={T + (ph * i) / 4} y2={T + (ph * i) / 4}
              stroke={theme.rail} strokeWidth="1" />
      ))}
      {grid.map((i) => (
        <text key={'l' + i} x={L - 6} y={T + (ph * i) / 4 + 6} textAnchor="end"
              fontSize={FS} fill={theme.muted}>{tick(wmax * (4 - i) / 4)}</text>
      ))}
      {grid.map((i) => (
        <text key={'r' + i} x={W - R + 6} y={T + (ph * i) / 4 + 4} textAnchor="start"
              fontSize={FS} fill={theme.muted}>{tick(fmax * (4 - i) / 4)}</text>
      ))}
      {compare && compare.length >= 2 && (
        <>
          <path d={path(compare, yf, 'f')} fill="none" stroke={theme.ok} strokeWidth="1.5"
                strokeDasharray="5 4" opacity="0.6" />
          <path d={path(compare, yw, 'w')} fill="none" stroke={theme.compare} strokeWidth="2"
                strokeDasharray="6 4" />
        </>
      )}
      {have && (
        <>
          <path d={path(samples, yf, 'f') + base} fill={theme.ok} opacity="0.1" />
          <path d={path(samples, yw, 'w') + base} fill={theme.accent} opacity="0.1" />
        </>
      )}
      {target > 0 && target <= wmax && (
        <line x1={L} x2={W - R} y1={yw(target)} y2={yw(target)}
              stroke={theme.warn} strokeWidth="2" strokeDasharray="6 4" />
      )}
      {have && (
        <>
          <path d={path(samples, yf, 'f')} fill="none" stroke={theme.ok} strokeWidth="2" />
          <path d={path(samples, yw, 'w')} fill="none" stroke={theme.accent} strokeWidth="2.5" />
          <circle cx={x(last.t)} cy={yf(last.f)} r="4" fill={theme.ok} stroke={theme.bg} strokeWidth="2" />
          <circle cx={x(last.t)} cy={yw(last.w)} r="4" fill={theme.accent} stroke={theme.bg} strokeWidth="2" />
        </>
      )}
      <text x={L} y={20} fontSize={FS} fill={theme.muted}>
        <tspan fill={theme.accent}>■</tspan> Weight g
        {compare && compare.length >= 2 && (
          <tspan dx="10"><tspan fill={theme.compare}>■</tspan> Compared</tspan>
        )}
      </text>
      <text x={W - R} y={20} fontSize={FS} fill={theme.muted} textAnchor="end">
        Flow g/s <tspan fill={theme.ok}>■</tspan>
      </text>
      <text x={L} y={H - 6} fontSize={FS} fill={theme.muted}>0 s</text>
      <text x={W - R} y={H - 6} fontSize={FS} fill={theme.muted} textAnchor="end">
        {(tEnd / 1000).toFixed(0)} s
      </text>
    </svg>
  );
}

// Bars (one per bucket) with optional value labels. points: [{label, v}].
export function BarChart({ points, theme, unit = '', height = 200 }) {
  const W = 640, H = height, L = 40, R = 8, T = 16, B = 30;
  const pw = W - L - R, ph = H - T - B;
  if (!points.length) return null;
  const vmax = niceStep(Math.max(...points.map((p) => p.v), 1));
  const bw = pw / points.length;
  const every = Math.ceil(points.length / 5);  // keep the axis labels apart
  return (
    <svg viewBox={`0 0 ${W} ${H}`} style={{ width: '100%', height: 'auto', display: 'block' }}>
      {[0, 1, 2].map((i) => (
        <g key={i}>
          <line x1={L} x2={W - R} y1={T + (ph * i) / 2} y2={T + (ph * i) / 2}
                stroke={theme.rail} strokeWidth="1" />
          <text x={L - 4} y={T + (ph * i) / 2 + 4} textAnchor="end" fontSize={FS}
                fill={theme.muted}>{tick(vmax * (2 - i) / 2)}</text>
        </g>
      ))}
      {points.map((p, i) => {
        const h = (p.v / vmax) * ph;
        return (
          <g key={i}>
            <rect x={L + i * bw + bw * 0.15} y={T + ph - h} width={bw * 0.7} height={Math.max(h, 0)}
                  rx="2" fill={theme.accent} opacity={p.v ? 0.9 : 0}>
              <title>{`${p.label}: ${p.v}${unit}`}</title>
            </rect>
            {i % every === 0 && (
              <text x={L + i * bw + bw / 2} y={H - 8} textAnchor="middle" fontSize={FS}
                    fill={theme.muted}>{p.label}</text>
            )}
          </g>
        );
      })}
    </svg>
  );
}

// One or two lines over the same buckets. series: [{name, color, points:
// [{label, v|null}], unit}] -- each series gets its own scale (left/right).
export function LineChart({ series, theme, height = 230 }) {
  const W = 640, H = height, L = 56, R = 56, T = 34, B = 30;
  const pw = W - L - R, ph = H - T - B;
  const n = series[0]?.points.length || 0;
  if (n === 0) return null;
  const x = (i) => L + (n === 1 ? pw / 2 : (i / (n - 1)) * pw);
  const every = Math.ceil(n / 5);
  const scales = series.map((s) => {
    const vals = s.points.map((p) => p.v).filter((v) => v != null);
    if (!vals.length) return { lo: 0, hi: 1 };
    let lo = Math.min(...vals), hi = Math.max(...vals);
    if (s.fixed) [lo, hi] = s.fixed;
    const pad = Math.max((hi - lo) * 0.15, s.minSpan || 1);
    return { lo: Math.max(s.floor ?? -Infinity, lo - pad), hi: hi + pad };
  });
  const y = (k, v) => T + ph - ((v - scales[k].lo) / (scales[k].hi - scales[k].lo)) * ph;
  return (
    <svg viewBox={`0 0 ${W} ${H}`} style={{ width: '100%', height: 'auto', display: 'block' }}>
      {[0, 1, 2].map((i) => (
        <line key={i} x1={L} x2={W - R} y1={T + (ph * i) / 2} y2={T + (ph * i) / 2}
              stroke={theme.rail} strokeWidth="1" />
      ))}
      {series.map((s, k) => [0, 2].map((i) => (
        <text key={`${k}${i}`} x={k === 0 ? L - 4 : W - R + 4} y={T + (ph * i) / 2 + 4}
              textAnchor={k === 0 ? 'end' : 'start'} fontSize={FS} fill={s.color}>
          {tick(+(scales[k].lo + ((scales[k].hi - scales[k].lo) * (2 - i)) / 2).toFixed(1))}
        </text>
      )))}
      {series.map((s, k) => {
        let d = '';
        let pen = false;
        s.points.forEach((p, i) => {
          if (p.v == null) { pen = false; return; }
          d += `${pen ? 'L' : 'M'}${x(i).toFixed(1)},${y(k, p.v).toFixed(1)}`;
          pen = true;
        });
        return (
          <g key={k}>
            <path d={d} fill="none" stroke={s.color} strokeWidth="2.5" />
            {s.points.map((p, i) => p.v != null && (
              <circle key={i} cx={x(i)} cy={y(k, p.v)} r="3.5" fill={s.color}>
                <title>{`${p.label}: ${p.v.toFixed(1)}${s.unit || ''}`}</title>
              </circle>
            ))}
          </g>
        );
      })}
      {series[0].points.map((p, i) => i % every === 0 && (
        <text key={i} x={x(i)} y={H - 8} textAnchor="middle" fontSize={FS} fill={theme.muted}>
          {p.label}
        </text>
      ))}
      <text x={L} y={20} fontSize={FS} fill={theme.muted}>
        {series.map((s, k) => (
          <tspan key={k} dx={k ? 12 : 0}><tspan fill={s.color}>■</tspan> {s.name}</tspan>
        ))}
      </text>
    </svg>
  );
}
