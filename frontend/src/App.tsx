import { useCallback, useEffect, useState } from "react";
import { api, setSessionLostHandler, tokens } from "./api";
import type { AuthResponse, Stats, UrlListItem } from "./types";
import AuthCard from "./components/AuthCard";
import CreateCard from "./components/CreateCard";
import StatsPanel from "./components/StatsPanel";
import UrlTable from "./components/UrlTable";

type Message = { text: string; kind: "ok" | "err" } | null;

export default function App() {
  // "unknown" until the stored token is verified, so the UI does not flash the
  // sign-in form for an already-authenticated user.
  const [session, setSession] = useState<"unknown" | "in" | "out">("unknown");
  const [email, setEmail] = useState(tokens.email() ?? "");
  const [urls, setUrls] = useState<UrlListItem[]>([]);
  const [stats, setStats] = useState<Stats | null>(null);
  const [message, setMessage] = useState<Message>(null);

  const say = useCallback((text: string, kind: "ok" | "err" = "err") => {
    setMessage({ text, kind });
  }, []);

  useEffect(() => {
    if (!message) return;
    const t = window.setTimeout(() => setMessage(null), 5000);
    return () => window.clearTimeout(t);
  }, [message]);

  const loadUrls = useCallback(async () => {
    try {
      const list = await api.listUrls();
      setUrls(list.urls);
    } catch (err) {
      say(err instanceof Error ? err.message : "Could not load your links");
    }
  }, [say]);

  const signOut = useCallback(() => {
    tokens.clear();
    setSession("out");
    setUrls([]);
    setStats(null);
    setEmail("");
  }, []);

  // The api module calls this when a refresh fails, so an expired session
  // clears the UI instead of leaving it in a half-signed-in state.
  useEffect(() => setSessionLostHandler(signOut), [signOut]);

  // Verify a stored token before trusting it: it may have expired while the
  // tab was closed, or been revoked from another device.
  useEffect(() => {
    if (!tokens.access()) {
      setSession("out");
      return;
    }
    api
      .listUrls()
      .then((list) => {
        setUrls(list.urls);
        setSession("in");
      })
      .catch(() => signOut());
  }, [signOut]);

  function onSignedIn(auth: AuthResponse) {
    tokens.set(auth.accessToken, auth.refreshToken, auth.user.email);
    setEmail(auth.user.email);
    setSession("in");
    void loadUrls();
    say("Signed in.", "ok");
  }

  async function handleLogout() {
    await api.logout();
    signOut();
    say("Signed out.", "ok");
  }

  async function showStats(code: string) {
    try {
      setStats(await api.stats(code));
    } catch (err) {
      say(err instanceof Error ? err.message : "Could not load stats");
    }
  }

  async function handleDelete(code: string) {
    if (!window.confirm(`Delete /${code}? Its click history goes too.`)) return;
    try {
      await api.deleteUrl(code);
      if (stats?.shortCode === code) setStats(null);
      say(`Deleted /${code}`, "ok");
      await loadUrls();
    } catch (err) {
      say(err instanceof Error ? err.message : "Could not delete that link");
    }
  }

  return (
    <div className="wrap">
      <header>
        <div className="brand">
          <h1>Shortly</h1>
          <span className="tag">C++ · Drogon · PostgreSQL · Redis</span>
        </div>
        <div className="who">
          {session === "in" && (
            <>
              <span>{email}</span>
              <button className="ghost sm" onClick={handleLogout}>Log out</button>
            </>
          )}
        </div>
      </header>

      {message && (
        <div className={`msg show ${message.kind}`}>{message.text}</div>
      )}

      {session === "out" && <AuthCard onSignedIn={onSignedIn} onError={say} />}

      {session === "in" && (
        <>
          <CreateCard onCreated={loadUrls} onError={say} />

          <section className="card">
            <h2>Your links</h2>
            <UrlTable urls={urls} onStats={showStats} onDelete={handleDelete} />
          </section>

          {stats && <StatsPanel stats={stats} onClose={() => setStats(null)} />}
        </>
      )}

      <footer>
        <a href="/docs/" target="_blank" rel="noopener noreferrer">API documentation</a>
        {" · short links work in any browser, no login needed"}
      </footer>
    </div>
  );
}
