import type { Stats } from "../types";
import ClicksChart from "./ClicksChart";

interface Props {
  stats: Stats;
  onClose: () => void;
}

export default function StatsPanel({ stats, onClose }: Props) {
  return (
    <section className="card">
      <h2>
        Stats for <code>{stats.shortCode}</code>
        {stats.expired ? (
          <span className="badge warn">expired</span>
        ) : stats.expiresAt ? (
          <span className="badge">expires {stats.expiresAt.slice(0, 10)}</span>
        ) : null}
      </h2>

      <div className="stats">
        <div className="stat">
          <div className="n">{stats.totalClicks}</div>
          <div className="l">Total clicks</div>
        </div>
        <div className="stat">
          <div className="n">{stats.todayClicks}</div>
          <div className="l">Today</div>
        </div>
        <div className="stat">
          <div className="n">{stats.uniqueVisitors}</div>
          <div className="l">Unique visitors</div>
        </div>
      </div>

      <ClicksChart points={stats.dailyClicks} />

      {stats.topReferrers.length > 0 ? (
        <ul className="refs">
          {stats.topReferrers.map((r) => (
            <li key={r.source}>
              <span className="src" title={r.source}>{r.source}</span>
              <strong>{r.clicks}</strong>
            </li>
          ))}
        </ul>
      ) : (
        <div className="empty">No clicks recorded yet.</div>
      )}

      <div className="hint">
        Clicks are written in batches, so a fresh click can take a second to appear.
      </div>
      <div style={{ marginTop: 14 }}>
        <button className="ghost sm" onClick={onClose}>Close</button>
      </div>
    </section>
  );
}
