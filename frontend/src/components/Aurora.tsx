import { useEffect, useRef } from "react";

/**
 * The drifting background field, and its reaction to the pointer.
 *
 * Two independent motions are layered:
 *
 *   1. Ambient drift -- CSS keyframes on the inner element, always running.
 *   2. Parallax -- this component writes the smoothed pointer position into
 *      two custom properties, and CSS translates each blob by a different
 *      multiple of them. Opposite signs on the middle blob make the layers
 *      separate in depth instead of sliding as one sheet.
 *
 * Why a rAF loop rather than moving things directly in the pointermove
 * handler: pointer events fire far more often than the screen refreshes (a
 * 1000Hz mouse will happily hand you 1000 events a second), so writing style
 * on every one is work the browser throws away. Storing the target and
 * easing toward it once per frame both bounds the work and produces the
 * trailing, weighty feel -- the field lags the cursor slightly, as though it
 * has mass.
 *
 * Nothing here touches layout or paint. The only properties that change are
 * transforms, which the compositor handles on the GPU.
 */
export default function Aurora() {
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const el = ref.current;
    if (!el) return;

    // Ambient motion that follows the cursor is a common migraine and
    // vestibular trigger. If the OS says to reduce motion, this never starts.
    if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) return;

    // A finger is not a cursor: on touch there is no hover position to
    // follow, and the handler would only fire mid-tap. Skip it entirely --
    // the ambient drift still plays.
    if (!window.matchMedia("(hover: hover) and (pointer: fine)").matches) return;

    // Targets (where the pointer is) and current values (where the field has
    // eased to so far).
    let targetX = 0;
    let targetY = 0;
    let currentX = 0;
    let currentY = 0;
    let glowTargetX = window.innerWidth / 2;
    let glowTargetY = window.innerHeight / 2;
    let glowX = glowTargetX;
    let glowY = glowTargetY;
    let frame = 0;

    function onPointerMove(event: PointerEvent) {
      // Normalised to -0.5..0.5 from the centre of the viewport, so the same
      // multiplier gives the same visual travel on any screen size.
      targetX = event.clientX / window.innerWidth - 0.5;
      targetY = event.clientY / window.innerHeight - 0.5;
      glowTargetX = event.clientX;
      glowTargetY = event.clientY;
    }

    function tick() {
      // Exponential easing: move a fixed fraction of the remaining distance
      // each frame. Fast when far, slow when close, and it never overshoots.
      // The blobs use a smaller fraction than the glow, so heavier things
      // visibly lag lighter ones.
      currentX += (targetX - currentX) * 0.045;
      currentY += (targetY - currentY) * 0.045;
      glowX += (glowTargetX - glowX) * 0.14;
      glowY += (glowTargetY - glowY) * 0.14;

      el!.style.setProperty("--mx", currentX.toFixed(4));
      el!.style.setProperty("--my", currentY.toFixed(4));
      el!.style.setProperty("--gx", `${glowX.toFixed(1)}px`);
      el!.style.setProperty("--gy", `${glowY.toFixed(1)}px`);

      frame = requestAnimationFrame(tick);
    }

    // passive: the handler never calls preventDefault, and saying so lets the
    // browser skip waiting on it before it scrolls.
    window.addEventListener("pointermove", onPointerMove, { passive: true });
    frame = requestAnimationFrame(tick);

    return () => {
      window.removeEventListener("pointermove", onPointerMove);
      cancelAnimationFrame(frame);
    };
  }, []);

  // aria-hidden: pure decoration, and a screen reader should never announce it.
  return (
    <div className="aurora" ref={ref} aria-hidden="true">
      <span className="blob blob-1"><i /></span>
      <span className="blob blob-2"><i /></span>
      <span className="blob blob-3"><i /></span>
      <div className="glow" />
      <div className="grain" />
    </div>
  );
}
