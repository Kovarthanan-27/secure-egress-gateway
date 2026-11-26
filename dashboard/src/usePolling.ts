import { useEffect, useRef, useState } from 'react';
import type { DependencyList } from 'react';

/** Calls `fn` immediately and then every `intervalMs`. Re-runs from scratch when `deps` change. */
export function usePolling<T>(fn: () => Promise<T>, intervalMs: number, deps: DependencyList = []) {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState<string | null>(null);
  const latest = useRef(fn);
  latest.current = fn; // always call the newest closure without restarting the timer

  useEffect(() => {
    let cancelled = false;
    const tick = async () => {
      try {
        const result = await latest.current();
        if (!cancelled) {
          setData(result);
          setError(null);
        }
      } catch (e) {
        if (!cancelled) setError(e instanceof Error ? e.message : 'Request failed');
      }
    };
    void tick();
    const id = window.setInterval(tick, intervalMs);
    return () => {
      cancelled = true; // ignore responses that arrive after unmount / dependency change
      window.clearInterval(id);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [intervalMs, ...deps]);

  return { data, error, setData };
}
