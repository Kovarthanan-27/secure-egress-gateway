import { useEffect, useState } from 'react';
import { api } from '../api';
import { formatBytes, formatTime } from '../format';
import type { Decision } from '../types';
import { usePolling } from '../usePolling';

export default function Logs() {
  const [decision, setDecision] = useState<Decision | ''>('');
  const [search, setSearch] = useState('');
  const [q, setQ] = useState(''); // debounced copy of `search`
  const [paused, setPaused] = useState(false);

  useEffect(() => {
    const t = window.setTimeout(() => setQ(search), 300); // do not query on every keystroke
    return () => window.clearTimeout(t);
  }, [search]);

  const { data, error } = usePolling(() => api.logs(decision, q), paused ? 3600_000 : 2500, [decision, q, paused]);
  const rows = data ?? [];

  return (
    <section>
      <div className="section-head">
        <h2>Traffic log</h2>
        <div className="filters">
          <input
            type="search"
            placeholder="Filter by site"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            aria-label="Filter by site"
          />
          <select value={decision} onChange={(e) => setDecision(e.target.value as Decision | '')} aria-label="Decision">
            <option value="">All requests</option>
            <option value="BLOCK">Blocked only</option>
            <option value="ALLOW">Allowed only</option>
          </select>
          <button className="secondary" onClick={() => setPaused((p) => !p)}>
            {paused ? 'Resume live updates' : 'Pause live updates'}
          </button>
        </div>
      </div>
      {error && <p className="error" role="alert">{error}</p>}

      <div className="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Time</th>
              <th>Decision</th>
              <th>Site</th>
              <th>Why</th>
              <th>Rule</th>
              <th className="r">Traffic</th>
              <th className="r">Decided in</th>
            </tr>
          </thead>
          <tbody>
            {rows.length === 0 && (
              <tr>
                <td colSpan={7} className="muted empty">
                  No requests yet. Point a client at the proxy (localhost:8080) and make a request.
                </td>
              </tr>
            )}
            {rows.map((r) => (
              <tr key={r.id}>
                <td className="mono nowrap">{formatTime(r.ts)}</td>
                <td>
                  <span className={r.decision === 'BLOCK' ? 'pill block' : 'pill allow'}>
                    {r.decision === 'BLOCK' ? 'Blocked' : 'Allowed'}
                  </span>
                </td>
                <td className="mono">
                  {r.method} {r.host}:{r.port}
                </td>
                <td>{r.reason}</td>
                <td className="mono">
                  {r.rulePattern || '–'}
                  {r.category ? <span className="muted"> · {r.category}</span> : null}
                </td>
                <td className="mono r nowrap">
                  {r.decision === 'ALLOW' ? `${formatBytes(r.bytesUp)} up / ${formatBytes(r.bytesDown)} down` : '–'}
                </td>
                <td className="mono r nowrap">{r.decisionUs} µs</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}
