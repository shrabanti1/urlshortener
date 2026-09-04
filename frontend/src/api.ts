import type { AuthResponse, CreatedUrl, Stats, UrlList } from "./types";

// Relative paths: the UI is served from the same origin as the API, so this
// works unchanged on localhost, on a DuckDNS domain, or anywhere else.
const ACCESS_KEY = "shortly.access";
const REFRESH_KEY = "shortly.refresh";
const EMAIL_KEY = "shortly.email";

export class ApiError extends Error {
  readonly status: number;
  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

export const tokens = {
  access: () => localStorage.getItem(ACCESS_KEY),
  refresh: () => localStorage.getItem(REFRESH_KEY),
  email: () => localStorage.getItem(EMAIL_KEY),
  set(access: string, refresh: string, email?: string) {
    localStorage.setItem(ACCESS_KEY, access);
    localStorage.setItem(REFRESH_KEY, refresh);
    if (email) localStorage.setItem(EMAIL_KEY, email);
  },
  clear() {
    localStorage.removeItem(ACCESS_KEY);
    localStorage.removeItem(REFRESH_KEY);
    localStorage.removeItem(EMAIL_KEY);
  },
};

/** Called when a session cannot be recovered, so the UI can sign out. */
let onSessionLost: () => void = () => {};
export function setSessionLostHandler(fn: () => void) {
  onSessionLost = fn;
}

async function refreshSession(): Promise<boolean> {
  const refresh = tokens.refresh();
  if (!refresh) return false;
  try {
    const res = await fetch("/api/auth/refresh", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refreshToken: refresh }),
    });
    if (!res.ok) return false;
    const data = (await res.json()) as AuthResponse;
    tokens.set(data.accessToken, data.refreshToken);
    return true;
  } catch {
    return false;
  }
}

// Several components can hit a 401 at the same moment. Without this, each
// would spend the refresh token separately -- and because refresh tokens
// ROTATE, the second call would replay an already-used token, which the server
// treats as theft and revokes every session. Sharing one in-flight promise
// means exactly one refresh happens.
let inFlightRefresh: Promise<boolean> | null = null;
function refreshOnce(): Promise<boolean> {
  if (!inFlightRefresh) {
    inFlightRefresh = refreshSession().finally(() => {
      inFlightRefresh = null;
    });
  }
  return inFlightRefresh;
}

interface RequestOptions {
  method?: string;
  body?: unknown;
  noAuth?: boolean;
}

async function request<T>(
  path: string,
  options: RequestOptions = {},
  isRetry = false,
): Promise<T> {
  const headers: Record<string, string> = { "Content-Type": "application/json" };
  const access = tokens.access();
  if (access && !options.noAuth) headers.Authorization = `Bearer ${access}`;

  const res = await fetch(path, {
    method: options.method ?? "GET",
    headers,
    body: options.body === undefined ? undefined : JSON.stringify(options.body),
  });

  // Access tokens last 15 minutes. Refresh once and replay, rather than
  // dropping the user at the login screen. isRetry stops a dead session from
  // recursing forever.
  if (res.status === 401 && !options.noAuth && !isRetry && tokens.refresh()) {
    const ok = await refreshOnce();
    if (!ok) {
      tokens.clear();
      onSessionLost();
      throw new ApiError("Session expired. Please sign in again.", 401);
    }
    return request<T>(path, options, true);
  }

  if (res.status === 204) return undefined as T;

  const data = await res.json().catch(() => ({}) as Record<string, unknown>);
  if (!res.ok) {
    const message =
      typeof (data as { error?: unknown }).error === "string"
        ? (data as { error: string }).error
        : `Request failed (${res.status})`;
    throw new ApiError(message, res.status);
  }
  return data as T;
}

export const api = {
  register: (email: string, password: string) =>
    request<AuthResponse>("/api/auth/register", {
      method: "POST",
      noAuth: true,
      body: { email, password },
    }),

  login: (email: string, password: string) =>
    request<AuthResponse>("/api/auth/login", {
      method: "POST",
      noAuth: true,
      body: { email, password },
    }),

  // Best-effort: revoking server-side is preferable, but a failure here must
  // not stop the user signing out locally.
  logout: async (allDevices = false) => {
    const refresh = tokens.refresh();
    if (!refresh) return;
    await fetch("/api/auth/logout", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refreshToken: refresh, allDevices }),
    }).catch(() => undefined);
  },

  listUrls: () => request<UrlList>("/api/urls?limit=100"),

  createUrl: (url: string, alias?: string, expiresInDays?: number) =>
    request<CreatedUrl>("/api/urls", {
      method: "POST",
      body: {
        url,
        ...(alias ? { alias } : {}),
        ...(expiresInDays && expiresInDays > 0 ? { expiresInDays } : {}),
      },
    }),

  deleteUrl: (code: string) =>
    request<void>(`/api/urls/${encodeURIComponent(code)}`, { method: "DELETE" }),

  stats: (code: string, days = 30) =>
    request<Stats>(`/api/urls/${encodeURIComponent(code)}/stats?days=${days}`),
};
