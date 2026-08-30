# URL Shortener

A production-style URL shortener backend built in C++.

**Stack:** C++17 · Drogon · PostgreSQL · Redis · Base62 · JWT · GoogleTest · Docker · Nginx

## Status
- [x] **Phase 1 — Drogon + PostgreSQL** (working)
- [ ] Phase 2 — Base62
- [ ] Phase 3 — Redis
- [ ] Phase 4 — Authentication
- [ ] Phase 5 — Analytics
- [ ] Phase 6 — Testing
- [ ] Phase 7 — Docker
- [ ] Phase 8 — Deployment

## API

| Method | Path             | Purpose                        | Success |
|--------|------------------|--------------------------------|---------|
| GET    | `/`              | Service name                   | 200     |
| GET    | `/health`        | Liveness + database round trip | 200/503 |
| POST   | `/api/urls`      | Create a short URL             | 201     |
| GET    | `/{shortCode}`   | Redirect to the original URL   | 302/404 |

```bash
curl -X POST http://localhost:8080/api/urls \
     -H 'Content-Type: application/json' \
     -d '{"url":"https://example.com/a/very/long/url"}'
# {"shortCode":"1","shortUrl":"http://localhost:8080/1"}
```

## Setup (macOS, Apple Silicon)

Drogon must be built from source: the Homebrew bottle is compiled **without**
PostgreSQL support (it declares no libpq dependency, so the backend is silently
excluded). Verify any Drogon build with `otool -L .../libdrogon.dylib | grep pq`.

```bash
brew install cmake postgresql@17 jsoncpp c-ares brotli openssl@3 hiredis
brew services start postgresql@17
export PATH="/opt/homebrew/opt/postgresql@17/bin:$PATH"
```

Database (apply the schema **as the app role** so it owns the table and sequence):

```bash
psql -d postgres -c "CREATE ROLE urlshortener WITH LOGIN PASSWORD 'devpassword';"
psql -d postgres -c "CREATE DATABASE urlshortener OWNER urlshortener;"
psql -d urlshortener -U urlshortener -f db/schema.sql
```

Configuration:

```bash
cp .env.example .env   # then edit; .env is gitignored
```

## Build and run

```bash
cmake -S . -B build
cmake --build build -j
./build/url_shortener
```

## Layout

```
src/
├── main.cpp                     entrypoint, DB pool, GET / and /health
├── controllers/                 HTTP layer: parse, validate, respond
│   ├── UrlController.*          POST /api/urls
│   └── RedirectController.*     GET /{shortCode}
├── repositories/                all SQL lives here
│   └── UrlRepository.*
└── utils/                       Config · ShortCode · UrlValidator · Http
db/schema.sql
```
