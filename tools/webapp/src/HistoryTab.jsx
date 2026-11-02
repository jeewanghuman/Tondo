// History: every shot on the card, with filters (period, beans), notes and a
// rating per shot, a side-by-side overlay of two shots, delete, and exports.
import React, { useEffect, useMemo, useState } from 'react';
import {
  Autocomplete, Box, Button, Card, CardContent, Chip, CircularProgress, Dialog,
  DialogActions, DialogContent, DialogTitle, Link, MenuItem, Rating, Stack,
  Table, TableBody, TableCell, TableHead, TableRow, TextField, Typography,
} from '@mui/material';
import { getSamples } from './api.js';
import { ShotChart } from './charts.jsx';
import {
  diffOf, fmtDay, fmtWhen, modeLabel, modeTag, monthKey, monthLabel, shotFileBase,
} from './util.js';

function StatCard({ value, caption }) {
  return (
    <Card sx={{ flex: 1, textAlign: 'center', minWidth: 0 }}>
      <CardContent sx={{ py: 1.5, px: 0.5, '&:last-child': { pb: 1.5 } }}>
        <Typography noWrap sx={{ fontWeight: 500, fontSize: 'clamp(1.15rem, 5.5vw, 2.125rem)' }}>
          {value}
        </Typography>
        <Typography variant="body2" color="text.secondary"
                    sx={{ lineHeight: 1.2, fontSize: { xs: '0.72rem', sm: '0.875rem' } }}>
          {caption}
        </Typography>
      </CardContent>
    </Card>
  );
}

// "Download card": the dialog's header, stats and graph as one PNG.
function downloadCard(shot, svgEl, theme, textColor) {
  const W = 1600, H = 960, M = 48;
  const canvas = document.createElement('canvas');
  canvas.width = W;
  canvas.height = H;
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = theme.bg;
  ctx.fillRect(0, 0, W, H);
  ctx.fillStyle = theme.muted;
  ctx.font = '28px system-ui, sans-serif';
  ctx.textBaseline = 'top';
  ctx.fillText(fmtWhen(shot.unix) + (shot.bean ? `  ·  ${shot.bean}` : ''), M, M);
  ctx.textAlign = 'right';
  ctx.fillText(shot.rating ? '★'.repeat(shot.rating) : modeLabel(shot), W - M, M);
  ctx.textAlign = 'left';
  const diff = diffOf(shot);
  const stats = [
    [shot.final_g.toFixed(1) + ' g', 'Result', textColor],
    [shot.target_g > 0 ? shot.target_g.toFixed(0) + ' g' : '-', 'Target', textColor],
    [diff != null ? (diff >= 0 ? '+' : '') + diff.toFixed(1) + ' g' : '-', 'Diff',
      diff == null ? textColor : Math.abs(diff) <= 2 ? theme.ok : theme.warn],
    [(shot.duration_ms / 1000).toFixed(1) + ' s', 'Time', textColor],
    [shot.avg_gps.toFixed(2) + ' g/s', 'Avg flow', textColor],
  ];
  const colW = (W - 2 * M) / stats.length;
  stats.forEach(([value, caption, color], i) => {
    const cx = M + colW * i + colW / 2;
    ctx.textAlign = 'center';
    ctx.fillStyle = color;
    ctx.font = '600 52px system-ui, sans-serif';
    ctx.fillText(value, cx, 120);
    ctx.fillStyle = theme.muted;
    ctx.font = '26px system-ui, sans-serif';
    ctx.fillText(caption, cx, 190);
  });
  const clone = svgEl.cloneNode(true);
  clone.setAttribute('width', W - 2 * M);
  clone.setAttribute('height', H - 300);
  const xml = new XMLSerializer().serializeToString(clone);
  const img = new Image();
  img.onload = () => {
    ctx.drawImage(img, M, 250, W - 2 * M, H - 300);
    canvas.toBlob((blob) => {
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `${shotFileBase(shot)}.png`;
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 5000);
    }, 'image/png');
  };
  img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(xml);
}

const PERIODS = [
  { key: 'all', label: 'All' },
  { key: 7, label: '7 days' },
  { key: 30, label: '30 days' },
];

function ShotDialog({ shot, shots, beans, notesOn, theme, textColor, postPin, onChanged, onClose }) {
  const [samples, setSamples] = useState(null);
  const [cmpId, setCmpId] = useState('');
  const [cmp, setCmp] = useState(null);
  const [rating, setRating] = useState(shot.rating || 0);
  const [bean, setBean] = useState(shot.bean || '');
  const [note, setNote] = useState(shot.note || '');
  const [saving, setSaving] = useState(false);
  const [msg, setMsg] = useState('');
  const [confirmDel, setConfirmDel] = useState(false);

  useEffect(() => {
    getSamples(shot.id).then(setSamples).catch(() => setSamples([]));
  }, [shot.id]);
  useEffect(() => {
    if (!cmpId) { setCmp(null); return; }
    getSamples(cmpId).then(setCmp).catch(() => setCmp(null));
  }, [cmpId]);

  const cmpShot = shots.find((s) => s.id === cmpId);
  const dirty = rating !== (shot.rating || 0) || bean !== (shot.bean || '') || note !== (shot.note || '');
  const diff = diffOf(shot);

  const save = async () => {
    setSaving(true);
    setMsg('');
    try {
      await postPin('/api/shot/note', { id: shot.id, rating, bean: bean.trim(), note: note.trim() });
      onChanged({ ...shot, rating, bean: bean.trim(), note: note.trim() });
      setMsg('Saved');
    } catch (e) {
      setMsg(e.message);
    } finally {
      setSaving(false);
    }
  };
  const del = async () => {
    try {
      await postPin('/api/shot/delete', { id: shot.id });
      onChanged(null);
      onClose();
    } catch (e) {
      setMsg(e.message);
      setConfirmDel(false);
    }
  };

  return (
    <Dialog open onClose={onClose} maxWidth="md" fullWidth>
      <DialogTitle sx={{ display: 'flex', justifyContent: 'space-between', gap: 2 }}>
        <span>{fmtWhen(shot.unix)}</span>
        <Typography component="span" color="text.secondary">{modeLabel(shot)}</Typography>
      </DialogTitle>
      <DialogContent>
        <Stack direction="row" spacing={3} useFlexGap sx={{ mb: 2, flexWrap: 'wrap', rowGap: 1 }}>
          <Typography><b>{shot.final_g.toFixed(1)} g</b> result</Typography>
          {shot.target_g > 0 && <Typography><b>{shot.target_g.toFixed(0)} g</b> target</Typography>}
          {diff != null && (
            <Typography sx={{ color: Math.abs(diff) <= 2 ? 'success.main' : 'warning.main' }}>
              <b>{(diff >= 0 ? '+' : '') + diff.toFixed(1)} g</b> diff
            </Typography>
          )}
          <Typography><b>{(shot.duration_ms / 1000).toFixed(1)} s</b> time</Typography>
          <Typography><b>{shot.avg_gps.toFixed(2)} g/s</b> avg flow</Typography>
        </Stack>
        {samples == null
          ? <Box sx={{ textAlign: 'center', py: 4 }}><CircularProgress size={28} /></Box>
          : <ShotChart id="shot-chart" samples={samples} compare={cmp} target={shot.target_g} theme={theme} />}
        <TextField select fullWidth size="small" label="Compare with" value={cmpId}
                   onChange={(e) => setCmpId(e.target.value)} sx={{ mt: 2 }}>
          <MenuItem value="">Nothing</MenuItem>
          {shots.filter((s) => s.id !== shot.id).slice(0, 100).map((s) => (
            <MenuItem key={s.id} value={s.id}>
              {fmtWhen(s.unix)} · {s.final_g.toFixed(1)} g · {(s.duration_ms / 1000).toFixed(0)} s
              {s.bean ? ` · ${s.bean}` : ''}
            </MenuItem>
          ))}
        </TextField>
        {cmpShot && (
          <Typography variant="body2" color="text.secondary" sx={{ mt: 1 }}>
            Dashed: {cmpShot.final_g.toFixed(1)} g in {(cmpShot.duration_ms / 1000).toFixed(1)} s
            {' '}({(shot.final_g - cmpShot.final_g >= 0 ? '+' : '')}
            {(shot.final_g - cmpShot.final_g).toFixed(1)} g,
            {' '}{((shot.duration_ms - cmpShot.duration_ms) / 1000 >= 0 ? '+' : '')}
            {((shot.duration_ms - cmpShot.duration_ms) / 1000).toFixed(1)} s for this one)
          </Typography>
        )}

        {notesOn && (
          <Stack spacing={1.5} sx={{ mt: 3 }}>
            <Stack direction="row" alignItems="center" spacing={1}>
              <Typography variant="body2" color="text.secondary">Rating</Typography>
              <Rating value={rating} onChange={(e, v) => setRating(v || 0)} />
            </Stack>
            <Autocomplete freeSolo options={beans} value={bean} inputValue={bean}
                          onInputChange={(e, v) => setBean(v.slice(0, 47))}
                          renderInput={(p) => <TextField {...p} size="small" label="Beans" />} />
            <TextField size="small" label="Notes" multiline minRows={2} value={note}
                       inputProps={{ maxLength: 239 }}
                       onChange={(e) => setNote(e.target.value)} />
            <Stack direction="row" spacing={2} alignItems="center">
              <Button variant="contained" disabled={!dirty || saving} onClick={save}>Save notes</Button>
              <Typography variant="body2" color="text.secondary">{msg}</Typography>
            </Stack>
          </Stack>
        )}
        <Stack direction="row" spacing={2} useFlexGap sx={{ mt: 3, flexWrap: 'wrap', rowGap: 1 }}>
          <Link component="button" underline="hover"
                disabled={samples == null || samples.length < 2}
                onClick={() => {
                  const el = document.getElementById('shot-chart');
                  if (el) downloadCard({ ...shot, rating, bean }, el, theme, textColor);
                }}>
            Download card (PNG)
          </Link>
          <Link href={`/api/shot.csv?id=${shot.id}`} download={`${shotFileBase(shot)}.csv`}>
            Download data (CSV)
          </Link>
          <Box sx={{ flexGrow: 1 }} />
          <Link component="button" underline="hover" color="error" onClick={() => setConfirmDel(true)}>
            Delete shot
          </Link>
        </Stack>
      </DialogContent>
      <Dialog open={confirmDel} onClose={() => setConfirmDel(false)}>
        <DialogTitle>Delete this shot?</DialogTitle>
        <DialogContent>
          <Typography>It's removed from the card for good, with its notes.</Typography>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setConfirmDel(false)}>Cancel</Button>
          <Button color="error" variant="contained" onClick={del}>Delete</Button>
        </DialogActions>
      </Dialog>
    </Dialog>
  );
}

export default function HistoryTab({ summary, shots, setShots, theme, textColor, postPin }) {
  const [period, setPeriod] = useState('all');  // 'all' | days | year*100+month
  const [bean, setBean] = useState('');
  const [open, setOpen] = useState(null);

  const months = useMemo(() => {
    const seen = [];
    for (const s of shots) {
      const ym = monthKey(s.unix);
      if (!seen.includes(ym)) seen.push(ym);
    }
    return seen;
  }, [shots]);
  const beans = useMemo(
    () => [...new Set(shots.map((s) => s.bean).filter(Boolean))].sort((a, b) => a.localeCompare(b)),
    [shots],
  );

  const visible = useMemo(() => {
    const now = Date.now() / 1000;
    return shots.filter((s) => {
      if (bean && s.bean !== bean) return false;
      if (period === 'all') return true;
      if (period < 1000) return now - s.unix <= period * 86400;
      return monthKey(s.unix) === period;
    });
  }, [shots, period, bean]);

  const changed = (id) => (next) => {
    setShots((list) => (next == null ? list.filter((s) => s.id !== id)
      : list.map((s) => (s.id === id ? next : s))));
    if (next) setOpen(next);
  };

  return (
    <Stack spacing={2}>
      <Stack direction="row" spacing={2}>
        <StatCard value={summary.stats.total}
                  caption={summary.stats.since > 0
                    ? `Shots since ${fmtDay(summary.stats.since)}` : 'Total shots'} />
        <StatCard value={summary.stats.life > 0 ? summary.stats.life.toFixed(1) + '%' : '—'}
                  caption="Lifetime accuracy" />
        <StatCard value={summary.stats.d30 > 0 ? summary.stats.d30.toFixed(1) + '%' : '—'}
                  caption="30-day accuracy" />
      </Stack>
      <Stack direction="row" sx={{ flexWrap: 'wrap', gap: 1 }}>
        {PERIODS.map((p) => (
          <Chip key={p.key} label={p.label} color={period === p.key ? 'primary' : 'default'}
                onClick={() => setPeriod(p.key)} />
        ))}
        {months.map((ym) => (
          <Chip key={ym} label={monthLabel(ym)} color={period === ym ? 'primary' : 'default'}
                onClick={() => setPeriod(ym)} />
        ))}
      </Stack>
      {beans.length > 0 && (
        <TextField select size="small" label="Beans" value={bean} sx={{ maxWidth: 320 }}
                   onChange={(e) => setBean(e.target.value)}>
          <MenuItem value="">All beans</MenuItem>
          {beans.map((b) => <MenuItem key={b} value={b}>{b}</MenuItem>)}
        </TextField>
      )}
      <Card>
        <Table size="small">
          <TableHead>
            <TableRow>
              <TableCell>When</TableCell>
              <TableCell sx={{ display: { xs: 'none', sm: 'table-cell' } }}>Beans</TableCell>
              <TableCell align="right">Result</TableCell>
              <TableCell align="right">Diff</TableCell>
              <TableCell align="right">Time</TableCell>
              <TableCell align="right" sx={{ display: { xs: 'none', sm: 'table-cell' } }}>Avg flow</TableCell>
            </TableRow>
          </TableHead>
          <TableBody>
            {visible.map((s) => {
              const d = diffOf(s);
              return (
                <TableRow key={s.id} hover sx={{ cursor: 'pointer' }} onClick={() => setOpen(s)}>
                  <TableCell>
                    {fmtWhen(s.unix)}
                    <Typography variant="caption" color="text.secondary" component="div">
                      {modeTag(s)}{s.rating ? ` · ${'★'.repeat(s.rating)}` : ''}
                      <Box component="span" sx={{ display: { sm: 'none' } }}>
                        {s.bean ? ` · ${s.bean}` : ''}
                      </Box>
                    </Typography>
                  </TableCell>
                  <TableCell sx={{ display: { xs: 'none', sm: 'table-cell' }, color: 'text.secondary' }}>
                    {s.bean || ''}
                  </TableCell>
                  <TableCell align="right">
                    {s.final_g.toFixed(1)}{s.target_g > 0 ? ` / ${s.target_g.toFixed(0)}` : ''} g
                  </TableCell>
                  <TableCell align="right"
                             sx={{ color: d == null ? 'text.secondary'
                               : Math.abs(d) <= 2 ? 'success.main' : 'warning.main' }}>
                    {d == null ? '—' : (d >= 0 ? '+' : '') + d.toFixed(1)}
                  </TableCell>
                  <TableCell align="right">{Math.round(s.duration_ms / 1000)} s</TableCell>
                  <TableCell align="right" sx={{ display: { xs: 'none', sm: 'table-cell' } }}>
                    {s.avg_gps.toFixed(2)} g/s
                  </TableCell>
                </TableRow>
              );
            })}
            {visible.length === 0 && (
              <TableRow><TableCell colSpan={6}>
                <Typography color="text.secondary" align="center" sx={{ py: 2 }}>
                  {shots.length ? 'No shots match these filters' : 'No shots recorded yet'}
                </Typography>
              </TableCell></TableRow>
            )}
          </TableBody>
        </Table>
      </Card>
      {shots.length > 0 && (
        <Stack direction="row" spacing={2} useFlexGap sx={{ flexWrap: 'wrap', rowGap: 1 }}>
          <Button variant="outlined" href="/api/export.csv" download="tondo-shots.csv">
            Export shots (CSV)
          </Button>
          <Button variant="outlined" href="/api/export_samples.csv" download="tondo-shot-samples.csv">
            Export every graph (CSV)
          </Button>
        </Stack>
      )}
      {open && (
        <ShotDialog key={open.id} shot={open} shots={shots} beans={beans}
                    notesOn={summary.notes} theme={theme} textColor={textColor}
                    postPin={postPin} onChanged={changed(open.id)} onClose={() => setOpen(null)} />
      )}
    </Stack>
  );
}
