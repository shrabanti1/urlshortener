import { useState, type FormEvent } from "react";
import { api } from "../api";
import type { AuthResponse } from "../types";

interface Props {
  onSignedIn: (auth: AuthResponse) => void;
  onError: (message: string) => void;
}

export default function AuthCard({ onSignedIn, onError }: Props) {
  const [mode, setMode] = useState<"login" | "register">("login");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [busy, setBusy] = useState(false);

  async function submit(e: FormEvent) {
    e.preventDefault();
    setBusy(true);
    try {
      const auth = mode === "login"
        ? await api.login(email.trim(), password)
        : await api.register(email.trim(), password);
      setPassword("");
      onSignedIn(auth);
    } catch (err) {
      onError(err instanceof Error ? err.message : "Something went wrong");
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className="card">
      <div className="tabs" role="tablist">
        <button role="tab" aria-selected={mode === "login"}
                onClick={() => setMode("login")}>Log in</button>
        <button role="tab" aria-selected={mode === "register"}
                onClick={() => setMode("register")}>Create account</button>
      </div>

      <form onSubmit={submit}>
        <div className="field">
          <label htmlFor="email">Email</label>
          <input id="email" type="email" required autoComplete="username"
                 placeholder="you@example.com"
                 value={email} onChange={(e) => setEmail(e.target.value)} />
        </div>
        <div className="field">
          <label htmlFor="password">Password</label>
          <input id="password" type="password" required minLength={8}
                 autoComplete={mode === "login" ? "current-password" : "new-password"}
                 placeholder="at least 8 characters"
                 value={password} onChange={(e) => setPassword(e.target.value)} />
        </div>
        <button type="submit" disabled={busy}>
          {busy ? "Working…" : mode === "login" ? "Log in" : "Create account"}
        </button>
        <div className="hint">
          Passwords are hashed with Argon2id. Nothing is stored in plaintext.
        </div>
      </form>
    </section>
  );
}
