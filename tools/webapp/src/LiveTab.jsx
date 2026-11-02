// Live: the machine, the scale and the running shot, polled once a second
// (every five while the page is in the background). Power, brew temperature
// and steam can be changed here with the PIN. A shot can't be started from
// the phone, by design: the group only runs with someone at the machine.
import React, { useEffect, useRef, useState } from 'react';
import {
  Box, Button, Card, CardContent, Chip, CircularProgress, IconButton, Stack,
  Switch, ToggleButton, ToggleButtonGroup, Typography,
} from '@mui/material';
import { getJson } from './api.js';
import { ShotChart } from './charts.jsx';
import { STEAM_LEVELS_C, fmtTemp, toUnit, unitLabel, withFlow } from './util.js';

const LINK_TEXT = {
  connected: 'Connected',
  connecting: 'Connecting…',
  disconnected: 'Not connected',
  needs_token: 'Needs pairing',
  unconfigured: 'No machine set up',
};

const PHASE_TEXT = { brewing: 'Shot running', settling: 'Settling', review: 'Shot finished' };

function machineState(l) {
  if (l.link !== 'connected') return { text: LINK_TEXT[l.link] || l.link, color: 'default' };
  if (!l.on) return { text: 'Standby', color: 'default' };
  if (l.brewing) return { text: 'Brewing', color: 'primary' };
  if (l.heating) return { text: 'Heating', color: 'warning' };
  return { text: 'Ready', color: 'success' };
}

function TempCard({ title, temp, set, f, children }) {
  return (
    <Card sx={{ flex: 1, minWidth: 0 }}>
      <CardContent sx={{ '&:last-child': { pb: 2 } }}>
        <Typography variant="body2" color="text.secondary">{title}</Typography>
        <Typography sx={{ fontSize: 'clamp(2rem, 9vw, 2.8rem)', fontWeight: 300, lineHeight: 1.1 }}>
          {fmtTemp(temp, f)}
        </Typography>
        <Typography variant="body2" color="text.secondary" sx={{ mb: 1 }}>
          {set}
        </Typography>
        {children}
      </CardContent>
    </Card>
  );
}

export default function LiveTab({ theme, act, onLive }) {
  const [live, setLive] = useState(null);
  const [error, setError] = useState(false);
  const [pts, setPts] = useState([]);
  const series = useRef({ id: -1, total: 0, pts: [] });
  const [busy, setBusy] = useState('');

  useEffect(() => {
    let alive = true;
    let timer = null;
    const tick = async () => {
      const s = series.current;
      try {
        const l = await getJson(`/api/live?shot=${s.id}&since=${s.total}`);
        if (!alive) return;
        const shot = l.shot;
        const fresh = shot.pts.map(([t, w]) => ({ t, w }));
        if (shot.id !== s.id || shot.from === 0) {
          series.current = { id: shot.id, total: shot.total, pts: fresh };
        } else {
          series.current = { id: shot.id, total: shot.total, pts: s.pts.concat(fresh) };
        }
        setPts(series.current.pts);
        setLive(l);
        onLive?.(l);
        setError(false);
      } catch {
        if (alive) setError(true);
      }
      if (alive) timer = setTimeout(tick, document.hidden ? 5000 : 1000);
    };
    tick();
    return () => { alive = false; clearTimeout(timer); };
  }, []);

  const run = async (key, action, value) => {
    setBusy(key);
    try {
      await act(action, value);
    } finally {
      setTimeout(() => setBusy(''), 1500);  // the device confirms within a poll or two
    }
  };

  if (!live) {
    return (
      <Box sx={{ textAlign: 'center', py: 8 }}>
        {error
          ? <Typography color="error">Can't reach Tondo. Is it on the same WiFi?</Typography>
          : <CircularProgress />}
      </Box>
    );
  }

  const f = live.f;
  const st = machineState(live);
  const connected = live.link === 'connected';
  const on = connected && live.on;
  const shot = live.shot;
  const shotActive = shot.phase !== 'idle';
  const flowPts = withFlow(pts);
  const brewStep = (d) => {
    const v = Math.round((live.brew.set + d) * 2) / 2;
    run('brew', 'brew', Math.min(100, Math.max(80, v)).toFixed(1));
  };

  return (
    <Stack spacing={2}>
      {error && (
        <Typography color="warning.main" variant="body2">
          Lost contact. Retrying…
        </Typography>
      )}
      <Card>
        <CardContent sx={{ display: 'flex', alignItems: 'center', gap: 1.5, flexWrap: 'wrap',
                           '&:last-child': { pb: 2 } }}>
          <Chip label={st.text} color={st.color} />
          <Box sx={{ flexGrow: 1 }} />
          <Button variant={on ? 'outlined' : 'contained'} disabled={!connected || busy === 'power'}
                  onClick={() => run('power', 'power', on ? 0 : 1)}>
            {busy === 'power' ? 'Working…' : on ? 'Standby' : 'Turn on'}
          </Button>
        </CardContent>
      </Card>

      <Stack direction="row" spacing={2}>
        <TempCard title="Brew" temp={connected ? live.brew.t : null} f={f}
                  set={connected ? `Set ${fmtTemp(live.brew.set, f)}` : ''}>
          <Stack direction="row" alignItems="center" spacing={1}>
            <IconButton aria-label="Brew colder" disabled={!connected || live.brew.set <= 80}
                        onClick={() => brewStep(-0.5)}
                        sx={{ border: 1, borderColor: 'divider', width: 40, height: 40 }}>−</IconButton>
            <IconButton aria-label="Brew hotter" disabled={!connected || live.brew.set >= 100}
                        onClick={() => brewStep(0.5)}
                        sx={{ border: 1, borderColor: 'divider', width: 40, height: 40 }}>+</IconButton>
            <Typography variant="caption" color="text.secondary">
              {f ? '0.9' : '0.5'} {unitLabel(f)} steps
            </Typography>
          </Stack>
        </TempCard>
        <TempCard title="Steam" temp={connected ? live.steam.t : null} f={f}
                  set={!connected ? '' : live.steam.on ? `Set ${fmtTemp(live.steam.set, f, 0)}` : 'Off'}>
          <Stack direction="row" alignItems="center" spacing={1} useFlexGap sx={{ flexWrap: 'wrap' }}>
            <Switch checked={!!live.steam.on} disabled={!connected}
                    onChange={(e) => run('steam', 'steam', e.target.checked ? 1 : 0)} />
            <ToggleButtonGroup size="small" exclusive value={live.steam.level}
                               disabled={!connected || !live.steam.on}
                               onChange={(e, v) => v != null && run('lvl', 'steam_level', v)}>
              {STEAM_LEVELS_C.map((c, i) => (
                <ToggleButton key={i} value={i} sx={{ px: 1.2 }}>
                  {Math.round(toUnit(c, f))}°
                </ToggleButton>
              ))}
            </ToggleButtonGroup>
          </Stack>
        </TempCard>
      </Stack>

      <Card>
        <CardContent sx={{ '&:last-child': { pb: 2 } }}>
          <Stack direction="row" alignItems="baseline" spacing={2}>
            <Typography variant="body2" color="text.secondary" sx={{ flexGrow: 1 }}>
              {shotActive ? PHASE_TEXT[shot.phase] : 'Scale'}
            </Typography>
            {live.scale.batt >= 0 && (
              <Typography variant="body2" color={live.scale.batt <= 15 ? 'error' : 'text.secondary'}>
                Scale battery {live.scale.batt}%
              </Typography>
            )}
          </Stack>
          <Stack direction="row" alignItems="baseline" spacing={2} useFlexGap sx={{ flexWrap: 'wrap' }}>
            {shotActive ? (
              <>
                <Typography sx={{ fontSize: '2rem', fontWeight: 300, flexGrow: 1 }}>
                  {shot.g.toFixed(1)} g
                  {shot.target > 0 && (
                    <Typography component="span" color="text.secondary"> / {shot.target.toFixed(0)} g</Typography>
                  )}
                </Typography>
                <Typography sx={{ fontSize: '2rem', fontWeight: 300 }}>
                  {(shot.ms / 1000).toFixed(1)} s
                </Typography>
              </>
            ) : (
              <Typography sx={{ fontSize: '2rem', fontWeight: 300 }}>
                {live.scale.connected ? `${live.scale.g.toFixed(1)} g`
                  : live.scale.paired ? 'Not connected' : 'No scale'}
              </Typography>
            )}
          </Stack>
          {flowPts.length >= 2 && (
            <Box sx={{ mt: 1 }}>
              {!shotActive && (
                <Typography variant="caption" color="text.secondary">Last shot</Typography>
              )}
              <ShotChart samples={flowPts} target={shot.target} theme={theme}
                         minSeconds={shot.phase === 'brewing' ? 30 : 0} />
            </Box>
          )}
          {flowPts.length < 2 && (
            <Typography variant="body2" color="text.secondary" sx={{ mt: 1 }}>
              The shot graph shows here while a shot runs.
            </Typography>
          )}
        </CardContent>
      </Card>
    </Stack>
  );
}
