import { useState, type FormEvent } from "react";
import { api } from "../api";
import type { CreatedUrl } from "../types";
import CopyButton from "./CopyButton";

interface Props {
  onCreated: () => void;
  onError: (message: string) => void;
}

export default function CreateCard({ onCreated, onError }: Props) {
  const [url, setUrl] = useState("");
  const [alias, setAlias] = useState("");
  const [expiry, setExpiry] = useState("0");
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState<CreatedUrl | null>(null);

  async function submit(e: FormEvent) {
    e.preventDefault();
    setBusy(true);
    try {
      const created = await api.createUrl(
        url.trim(),
        alias.trim() || undefined,
        Number(expiry) || undefined,
      );
      setResult(created);
      setUrl("");
      setAlias("");
      onCreated();
    } catch (err) {
      onError(err instanceof Error ? err.message : "Could not shorten that URL");
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className="card">
      <h2>Shorten a URL</h2>
      <form onSubmit={submit}>
        <div className="row">
          <input type="url" required placeholder="https://example.com/a/very/long/url"
                 value={url} onChange={(e) => setUrl(e.target.value)} />
          <button type="submit" style={{ flex: "0 0 auto" }} disabled={busy}>
            {busy ? "Shortening…" : "Shorten"}
          </button>
        </div>
        <div className="row" style={{ marginTop: 10 }}>
          <div>
            <label htmlFor="alias">Custom alias <span className="opt">optional</span></label>
            <input id="alias" type="text" maxLength={16} placeholder="my-link"
                   value={alias} onChange={(e) => setAlias(e.target.value)} />
          </div>
          <div>
            <label htmlFor="expiry">Expires <span className="opt">optional</span></label>
            <select id="expiry" value={expiry} onChange={(e) => setExpiry(e.target.value)}>
              <option value="0">Never</option>
              <option value="1">In 1 day</option>
              <option value="7">In 7 days</option>
              <option value="30">In 30 days</option>
              <option value="365">In 1 year</option>
            </select>
          </div>
        </div>
      </form>

      {result && (
        <div className="result show">
          <a href={result.shortUrl} target="_blank" rel="noopener noreferrer">
            {result.shortUrl}
          </a>
          <CopyButton text={result.shortUrl} className="ghost sm" />
        </div>
      )}
    </section>
  );
}
