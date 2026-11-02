// Settings from the phone: the same values as Settings on the device, applied
// through the device's own controls (so the screen follows), plus updates,
// the settings backup as a file, and the page's own name.
import React, { useEffect, useRef, useState } from 'react';
import {
  Alert, Box, Button, Card, CardContent, CircularProgress, Dialog,
  DialogActions, DialogContent, DialogTitle, Divider, FormControlLabel,
  IconButton, Link, MenuItem, Slider, Stack, Switch, TextField, Typography,
} from '@mui/material';
import { getJson, post } from './api.js';

const TIMEOUTS = [[0, 'Never'], [1, '1 min'], [5, '5 min'], [15, '15 min'], [30, '30 min']];
const IDLE = [[0, 'Lion'], [1, 'Dim'], [2, 'Screen off'], [3, 'Clock']];
const FLUSH = [[0, 'Off'], [3, '3 s'], [6, '6 s'], [9, '9 s'], [15, '15 s']];
const DELAY = [[3, '3 s'], [6, '6 s'], [9, '9 s'], [15, '15 s']];
const CADENCE = [[0, 'Off'], [1, 'On boot'], [2, 'Daily'], [3, 'Hourly']];
const RANDOM_MELODY = 255;

function Section({ title, children }) {
  return (
    <Card>
      <CardContent sx={{ '&:last-child': { pb: 2 } }}>
        <Typography variant="subtitle1" sx={{ mb: 1 }}>{title}</Typography>
        <Stack spacing={1.5}>{children}</Stack>
      </CardContent>
    </Card>
  );
}

function Row({ label, children }) {
  return (
    <Stack direction="row" alignItems="center" spacing={2} sx={{ minHeight: 40 }}>
      <Typography sx={{ flexGrow: 1 }}>{label}</Typography>
      {children}
    </Stack>
  );
}

function Pick({ value, options, onChange }) {
  return (
    <TextField select size="small" value={value} sx={{ minWidth: 140 }}
               onChange={(e) => onChange(Number(e.target.value))}>
      {options.map(([v, l]) => <MenuItem key={v} value={v}>{l}</MenuItem>)}
    </TextField>
  );
}

export default function SettingsTab({ summary, postPin, ensurePin, pinOk, lock }) {
  const [s, setS] = useState(null);
  const [live, setLive] = useState(null);
  const [err, setErr] = useState('');
  const [name, setName] = useState('');
  const [nameMsg, setNameMsg] = useState('');
  const [inclWifi, setInclWifi] = useState(false);
  const [inclToken, setInclToken] = useState(false);
  const [restoreText, setRestoreText] = useState(null);
  const [restoreMsg, setRestoreMsg] = useState('');
  const [checking, setChecking] = useState(false);
  const [installAsk, setInstallAsk] = useState(false);
  const [installMsg, setInstallMsg] = useState('');
  const fileRef = useRef(null);
  const checkSeq = useRef(null);  // the check count before "Check now"

  const load = () => getJson('/api/settings').then((v) => {
    setS(v);
    setName((cur) => cur || v.name);
  }).catch(() => setErr("Can't read the settings from Tondo."));

  useEffect(() => { load(); }, []);
  // Update status rides on the live view; poll it while this tab is open.
  useEffect(() => {
    let alive = true;
    let t = null;
    const tick = () => getJson('/api/live?shot=-1&since=0')
      .then((l) => { if (alive) setLive(l); })
      .catch(() => {})
      .finally(() => { if (alive) t = setTimeout(tick, checking ? 1500 : 4000); });
    tick();
    return () => { alive = false; clearTimeout(t); };
  }, [checking]);
  useEffect(() => {
    if (checking && live && !live.update.checking && checkSeq.current != null
        && live.update.seq !== checkSeq.current) {
      setChecking(false);
      load();  // the notes come with the settings
    }
  }, [live, checking]);

  const set = async (key, value) => {
    setErr('');
    setS((cur) => ({ ...cur, values: { ...cur.values, [key]: value } }));
    try {
      await postPin('/api/settings', { key, value });
    } catch (e) {
      setErr(e.message);
    }
    setTimeout(load, 900);  // the device applies it on its next refresh
  };

  if (!s) {
    return (
      <Box sx={{ textAlign: 'center', py: 8 }}>
        {err ? <Typography color="error">{err}</Typography> : <CircularProgress />}
      </Box>
    );
  }
  const v = s.values;
  const sw = (key, label) => (
    <Row label={label}>
      <Switch checked={!!v[key]} onChange={(e) => set(key, e.target.checked ? 1 : 0)} />
    </Row>
  );
  const melodies = [[0, 'Off'], ...s.melodies.map((m, i) => [i + 1, m]), [RANDOM_MELODY, 'Random']];
  const up = live?.update;

  const saveName = async () => {
    setNameMsg('');
    try {
      await postPin('/api/settings', { key: 'name', value: name.trim().toLowerCase() });
      setNameMsg(`Saved. The page is now at http://${name.trim().toLowerCase()}.local`);
      load();
    } catch (e) {
      setNameMsg(e.message);
    }
  };

  const download = async () => {
    const pin = await ensurePin();
    if (!pin) return;
    const q = new URLSearchParams({ wifi: inclWifi ? 1 : 0, token: inclToken ? 1 : 0, pin });
    window.location.href = `/api/backup.txt?${q}`;
  };

  const readFile = (e) => {
    const f = e.target.files?.[0];
    e.target.value = '';
    if (!f) return;
    f.text().then((t) => {
      setRestoreMsg('');
      if (!/^format=1$/m.test(t) || !/^version=/m.test(t)) {
        setRestoreMsg("That file isn't a Tondo settings backup.");
        return;
      }
      setRestoreText(t);
    });
  };
  const restore = async () => {
    try {
      const r = await postPin('/api/restore', { text: restoreText });
      setRestoreText(null);
      setRestoreMsg(`Restored ${r.restored} settings. Tondo is restarting; this page reconnects by itself.`);
    } catch (e) {
      setRestoreText(null);
      setRestoreMsg(e.message);
    }
  };

  const check = async () => {
    checkSeq.current = up ? up.seq : null;
    setChecking(true);
    try {
      await post('/api/update/check');
    } catch (e) {
      setChecking(false);
      setErr(e.message);
    }
  };
  const install = async () => {
    setInstallAsk(false);
    try {
      await postPin('/api/update/install');
      setInstallMsg('Installing. Tondo restarts on its own; give it about a minute.');
    } catch (e) {
      setInstallMsg(e.message);
    }
  };

  return (
    <Stack spacing={2}>
      {err && <Alert severity="warning" onClose={() => setErr('')}>{err}</Alert>}
      <Section title="Phone access">
        {pinOk ? (
          <Row label="Changes are unlocked on this phone.">
            <Button onClick={lock}>Lock</Button>
          </Row>
        ) : (
          <Row label="Changes need the PIN shown on Tondo: Settings › Tondo › WiFi › Phone PIN.">
            <Button variant="contained" onClick={() => ensurePin()}>Unlock</Button>
          </Row>
        )}
        <Stack direction="row" spacing={1} alignItems="center">
          <TextField size="small" label="Name on the network" value={name}
                     onChange={(e) => setName(e.target.value)}
                     helperText={`http://${(name || s.name).trim().toLowerCase()}.local`} />
          <Button disabled={!name.trim() || name.trim().toLowerCase() === s.name} onClick={saveName}>
            Save
          </Button>
        </Stack>
        {nameMsg && <Typography variant="body2" color="text.secondary">{nameMsg}</Typography>}
      </Section>

      <Section title="Display">
        {s.can_dim && (
          <Row label="Brightness">
            <Slider sx={{ maxWidth: 200 }} min={10} max={100} step={10} value={v.brightness}
                    valueLabelDisplay="auto"
                    onChange={(e, x) => setS((c) => ({ ...c, values: { ...c.values, brightness: x } }))}
                    onChangeCommitted={(e, x) => set('brightness', x)} />
          </Row>
        )}
        <Row label="Screen dims after">
          <Pick value={v.screen_timeout} options={TIMEOUTS} onChange={(x) => set('screen_timeout', x)} />
        </Row>
        {v.screen_timeout > 0 && (
          <Row label="Idle screen">
            <Pick value={v.idle_screen} options={IDLE} onChange={(x) => set('idle_screen', x)} />
          </Row>
        )}
        {sw('fahrenheit', 'Fahrenheit')}
        {sw('clock_24h', '24-hour clock')}
      </Section>

      {s.sound && (
        <Section title="Sound">
          {sw('click_sound', 'Button sounds')}
          {sw('shot_sounds', 'Shot sounds')}
          {sw('milk_sounds', 'Milk sounds')}
          {sw('alert_sounds', 'Alerts')}
          <Row label="Volume">
            <Slider sx={{ maxWidth: 200 }} min={0} max={100} step={10} value={v.chime_volume}
                    valueLabelDisplay="auto"
                    onChange={(e, x) => setS((c) => ({ ...c, values: { ...c.values, chime_volume: x } }))}
                    onChangeCommitted={(e, x) => set('chime_volume', x)} />
          </Row>
          <Row label="Ready chime">
            <Pick value={v.chime_melody} options={melodies} onChange={(x) => set('chime_melody', x)} />
          </Row>
        </Section>
      )}

      <Section title="Shots">
        <Row label="Target weight">
          <IconButton onClick={() => set('target_g', Math.max(5, v.target_g - 1))}
                      sx={{ border: 1, borderColor: 'divider', width: 36, height: 36 }}>−</IconButton>
          <Typography sx={{ minWidth: 48, textAlign: 'center' }}>{v.target_g.toFixed(0)} g</Typography>
          <IconButton onClick={() => set('target_g', Math.min(120, v.target_g + 1))}
                      sx={{ border: 1, borderColor: 'divider', width: 36, height: 36 }}>+</IconButton>
        </Row>
        <Row label="Review shows for">
          <Pick value={v.review_hold}
                options={Array.from({ length: 24 }, (_, i) => [(i + 1) * 5, `${(i + 1) * 5} s`])}
                onChange={(x) => set('review_hold', x)} />
        </Row>
        <Row label="Auto flush">
          <Pick value={v.auto_flush} options={FLUSH} onChange={(x) => set('auto_flush', x)} />
        </Row>
        {v.auto_flush > 0 && (
          <Row label="Flush delay">
            <Pick value={v.flush_delay} options={DELAY} onChange={(x) => set('flush_delay', x)} />
          </Row>
        )}
      </Section>

      <Section title="Updates">
        <Row label={`Firmware ${s.version}`}>
          <Button disabled={checking} onClick={check}>
            {checking ? 'Checking…' : 'Check now'}
          </Button>
        </Row>
        {up && !checking && up.seq > 0 && (
          <Typography variant="body2" color={up.available ? 'primary' : 'text.secondary'}>
            {up.available ? `${up.version} is available.`
              : up.ok ? 'Tondo is up to date.' : "Couldn't reach the update site."}
          </Typography>
        )}
        {up?.available && (
          <>
            {s.update_notes && (
              <Box component="pre" sx={{ m: 0, p: 1.5, bgcolor: 'background.default', borderRadius: 1,
                                         whiteSpace: 'pre-wrap', fontFamily: 'inherit', fontSize: '0.85rem' }}>
                {s.update_notes.replace(/\*\*/g, '')}
              </Box>
            )}
            <Button variant="contained" onClick={() => setInstallAsk(true)}>Install {up.version}</Button>
          </>
        )}
        {installMsg && <Typography variant="body2">{installMsg}</Typography>}
        <Row label="Check automatically">
          <Pick value={v.update_cadence} options={CADENCE} onChange={(x) => set('update_cadence', x)} />
        </Row>
        {v.update_cadence > 0 && sw('beta_updates', 'Beta updates')}
      </Section>

      <Section title="Settings backup">
        <Typography variant="body2" color="text.secondary">
          A copy of every setting as a file, the same one Tondo writes to its card.
        </Typography>
        <FormControlLabel control={<Switch checked={inclWifi} onChange={(e) => setInclWifi(e.target.checked)} />}
                          label="Include the WiFi password" />
        <FormControlLabel control={<Switch checked={inclToken} onChange={(e) => setInclToken(e.target.checked)} />}
                          label="Include the machine's pairing token" />
        <Stack direction="row" spacing={2} useFlexGap sx={{ flexWrap: 'wrap', rowGap: 1 }}>
          <Button variant="outlined" onClick={download}>Download backup</Button>
          <Button variant="outlined" onClick={() => fileRef.current?.click()}>Restore from file…</Button>
          <input ref={fileRef} type="file" accept=".txt,text/plain" hidden onChange={readFile} />
        </Stack>
        {restoreMsg && <Typography variant="body2">{restoreMsg}</Typography>}
      </Section>

      <Section title="Diagnostics">
        <Stack direction="row" spacing={2}>
          <Link href="/log" target="_blank" rel="noopener">Diagnostic log</Link>
          {summary?.coredump > 0 && <Link href="/coredump">Crash dump</Link>}
        </Stack>
      </Section>

      <Dialog open={restoreText != null} onClose={() => setRestoreText(null)}>
        <DialogTitle>Replace every setting?</DialogTitle>
        <DialogContent>
          <Typography>
            Tondo's settings are replaced with the file's, then it restarts. Anything the file
            doesn't hold goes back to its default.
          </Typography>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setRestoreText(null)}>Cancel</Button>
          <Button variant="contained" onClick={restore}>Restore</Button>
        </DialogActions>
      </Dialog>
      <Dialog open={installAsk} onClose={() => setInstallAsk(false)}>
        <DialogTitle>Install {up?.version}?</DialogTitle>
        <DialogContent>
          <Typography>
            Tondo restarts to install. Its screen goes dark for about 20 seconds; don't
            switch it off.
          </Typography>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setInstallAsk(false)}>Cancel</Button>
          <Button variant="contained" onClick={install}>Install</Button>
        </DialogActions>
      </Dialog>
      <Divider sx={{ opacity: 0 }} />
    </Stack>
  );
}
