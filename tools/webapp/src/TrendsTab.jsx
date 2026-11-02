// Trends: how the shots have gone over the last weeks -- shots per day, the
// average yield and time per week, how close they land on target, and a
// per-beans summary. Everything is worked out here from the shot list.
import React, { useMemo, useState } from 'react';
import {
  Card, CardContent, Chip, Stack, Table, TableBody, TableCell, TableHead,
  TableRow, Typography,
} from '@mui/material';
import { BarChart, LineChart } from './charts.jsx';

const RANGES = [4, 12, 26, 52];
const DAY = 86400;

const startOfDay = (unix) => {
  const d = new Date(unix * 1000);
  d.setHours(0, 0, 0, 0);
  return d.getTime() / 1000;
};
// Weeks start on Monday.
const startOfWeek = (unix) => {
  const d = new Date(startOfDay(unix) * 1000);
  d.setDate(d.getDate() - ((d.getDay() + 6) % 7));
  return d.getTime() / 1000;
};
const label = (unix) => new Date(unix * 1000)
  .toLocaleDateString([], { month: 'numeric', day: 'numeric' });
const avg = (xs) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : null);
const accuracy = (xs) => {
  const t = xs.filter((s) => s.target_g > 0);
  if (!t.length) return null;
  return 100 * (1 - avg(t.map((s) => Math.abs(s.final_g - s.target_g) / s.target_g)));
};

function Section({ title, sub, children }) {
  return (
    <Card>
      <CardContent sx={{ '&:last-child': { pb: 2 } }}>
        <Typography variant="subtitle1">{title}</Typography>
        {sub && <Typography variant="body2" color="text.secondary" sx={{ mb: 1 }}>{sub}</Typography>}
        {children}
      </CardContent>
    </Card>
  );
}

export default function TrendsTab({ shots, theme }) {
  const [weeks, setWeeks] = useState(12);

  const data = useMemo(() => {
    const now = Date.now() / 1000;
    const firstWeek = startOfWeek(now) - (weeks - 1) * 7 * DAY;
    const inRange = shots.filter((s) => s.unix >= firstWeek);

    // Shots per day for short ranges, per week otherwise.
    const daily = weeks <= 4;
    const buckets = [];
    if (daily) {
      const first = startOfDay(now) - (weeks * 7 - 1) * DAY;
      for (let t = first; t <= now; t += DAY) buckets.push(startOfDay(t + DAY / 2));
    } else {
      for (let w = 0; w < weeks; w += 1) buckets.push(firstWeek + w * 7 * DAY);
    }
    const key = daily ? startOfDay : startOfWeek;
    const counts = buckets.map((b) => ({
      label: label(b), v: inRange.filter((s) => key(s.unix) === b).length,
    }));

    const weekly = [];
    for (let w = 0; w < weeks; w += 1) {
      const b = firstWeek + w * 7 * DAY;
      const xs = inRange.filter((s) => startOfWeek(s.unix) === b);
      weekly.push({
        label: label(b),
        yield: avg(xs.map((s) => s.final_g)),
        time: avg(xs.map((s) => s.duration_ms / 1000)),
        acc: accuracy(xs),
        ratio: avg(xs.filter((s) => s.target_g > 0).map((s) => s.final_g - s.target_g)),
      });
    }

    const byBean = new Map();
    for (const s of inRange) {
      const k = s.bean || '';
      if (!byBean.has(k)) byBean.set(k, []);
      byBean.get(k).push(s);
    }
    const beans = [...byBean.entries()]
      .map(([bean, xs]) => ({
        bean,
        n: xs.length,
        rating: avg(xs.filter((s) => s.rating > 0).map((s) => s.rating)),
        yield: avg(xs.map((s) => s.final_g)),
        time: avg(xs.map((s) => s.duration_ms / 1000)),
        acc: accuracy(xs),
      }))
      .sort((a, b) => b.n - a.n);

    return {
      n: inRange.length, daily, counts, weekly, beans,
      perDay: inRange.length / (weeks * 7),
      acc: accuracy(inRange),
    };
  }, [shots, weeks]);

  const amber = theme.accent;
  return (
    <Stack spacing={2}>
      <Stack direction="row" sx={{ flexWrap: 'wrap', gap: 1 }}>
        {RANGES.map((w) => (
          <Chip key={w} label={`${w} weeks`} color={weeks === w ? 'primary' : 'default'}
                onClick={() => setWeeks(w)} />
        ))}
      </Stack>
      {data.n === 0 ? (
        <Typography color="text.secondary" sx={{ py: 4, textAlign: 'center' }}>
          No shots in the last {weeks} weeks.
        </Typography>
      ) : (
        <>
          <Section title={data.daily ? 'Shots per day' : 'Shots per week'}
                   sub={`${data.n} shots, ${data.perDay.toFixed(1)} a day on average`}>
            <BarChart points={data.counts} theme={theme} />
          </Section>
          <Section title="Yield and time" sub="Weekly averages">
            <LineChart theme={theme} series={[
              { name: 'Yield', unit: ' g', color: amber,
                points: data.weekly.map((w) => ({ label: w.label, v: w.yield })) },
              { name: 'Time', unit: ' s', color: theme.ok,
                points: data.weekly.map((w) => ({ label: w.label, v: w.time })) },
            ]} />
          </Section>
          <Section title="On target"
                   sub={data.acc != null
                     ? `${data.acc.toFixed(1)}% accuracy over the range (shots with a target)`
                     : 'No shots with a target in this range'}>
            {data.acc != null && (
              <LineChart theme={theme} series={[
                { name: 'Accuracy', unit: '%', color: theme.ok, minSpan: 1,
                  points: data.weekly.map((w) => ({ label: w.label, v: w.acc })) },
                { name: 'Over/under', unit: ' g', color: theme.warn, minSpan: 0.5,
                  points: data.weekly.map((w) => ({ label: w.label, v: w.ratio })) },
              ]} />
            )}
          </Section>
          <Section title="By beans" sub="Beans come from the notes on each shot (History)">
            <Table size="small" sx={{ '& td, & th': { px: 0.75, whiteSpace: 'nowrap' },
                                      '& td:first-of-type': { whiteSpace: 'normal' } }}>
              <TableHead>
                <TableRow>
                  <TableCell>Beans</TableCell>
                  <TableCell align="right">Shots</TableCell>
                  <TableCell align="right">Rating</TableCell>
                  <TableCell align="right">Yield</TableCell>
                  <TableCell align="right" sx={{ display: { xs: 'none', sm: 'table-cell' } }}>Time</TableCell>
                  <TableCell align="right" sx={{ display: { xs: 'none', sm: 'table-cell' } }}>On target</TableCell>
                </TableRow>
              </TableHead>
              <TableBody>
                {data.beans.map((b) => (
                  <TableRow key={b.bean}>
                    <TableCell sx={{ color: b.bean ? 'text.primary' : 'text.secondary' }}>
                      {b.bean || 'No beans noted'}
                    </TableCell>
                    <TableCell align="right">{b.n}</TableCell>
                    <TableCell align="right">{b.rating != null ? b.rating.toFixed(1) + ' ★' : '—'}</TableCell>
                    <TableCell align="right">{b.yield.toFixed(1)} g</TableCell>
                    <TableCell align="right" sx={{ display: { xs: 'none', sm: 'table-cell' } }}>
                      {b.time.toFixed(0)} s
                    </TableCell>
                    <TableCell align="right" sx={{ display: { xs: 'none', sm: 'table-cell' } }}>
                      {b.acc != null ? b.acc.toFixed(1) + '%' : '—'}
                    </TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </Section>
        </>
      )}
    </Stack>
  );
}
