export interface User {
  id: number;
  email: string;
}

export interface AuthResponse {
  accessToken: string;
  refreshToken: string;
  tokenType: string;
  expiresIn: number;
  refreshExpiresIn: number;
  user: User;
}

export interface CreatedUrl {
  shortCode: string;
  shortUrl: string;
  expiresInDays?: number;
}

export interface UrlListItem {
  shortCode: string;
  shortUrl: string;
  originalUrl: string;
  createdAt: string;
  isCustom: boolean;
  /** Empty string means the link never expires. */
  expiresAt: string;
  expired: boolean;
}

export interface UrlList {
  urls: UrlListItem[];
  count: number;
  limit: number;
  offset: number;
}

export interface DailyPoint {
  date: string;
  clicks: number;
}

export interface Referrer {
  source: string;
  clicks: number;
}

export interface Stats {
  shortCode: string;
  originalUrl: string;
  totalClicks: number;
  todayClicks: number;
  uniqueVisitors: number;
  lastClickedAt: string;
  expiresAt: string;
  expired: boolean;
  topReferrers: Referrer[];
  dailyClicks: DailyPoint[];
}
