import { useState } from 'react';
import type { FormEvent } from 'react';
import { api } from '../api';
import type { Decision, Rule } from '../types';
import { usePolling } from '../usePolling';

const EMPTY = { pattern: '', action: 'BLOCK' as Decision, category: '', reason: '' };

export default function Policies() {
  const rules = usePolling(() => api.rules(), 5000);
  const settings = usePolling(() => api.settings(), 5000);
  const [form, setForm] = useState(EMPTY);
  const [message, setMessage] = useState<{ kind: 'error' | 'ok'; text: string } | null>(null);
  const [busy, setBusy] = useState(false);

  async function refresh() {
    const [r, s] = await Promise.all([api.rules(), api.settings()]);
    rules.setData(r);
    settings.setData(s);
  }

  async function run(action: () => Promise<unknown>, okText: string) {
    setBusy(true);
    setMessage(null);
    try {
      await action();
      await refresh();
      setMessage({ kind: 'ok', text: okText });
    } catch (e) {
      setMessage({ kind: 'error', text: e instanceof Error ? e.message : 'Something went wrong' });
    } finally {
      setBusy(false);
    }
  }

  function addRule(e: FormEvent) {
    e.preventDefault();
    void run(async () => {
      await api.createRule({ ...form, enabled: true });
      setForm(EMPTY);
    }, 'Rule added. Proxies pick it up within a few seconds.');
  }

  function toggle(rule: Rule) {
    void run(
      () => api.updateRule(rule.id, { ...rule, enabled: !rule.enabled }),
      rule.enabled ? `Disabled ${rule.pattern}` : `Enabled ${rule.pattern}`,
    );
  }

  function remove(rule: Rule) {
    if (!window.confirm(`Delete the rule for ${rule.pattern}?`)) return;
    void run(() => api.deleteRule(rule.id), `Deleted ${rule.pattern}`);
  }

  const s = settings.data;

  return (
    <section>
      <div className="section-head">
        <h2>Policy</h2>
        <span className="muted">{s ? `Version v${s.policyVersion}` : ''}</span>
      </div>

      <div className="panel">
        <h3>Sites that match no rule</h3>
        <div className="segmented" role="group" aria-label="Default action">
          <button
            className={s?.defaultAction === 'ALLOW' ? 'seg active' : 'seg'}
            disabled={busy}
            onClick={() => void run(() => api.setDefaultAction('ALLOW'), 'Default is now allow')}
          >
            Allow
          </button>
          <button
            className={s?.defaultAction === 'BLOCK' ? 'seg active' : 'seg'}
            disabled={busy}
            onClick={() => void run(() => api.setDefaultAction('BLOCK'), 'Default is now block')}
          >
            Block
          </button>
        </div>
        <p className="muted small">
          The most specific rule always wins: a rule for developers.facebook.com overrides a rule for facebook.com.
        </p>
      </div>

      <form className="panel add" onSubmit={addRule}>
        <h3>Add a rule</h3>
        <div className="add-grid">
          <label>
            Domain
            <input
              value={form.pattern}
              onChange={(e) => setForm({ ...form, pattern: e.target.value })}
              placeholder="example.com or *.example.com"
              required
            />
          </label>
          <label>
            Action
            <select value={form.action} onChange={(e) => setForm({ ...form, action: e.target.value as Decision })}>
              <option value="BLOCK">Block</option>
              <option value="ALLOW">Allow</option>
            </select>
          </label>
          <label>
            Category
            <input value={form.category} onChange={(e) => setForm({ ...form, category: e.target.value })} placeholder="Social Media" />
          </label>
          <label className="wide">
            Reason shown to the user
            <input value={form.reason} onChange={(e) => setForm({ ...form, reason: e.target.value })} placeholder="Why this site is restricted" />
          </label>
        </div>
        <button className="primary" disabled={busy}>Add rule</button>
        {message && (
          <p className={message.kind === 'error' ? 'error' : 'ok'} role={message.kind === 'error' ? 'alert' : 'status'}>
            {message.text}
          </p>
        )}
      </form>

      {rules.error && <p className="error" role="alert">{rules.error}</p>}
      <div className="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Domain</th>
              <th>Action</th>
              <th>Category</th>
              <th>Reason</th>
              <th>Active</th>
              <th />
            </tr>
          </thead>
          <tbody>
            {(rules.data ?? []).length === 0 && (
              <tr>
                <td colSpan={6} className="muted empty">No rules yet. Add one above.</td>
              </tr>
            )}
            {(rules.data ?? []).map((r) => (
              <tr key={r.id} className={r.enabled ? '' : 'off'}>
                <td className="mono">{r.pattern}</td>
                <td>
                  <span className={r.action === 'BLOCK' ? 'pill block' : 'pill allow'}>
                    {r.action === 'BLOCK' ? 'Block' : 'Allow'}
                  </span>
                </td>
                <td>{r.category}</td>
                <td>{r.reason}</td>
                <td>
                  <input type="checkbox" checked={r.enabled} disabled={busy} onChange={() => toggle(r)} aria-label={`Rule for ${r.pattern} is active`} />
                </td>
                <td className="r">
                  <button className="link danger" disabled={busy} onClick={() => remove(r)}>Delete</button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}
