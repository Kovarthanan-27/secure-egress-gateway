import { useState } from 'react';
import { Bar, BarChart, CartesianGrid, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { api } from '../api';
import { formatMinute, formatNumber } from '../format';
import { usePolling } from '../usePolling';

const WINDOWS = [
  { minutes: 15, label: '15 min' },
  { minutes: 60, label: '1 hour' },
  { minutes: 180, label: '3 hours' },
];

export default function Overview() {
  const [minutes, setMinutes] = useState(60);
  const summary = usePolling(() => api.summary(minutes), 3000, [minutes]);
  const series = usePolling(() => api.timeseries(minutes), 3000, [minutes]);
  const top = usePolling(() => api.topBlocked(minutes), 3000, [minutes]);

  const s = summary.data;
  const blockRate = s && s.total > 0 ? ((s.blocked / s.total) * 100).toFixed(1) : '0.0';
  const maxTop = Math.max(1, ...(top.data ?? []).map((h) => h.count));
  const error = summary.error ?? series.error ?? top.error;

  return (
    <section>
      <div className="section-head">
        <h2>Traffic</h2>
        <div className="segmented" role="group" aria-label="Time window">
          {WINDOWS.map((w) => (
            <button
              key={w.minutes}
              className={w.minutes === minutes ? 'seg active' : 'seg'}
              onClick={() => setMinutes(w.minutes)}
            >
              {w.label}
            </button>
          ))}
        </div>
      </div>
      {error && <p className="error" role="alert">{error}</p>}

      <div className="band">
        <div>
          <span className="num">{s ? formatNumber(s.total) : '–'}</span>
          <span className="lbl">requests</span>
        </div>
        <div>
          <span className="num allow">{s ? formatNumber(s.allowed) : '–'}</span>
          <span className="lbl">allowed</span>
        </div>
        <div>
          <span className="num block">{s ? formatNumber(s.blocked) : '–'}</span>
          <span className="lbl">blocked ({blockRate}%)</span>
        </div>
        <div>
          <span className="num">{s ? `${Math.round(s.avgDecisionUs)} µs` : '–'}</span>
          <span className="lbl">avg policy decision</span>
        </div>
        <div>
          <span className="num">{s ? `v${s.policyVersion}` : '–'}</span>
          <span className="lbl">{s ? `${s.activeRules} active rules, default ${s.defaultAction.toLowerCase()}` : 'policy'}</span>
        </div>
      </div>

      <div className="panel">
        <h3>Requests per minute</h3>
        <div className="chart">
          <ResponsiveContainer width="100%" height="100%">
            <BarChart data={series.data ?? []} margin={{ top: 8, right: 8, bottom: 0, left: -12 }}>
              <CartesianGrid stroke="#e3e8ee" vertical={false} />
              <XAxis dataKey="minute" tickFormatter={formatMinute} minTickGap={32} tick={{ fontSize: 12, fill: '#5d6b79' }} />
              <YAxis allowDecimals={false} tick={{ fontSize: 12, fill: '#5d6b79' }} />
              <Tooltip labelFormatter={(v) => formatMinute(String(v))} cursor={{ fill: 'rgba(36,87,214,0.06)' }} />
              <Bar dataKey="allowed" name="Allowed" stackId="a" fill="#1c7c7d" isAnimationActive={false} />
              <Bar dataKey="blocked" name="Blocked" stackId="a" fill="#c0332a" isAnimationActive={false} />
            </BarChart>
          </ResponsiveContainer>
        </div>
      </div>

      <div className="panel">
        <h3>Most blocked sites</h3>
        {(top.data ?? []).length === 0 ? (
          <p className="muted">Nothing has been blocked in this window. Send a request to a blocked site through the proxy to see it here.</p>
        ) : (
          <ul className="bars">
            {(top.data ?? []).map((h) => (
              <li key={h.host}>
                <span className="mono">{h.host}</span>
                <span className="bar-track">
                  <span className="bar-fill" style={{ width: `${(h.count / maxTop) * 100}%` }} />
                </span>
                <span className="mono">{formatNumber(h.count)}</span>
              </li>
            ))}
          </ul>
        )}
      </div>
    </section>
  );
}
