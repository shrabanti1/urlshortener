import type { UrlListItem } from "../types";
import CopyButton from "./CopyButton";

interface Props {
  urls: UrlListItem[];
  onStats: (code: string) => void;
  onDelete: (code: string) => void;
}

function Badges({ url }: { url: UrlListItem }) {
  return (
    <>
      {url.isCustom && <span className="badge">custom</span>}
      {url.expired ? (
        <span className="badge warn">expired</span>
      ) : url.expiresAt ? (
        <span className="badge">expires {url.expiresAt.slice(0, 10)}</span>
      ) : null}
    </>
  );
}

export default function UrlTable({ urls, onStats, onDelete }: Props) {
  if (urls.length === 0) {
    return <div className="empty">No links yet. Shorten one above.</div>;
  }

  return (
    <table>
      <thead>
        <tr>
          <th>Code</th>
          <th>Original URL</th>
          <th />
        </tr>
      </thead>
      <tbody>
        {urls.map((u) => (
          <tr key={u.shortCode}>
            <td>
              <a href={u.shortUrl} target="_blank" rel="noopener noreferrer">
                <code>{u.shortCode}</code>
              </a>
              <Badges url={u} />
            </td>
            <td className="orig" title={u.originalUrl}>{u.originalUrl}</td>
            <td style={{ whiteSpace: "nowrap", textAlign: "right" }}>
              <CopyButton text={u.shortUrl} />
              <button className="link sm" onClick={() => onStats(u.shortCode)}>Stats</button>
              <button className="link sm danger" onClick={() => onDelete(u.shortCode)}>
                Delete
              </button>
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
