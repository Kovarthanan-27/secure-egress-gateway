import { useEffect, useState } from 'react';
import { api, clearCredentials, hasCredentials } from './api';
import Login from './components/Login';
import Overview from './components/Overview';
import Logs from './components/Logs';
import Policies from './components/Policies';

type Tab = 'overview' | 'traffic' | 'policy';

const TABS: { id: Tab; label: string }[] = [
  { id: 'overview', label: 'Overview' },
  { id: 'traffic', label: 'Traffic log' },
  { id: 'policy', label: 'Policy' },
];

export default function App() {
  const [signedIn, setSignedIn] = useState(false);
  const [checking, setChecking] = useState(hasCredentials());
  const [tab, setTab] = useState<Tab>('overview');

  // If a saved login exists, verify it once; also react to any 401 from any API call.
  useEffect(() => {
    if (hasCredentials()) {
      api.check().then(() => setSignedIn(true)).catch(() => setSignedIn(false)).finally(() => setChecking(false));
    }
    const onUnauthorized = () => setSignedIn(false);
    window.addEventListener('sgw-unauthorized', onUnauthorized);
    return () => window.removeEventListener('sgw-unauthorized', onUnauthorized);
  }, []);

  if (checking) return null;
  if (!signedIn) return <Login onSuccess={() => setSignedIn(true)} />;

  return (
    <div className="shell">
      <header className="topbar">
        <h1 className="brand">Egress Gateway</h1>
        <nav aria-label="Sections">
          {TABS.map((t) => (
            <button
              key={t.id}
              className={t.id === tab ? 'tab active' : 'tab'}
              aria-current={t.id === tab ? 'page' : undefined}
              onClick={() => setTab(t.id)}
            >
              {t.label}
            </button>
          ))}
        </nav>
        <button
          className="link"
          onClick={() => {
            clearCredentials();
            setSignedIn(false);
          }}
        >
          Sign out
        </button>
      </header>
      <main className="content">
        {tab === 'overview' && <Overview />}
        {tab === 'traffic' && <Logs />}
        {tab === 'policy' && <Policies />}
      </main>
    </div>
  );
}
