import type { Bucket, Decision, HostCount, LogRow, Rule, RuleInput, Settings, Summary } from './types';

// Credentials live in sessionStorage: they survive a refresh but vanish when the tab closes.
// (A production system would use short-lived session cookies or OIDC instead of Basic auth.)
const KEY = 'sgw-auth';
let token: string | null = sessionStorage.getItem(KEY);

export function setCredentials(user: string, password: string): void {
  token = btoa(`${user}:${password}`);
  sessionStorage.setItem(KEY, token);
}

export function clearCredentials(): void {
  token = null;
  sessionStorage.removeItem(KEY);
}

export function hasCredentials(): boolean {
  return token !== null;
}

export class ApiError extends Error {
  status: number;
  constructor(status: number, message: string) {
    super(message);
    this.status = status;
  }
}

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const res = await fetch(`/api${path}`, {
    ...init,
    headers: {
      'Content-Type': 'application/json',
      ...(token ? { Authorization: `Basic ${token}` } : {}),
    },
  });

  if (res.status === 401) {
    clearCredentials();
    window.dispatchEvent(new Event('sgw-unauthorized'));
    throw new ApiError(401, 'Not signed in');
  }
  if (!res.ok) {
    let message = res.statusText;
    try {
      const body = await res.json();
      if (body && typeof body.error === 'string') message = body.error;
    } catch {
      /* body was not JSON; keep the status text */
    }
    throw new ApiError(res.status, message);
  }
  if (res.status === 204) return undefined as T;
  return (await res.json()) as T;
}

export const api = {
  check: () => request<{ ok: boolean }>('/auth/check'),
  summary: (minutes: number) => request<Summary>(`/stats/summary?minutes=${minutes}`),
  timeseries: (minutes: number) => request<Bucket[]>(`/stats/timeseries?minutes=${minutes}`),
  topBlocked: (minutes: number) => request<HostCount[]>(`/stats/top-blocked?minutes=${minutes}&limit=8`),
  logs: (decision: Decision | '', q: string) => {
    const params = new URLSearchParams({ limit: '150' });
    if (decision) params.set('decision', decision);
    if (q.trim()) params.set('q', q.trim());
    return request<LogRow[]>(`/logs?${params.toString()}`);
  },
  rules: () => request<Rule[]>('/rules'),
  createRule: (r: RuleInput) => request<Rule>('/rules', { method: 'POST', body: JSON.stringify(r) }),
  updateRule: (id: number, r: RuleInput) => request<Rule>(`/rules/${id}`, { method: 'PUT', body: JSON.stringify(r) }),
  deleteRule: (id: number) => request<void>(`/rules/${id}`, { method: 'DELETE' }),
  settings: () => request<Settings>('/settings'),
  setDefaultAction: (action: Decision) =>
    request<Settings>('/settings/default-action', { method: 'PUT', body: JSON.stringify({ action }) }),
};
