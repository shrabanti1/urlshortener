# URL Shortener

A production-style URL shortener backend built in C++.

**Stack:** C++17 · Drogon · PostgreSQL · Redis · Base62 · JWT · GoogleTest · Docker · Nginx

## Status
- [x] Phase 1 — Drogon + PostgreSQL
- [ ] Phase 2 — Base62
- [ ] Phase 3 — Redis
- [ ] Phase 4 — Authentication
- [ ] Phase 5 — Analytics
- [ ] Phase 6 — Testing
- [ ] Phase 7 — Docker
- [ ] Phase 8 — Deployment

## Build
```bash
cmake -S . -B build
cmake --build build -j
./build/url_shortener
```
