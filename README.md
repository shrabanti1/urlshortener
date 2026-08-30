# URL Shortener

A production-style URL shortener backend built in C++.

**Stack:** C++17 · Drogon · PostgreSQL · Redis · Base62 · JWT · GoogleTest · Docker · Nginx

## Status
- [x] **Phase 1 — Drogon + PostgreSQL** (working)
- [x] **Phase 2 — Base62** (working)
- [x] **Phase 3 — Redis** (working)
- [x] **Phase 4 — Authentication** (working)
- [x] **Phase 5 — Analytics** (working)
- [x] **Phase 6 — Testing** (73 tests)
- [x] **Phase 7 — Docker** (working)
- [ ] Phase 8 — Deployment

## API

| Method | Path             | Purpose                        | Success |
|--------|------------------|--------------------------------|---------|
| GET    | `/`                     | Service name                   | 200     |
| GET    | `/health`               | Liveness + database round trip | 200/503 |
| POST   | `/api/auth/register`    | Create an account, returns JWT | 201/409 |
| POST   | `/api/auth/login`       | Exchange credentials for a JWT | 200/401 |
| POST   | `/api/urls` 🔒          | Create a short URL             | 201     |
| GET    | `/api/urls` 🔒          | List your URLs (paginated)     | 200     |
| DELETE | `/api/urls/{code}` 🔒   | Delete your URL                | 204/404 |
| GET    | `/api/urls/{code}/stats` 🔒 | Click analytics for your URL | 200/404 |
| GET    | `/{shortCode}`          | Redirect to the original URL   | 302/404 |

🔒 = requires `Authorization: Bearer <token>`

```bash
curl -X POST http://localhost:8080/api/urls \
     -H 'Content-Type: application/json' \
     -d '{"url":"https://example.com/a/very/long/url"}'
# {"shortCode":"1000","shortUrl":"http://localhost:8080/1000"}
```

## Setup (macOS, Apple Silicon)

Drogon must be built from source: the Homebrew bottle is compiled **without**
PostgreSQL support (it declares no libpq dependency, so the backend is silently
excluded). Verify any Drogon build with `otool -L .../libdrogon.dylib | grep pq`.

```bash
brew install cmake postgresql@17 jsoncpp c-ares brotli openssl@3 hiredis redis libsodium
brew services start postgresql@17
brew services start redis
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
cp .env.example .env
# JWT_SECRET must be >= 32 chars of real randomness; the app refuses to start otherwise
openssl rand -hex 32
```

Migrations:

```bash
psql -d urlshortener -U urlshortener -f db/migrations/002_users_and_ownership.sql
psql -d urlshortener -U urlshortener -f db/migrations/003_click_events.sql
```

## Run with Docker

```bash
cp .env.example .env          # set DB_PASSWORD, JWT_SECRET, IP_HASH_SECRET
docker compose up -d
curl http://localhost:8081/health
```

Four services: nginx (published) -> app -> postgres + redis (internal only).
Postgres data lives in a named volume and survives `docker compose down`.
The first build compiles Drogon from source and takes 10-20 minutes; later
builds reuse that layer.

## Tests

```bash
cmake --build build -j
cd build && ctest --output-on-failure     # 73 tests
ctest -L unit                             # 48 unit tests, no DB needed (~2s)
ctest -L integration                      # 25 tests, needs Postgres + Redis
```

Integration tests run against a separate `urlshortener_test` database:

```bash
psql -d postgres -c "CREATE DATABASE urlshortener_test OWNER urlshortener;"
for f in db/schema.sql db/migrations/*.sql; do
  psql -d urlshortener_test -U urlshortener -f "$f"
done
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
│   ├── UrlRepository.*          (cache-aside: Redis, then Postgres)
│   ├── UserRepository.*
│   └── AnalyticsRepository.*
├── cache/
│   └── UrlCache.*               Redis + circuit breaker
├── models/                      UrlRecord.h · User.h
├── filters/
│   └── JwtAuthFilter.*          rejects unauthenticated requests
tests/
├── unit/                        pure functions, no I/O
└── integration/                 real Postgres + Redis + HTTP
└── utils/                       Config · ShortCode · UrlValidator · Http
                                 Password (Argon2id) · Jwt (HS256)
                                 IpHash (HMAC-SHA256)
third_party/jwt-cpp/             vendored, header-only
db/schema.sql
```
