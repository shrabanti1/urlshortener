import type { DailyPoint } from "../types";

// Hand-drawn SVG rather than a charting library: a bar chart does not justify
// 60 KB of dependency, and this keeps the bundle small.
export default function ClicksChart({ points }: { points: DailyPoint[] }) {
  if (points.length === 0) return null;

  const max = points.reduce((m, p) => Math.max(m, p.clicks), 0);
  const W = 100;
  const H = 100;
  const gap = points.length > 40 ? 0.4 : 1.2;
  const barWidth = (W - gap * (points.length - 1)) / points.length;

  return (
    <div className="chart">
      <h3>Clicks per day · peak {max}</h3>
      <svg viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none" role="img"
           aria-label={`Daily clicks, peak ${max}`}>
        {points.map((p, i) => {
          // Zero-click days still get a 1-unit stub so the axis reads as a
          // continuous timeline instead of showing gaps.
          const h = max === 0 ? 1 : Math.max(1, (p.clicks / max) * (H - 4));
          return (
            <rect
              key={p.date}
              className={p.clicks === 0 ? "bar zero" : "bar"}
              x={i * (barWidth + gap)}
              y={H - h}
              width={barWidth}
              height={h}
              rx={0.4}
            >
              <title>{`${p.date}: ${p.clicks} ${p.clicks === 1 ? "click" : "clicks"}`}</title>
            </rect>
          );
        })}
      </svg>
      <div style={{ display: "flex", justifyContent: "space-between",
                    fontSize: 11, color: "var(--muted)", marginTop: 6 }}>
        <span>{points[0].date}</span>
        <span>{points[points.length - 1].date}</span>
      </div>
    </div>
  );
}
