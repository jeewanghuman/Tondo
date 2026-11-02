// Tondo's phone page. Served by the device itself (one embedded, gzipped
// file) at http://tondo.local (the name is a setting), styled from the
// DEVICE'S ACTIVE THEME (/api/summary carries the palette the screen uses).
// Four tabs: Live (the machine right now, with remote power/temperature),
// History (shots, notes, compare, export), Trends, and Settings. Reading is
// open to anyone on the WiFi; every change needs the PIN shown on the device.
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  AppBar, BottomNavigation, BottomNavigationAction, Box, Button, CircularProgress,
  Container, CssBaseline, Dialog, DialogActions, DialogContent, DialogTitle,
  Paper, Tab, Tabs, TextField, ThemeProvider, Toolbar, Typography, createTheme,
  useMediaQuery,
} from '@mui/material';
import { PinError, getJson, post, savePin, storedPin } from './api.js';
import { fmtBytes, hex } from './util.js';
import LiveTab from './LiveTab.jsx';
import HistoryTab from './HistoryTab.jsx';
import TrendsTab from './TrendsTab.jsx';
import SettingsTab from './SettingsTab.jsx';

const TABS = ['live', 'history', 'trends', 'settings'];
const TAB_LABEL = { live: 'Live', history: 'History', trends: 'Trends', settings: 'Settings' };
// Tiny line icons (Lucide-like, drawn inline: no icon font to ship).
const ICON = {
  live: 'M22 12h-4l-3 9L9 3l-3 9H2',
  history: 'M3 3v5h5M3.05 13A9 9 0 1 0 6 5.3L3 8M12 7v5l4 2',
  trends: 'M3 3v18h18M7 14l4-4 4 4 5-5',
  settings: 'M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6',
};
const Icon = ({ d }) => (
  <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor"
       strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round"><path d={d} /></svg>
);

function initialTab() {
  const h = window.location.hash.replace('#', '');
  return TABS.includes(h) ? h : 'live';
}

export default function App() {
  const [tab, setTab] = useState(initialTab);
  const [summary, setSummary] = useState(null);
  const [shots, setShots] = useState(null);
  const [error, setError] = useState(null);
  const [pinOk, setPinOk] = useState(!!storedPin());
  const [pinAsk, setPinAsk] = useState(false);
  const [pinText, setPinText] = useState('');
  const [pinMsg, setPinMsg] = useState('');
  const pinWaiters = useRef([]);
  const wide = useMediaQuery('(min-width:700px)');

  const loadShots = useCallback(() => Promise.all([
    getJson('/api/summary'), getJson('/api/shots'),
  ]).then(([s, sh]) => { setSummary(s); setShots(sh.shots); setError(null); })
    .catch(() => setError("Can't reach Tondo. Is this phone on the same WiFi?")), []);

  useEffect(() => { loadShots(); }, [loadShots]);
  useEffect(() => {
    window.location.hash = tab;
    if (tab === 'history' || tab === 'trends') loadShots();  // pick up new shots
  }, [tab]);

  // PIN: ensurePin() resolves the stored PIN, or asks for one (checked
  // against the device) and resolves it -- '' if the user gives up.
  const ensurePin = useCallback(() => {
    const p = storedPin();
    if (p) return Promise.resolve(p);
    return new Promise((resolve) => {
      pinWaiters.current.push(resolve);
      setPinMsg('');
      setPinText('');
      setPinAsk(true);
    });
  }, []);
  const finishPin = (pin) => {
    setPinAsk(false);
    const w = pinWaiters.current;
    pinWaiters.current = [];
    w.forEach((r) => r(pin));
  };
  const submitPin = async () => {
    try {
      await post('/api/pin', {}, pinText.trim());
      savePin(pinText.trim());
      setPinOk(true);
      finishPin(pinText.trim());
    } catch (e) {
      setPinMsg(e.message);
    }
  };
  const lock = () => { savePin(''); setPinOk(false); };

  // A change: with the PIN (asking for it if needed; once more if the stored
  // one has gone stale, e.g. after "New PIN" on the device).
  const postPin = useCallback(async (path, fields = {}) => {
    for (let attempt = 0; attempt < 2; attempt += 1) {
      const pin = await ensurePin();
      if (!pin) throw new Error('Needs the PIN shown on Tondo.');
      try {
        return await post(path, fields, pin);
      } catch (e) {
        if (!(e instanceof PinError)) throw e;
        setPinOk(false);
      }
    }
    throw new Error('Wrong PIN');
  }, [ensurePin]);
  const act = useCallback((action, value) => postPin('/api/control', { action, value })
    .catch((e) => setError(e.message)), [postPin]);

  const devTheme = summary?.theme;
  const muiTheme = useMemo(() => {
    const t = devTheme || {};
    const c = (k, fb) => (t[k] != null ? hex(t[k]) : fb);
    return createTheme({
      palette: {
        mode: 'dark',
        primary: { main: '#e8944a', contrastText: '#000' },
        success: { main: c('ok', '#2ecc71') },
        warning: { main: c('warn', '#f1c40f') },
        error: { main: c('alert', '#e74c3c') },
        background: { default: '#000000', paper: c('card', '#1b2026') },
        text: { primary: c('text', '#ffffff'), secondary: c('muted', '#8a949e') },
      },
      shape: { borderRadius: 14 },
      typography: { fontFamily: 'system-ui, -apple-system, "Segoe UI", Roboto, sans-serif' },
      components: {
        MuiButton: { styleOverrides: { root: { borderRadius: 999, textTransform: 'none' } } },
        MuiChip: { styleOverrides: { root: { borderRadius: 999 } } },
        MuiCard: { styleOverrides: { root: { backgroundImage: 'none' } } },
      },
    });
  }, [devTheme]);

  // Chart palette: the device's amber for the weight trace (the round UI's
  // accent), its status colours for the rest.
  const chart = {
    bg: '#000000',
    rail: devTheme ? hex(devTheme.rail) : '#22282f',
    accent: '#e8944a',
    compare: devTheme ? hex(devTheme.accent) : '#2e9be6',
    ok: devTheme ? hex(devTheme.ok) : '#2ecc71',
    warn: devTheme ? hex(devTheme.warn) : '#f1c40f',
    muted: devTheme ? hex(devTheme.muted) : '#8a949e',
  };

  const body = () => {
    if (tab === 'live') return <LiveTab theme={chart} act={act} />;
    if (tab === 'settings') {
      return <SettingsTab summary={summary} postPin={postPin} ensurePin={ensurePin}
                          pinOk={pinOk} lock={lock} />;
    }
    if (!summary || !shots) {
      return (
        <Box sx={{ textAlign: 'center', py: 8 }}>
          {error ? <Typography color="error">{error}</Typography> : <CircularProgress />}
        </Box>
      );
    }
    if (tab === 'history') {
      return <HistoryTab summary={summary} shots={shots} setShots={setShots} theme={chart}
                         textColor={muiTheme.palette.text.primary} postPin={postPin} />;
    }
    return <TrendsTab shots={shots} theme={chart} />;
  };

  return (
    <ThemeProvider theme={muiTheme}>
      <CssBaseline />
      <AppBar position="sticky" color="transparent" elevation={0}
              sx={{ borderBottom: 1, borderColor: 'divider', bgcolor: 'rgba(0,0,0,0.85)',
                    backdropFilter: 'blur(8px)', pt: 'env(safe-area-inset-top)' }}>
        <Toolbar sx={{ gap: 2 }}>
          <Typography noWrap variant="h6" sx={{ fontWeight: 400 }}>Tondo</Typography>
          {wide && (
            <Tabs value={tab} onChange={(e, v) => setTab(v)} sx={{ flexGrow: 1 }}>
              {TABS.map((t) => <Tab key={t} value={t} label={TAB_LABEL[t]} />)}
            </Tabs>
          )}
          {!wide && <Box sx={{ flexGrow: 1 }} />}
          {summary?.storage?.total > 0 && (tab === 'history' || wide) && (
            <Typography noWrap variant="body2" sx={{ flexShrink: 0, fontSize: '0.75rem' }}
                        color={summary.storage.full ? 'error' : 'text.secondary'}>
              {summary.storage.full ? 'Card full: not saving'
                : `${fmtBytes(summary.storage.free)} free`}
            </Typography>
          )}
        </Toolbar>
      </AppBar>
      <Container maxWidth="md" sx={{ py: 2, pb: wide ? 4 : 'calc(80px + env(safe-area-inset-bottom))' }}>
        {error && summary && (
          <Typography color="error" sx={{ mb: 2 }} onClick={() => setError(null)}>{error}</Typography>
        )}
        {body()}
      </Container>
      {!wide && (
        <Paper elevation={0} sx={{ position: 'fixed', left: 0, right: 0, bottom: 0, borderTop: 1,
                                   borderColor: 'divider', pb: 'env(safe-area-inset-bottom)',
                                   bgcolor: 'rgba(0,0,0,0.92)', backdropFilter: 'blur(8px)' }}>
          <BottomNavigation showLabels value={tab} onChange={(e, v) => setTab(v)}
                            sx={{ bgcolor: 'transparent' }}>
            {TABS.map((t) => (
              <BottomNavigationAction key={t} value={t} label={TAB_LABEL[t]} icon={<Icon d={ICON[t]} />} />
            ))}
          </BottomNavigation>
        </Paper>
      )}
      <Dialog open={pinAsk} onClose={() => finishPin('')}>
        <DialogTitle>Enter Tondo's PIN</DialogTitle>
        <DialogContent>
          <Typography variant="body2" color="text.secondary" sx={{ mb: 2 }}>
            It's on the machine's display: Settings › Tondo › WiFi › Phone PIN.
            This phone remembers it.
          </Typography>
          <TextField autoFocus fullWidth label="PIN" value={pinText}
                     inputProps={{ inputMode: 'numeric', pattern: '[0-9]*', maxLength: 4 }}
                     onChange={(e) => setPinText(e.target.value.replace(/\D/g, ''))}
                     onKeyDown={(e) => { if (e.key === 'Enter' && pinText.length === 4) submitPin(); }}
                     error={!!pinMsg} helperText={pinMsg || ' '} />
        </DialogContent>
        <DialogActions>
          <Button onClick={() => finishPin('')}>Cancel</Button>
          <Button variant="contained" disabled={pinText.length !== 4} onClick={submitPin}>Unlock</Button>
        </DialogActions>
      </Dialog>
    </ThemeProvider>
  );
}
