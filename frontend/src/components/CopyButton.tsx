import { useEffect, useRef, useState } from "react";

interface Props {
  text: string;
  className?: string;
  label?: string;
}

export default function CopyButton({ text, className = "link sm", label = "Copy" }: Props) {
  const [copied, setCopied] = useState(false);
  const timer = useRef<number>();

  // Without this, the timeout can fire after the row is removed (deleting a
  // link re-renders the table) and set state on an unmounted component.
  useEffect(() => () => window.clearTimeout(timer.current), []);

  async function copy() {
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      // clipboard needs a secure context; localhost counts, but fall back anyway
      const ta = document.createElement("textarea");
      ta.value = text;
      document.body.appendChild(ta);
      ta.select();
      document.execCommand("copy");
      document.body.removeChild(ta);
    }
    setCopied(true);
    timer.current = window.setTimeout(() => setCopied(false), 1200);
  }

  return (
    <button className={className} onClick={copy} type="button">
      {copied ? "Copied" : label}
    </button>
  );
}
