# Deployment

## Choosing a host

| Option | Cost | Why / why not |
|---|---|---|
| **VPS (Hetzner / DigitalOcean / Vultr)** | **$4–6/mo** | **Recommended.** `docker compose up -d` runs unchanged. You learn the whole stack: DNS, TLS, firewall, backups. |
| Fly.io / Render | $0–7/mo | Less to manage, but Postgres and Redis become paid add-ons and the free tiers sleep. Hides exactly what you want to learn. |
| AWS / GCP | varies | Powerful, but ECS + RDS + ElastiCache + ALB is a lot of incidental complexity for one binary. Easy to run up a surprise bill. |
| Kubernetes | — | Wrong tool at this size. |

**Recommendation: a $5 VPS.** 2 GB RAM comfortably runs all four containers. The
whole point of Phase 7 was that the stack is portable — a VPS lets you use it
as-is. Hetzner is the cheapest; DigitalOcean has the gentlest documentation.

Sizing: the app idles around 30–60 MB, Postgres ~150 MB, Redis is capped at
256 MB, nginx is negligible. 2 GB gives plenty of headroom.

---

## 1. Server preparation

```bash
ssh root@YOUR_SERVER_IP

# Never run the app as root.
adduser --disabled-password --gecos "" deploy
usermod -aG sudo deploy
rsync --archive --chown=deploy:deploy ~/.ssh /home/deploy/

apt update && apt upgrade -y
apt install -y ca-certificates curl git ufw fail2ban
install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] \
https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo $VERSION_CODENAME) stable" \
  > /etc/apt/sources.list.d/docker.list
apt update && apt install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin
usermod -aG docker deploy
```

### Firewall

Only three ports should be reachable. Everything else — including Postgres and
Redis — stays closed.

```bash
ufw default deny incoming
ufw default allow outgoing
ufw allow OpenSSH
ufw allow 80/tcp
ufw allow 443/tcp
ufw enable
ufw status verbose
```

### Harden SSH

```bash
# /etc/ssh/sshd_config
PermitRootLogin no
PasswordAuthentication no      # keys only
```
```bash
systemctl restart ssh
```

`fail2ban` is installed above and bans repeated SSH failures by default.

---

## 2. DNS

At your registrar, create an **A record**:

```
Type  Name    Value              TTL
A     short   YOUR_SERVER_IP     300
```

Use a low TTL (300s) until everything works, then raise it. Verify before
requesting a certificate — Let's Encrypt validates over HTTP and will fail if
DNS has not propagated:

```bash
dig +short short.example.com     # must print YOUR_SERVER_IP
```

---

## 3. Secrets

```bash
su - deploy
git clone <your-repo> urlshortener && cd urlshortener
cp .env.example .env
chmod 600 .env                   # readable only by deploy
```

Generate real values — never reuse development secrets:

```bash
echo "DB_PASSWORD=$(openssl rand -base64 32 | tr -d '/+=')" >> .env
echo "JWT_SECRET=$(openssl rand -hex 32)"                   >> .env
echo "IP_HASH_SECRET=$(openssl rand -hex 32)"               >> .env
echo "DOMAIN=short.example.com"                             >> .env
echo "LETSENCRYPT_EMAIL=you@example.com"                    >> .env
```

Then delete the placeholder lines the example file shipped with.

The app refuses to start on a weak or placeholder `JWT_SECRET`, so a
misconfiguration fails loudly instead of silently accepting forged tokens.

**Rotating `IP_HASH_SECRET` changes every future IP hash**, so unique-visitor
counts will not match historical rows. Rotate only deliberately.

---

## 4. First deploy

```bash
export $(grep -E '^(DOMAIN|LETSENCRYPT_EMAIL)=' .env | xargs)

docker compose -f docker-compose.yml -f docker-compose.prod.yml build
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d postgres redis app

./scripts/init-letsencrypt.sh          # obtains the first certificate

docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d
```

The first build compiles Drogon from source: 15–30 minutes on a small VPS, and
it needs ~2 GB of RAM. If the build is OOM-killed, add swap first:

```bash
fallocate -l 2G /swapfile && chmod 600 /swapfile && mkswap /swapfile && swapon /swapfile
echo '/swapfile none swap sw 0 0' >> /etc/fstab
```

### Verify

```bash
curl -I  http://short.example.com/           # 301 -> https
curl -sS https://short.example.com/health    # {"status":"ok",...}
docker compose ps                            # all healthy
```

---

## 5. Certificate renewal

The `certbot` container wakes every 12 hours and renews anything within 30 days
of expiry; nginx reloads on the same cadence. Certificates last 90 days, so
there is wide margin.

Test the renewal path before you rely on it:

```bash
docker compose -f docker-compose.yml -f docker-compose.prod.yml \
  run --rm certbot renew --dry-run
```

> While testing, add `--staging` to `init-letsencrypt.sh`. The production ACME
> endpoint allows only **5 failures per hostname per hour**, and it is easy to
> lock yourself out for an hour.

---

## 6. Backups

A volume is not a backup. It survives a removed container; it does not survive
a bad migration, a dropped table, or the server disappearing.

```bash
crontab -e
```
```
0 3 * * * cd /home/deploy/urlshortener && BACKUP_DIR=/var/backups/urlshortener ./scripts/backup.sh >> /var/log/urlshortener-backup.log 2>&1
```

`backup.sh` verifies the gzip integrity and refuses to keep an empty dump — a
truncated backup that looks fine is worse than no backup.

**Copy backups off the server.** A backup on the same disk as the database does
not protect against losing the disk:

```bash
0 4 * * * rclone copy /var/backups/urlshortener remote:urlshortener-backups
```

Restore (destructive, asks for confirmation):

```bash
./scripts/restore.sh /var/backups/urlshortener/urlshortener-20260830-030000.sql.gz
```

**Practise a restore on a throwaway server before you need it.** An untested
backup is a guess.

---

## 7. Logs and monitoring

Log rotation is configured in the production overlay (`max-size: 10m`,
`max-file: 3–5`), so container logs cannot fill the disk.

```bash
docker compose logs -f app                 # follow
docker compose logs --since 1h app | grep -i error
docker compose exec nginx tail -f /var/log/nginx/access.log
```

The nginx log format records `rt=` (total request time) and `urt=` (upstream
time), which separates "the app was slow" from "the network was slow".

Minimum viable monitoring, in order of value:

1. **Uptime check** on `/health` — UptimeRobot or Better Stack, free tier, emails
   you when the site is down. Do this one at minimum.
2. **Disk space** — the most common cause of a dead small server:
   `df -h` in a weekly cron with an alert.
3. **`docker compose ps`** — `restart: unless-stopped` recovers crashes, but a
   container stuck in a restart loop needs a human.

---

## 8. Security checklist

- [x] Firewall allows only 22, 80, 443
- [x] SSH: keys only, root login disabled, fail2ban active
- [x] Containers run as a non-root user (`appuser`)
- [x] Postgres and Redis publish **no** ports; reachable only on the internal network
- [x] HTTPS with HSTS; HTTP redirects to HTTPS
- [x] TLS 1.2+ only
- [x] Rate limiting: 1 r/s on auth, 10 r/s API, 50 r/s redirects
- [x] `X-Forwarded-For` overwritten by nginx, so it cannot be forged
- [x] Secrets in `.env` (mode 600, gitignored), never in the image
- [x] `server_tokens off`, `nosniff`, `X-Frame-Options: DENY`
- [x] Passwords hashed with Argon2id; raw IPs never stored
- [x] App refuses to start with a weak `JWT_SECRET`
- [ ] **Change `pg_hba.conf` from `trust` if you ever expose Postgres** — see below

### The `trust` issue

Development Postgres (Homebrew) uses `trust` auth, which accepts any connection
without checking the password. That is why a deliberately wrong password still
connected in Phase 3.

The official `postgres` image defaults to `scram-sha-256`, so the Docker
deployment is **not** affected. It matters only if you install Postgres directly
on a host. Verify:

```bash
docker compose exec postgres cat /var/lib/postgresql/data/pg_hba.conf | grep -v '^#' | grep .
```

---

## 9. Updating

```bash
cd ~/urlshortener
git pull
docker compose -f docker-compose.yml -f docker-compose.prod.yml build app
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d app
curl -sS https://short.example.com/health
```

There is a few seconds of downtime while the container restarts. Zero-downtime
deploys need two app containers and an nginx reload — worth doing only once
that downtime actually matters.

**Schema changes are not automatic.** The `docker-entrypoint-initdb.d` scripts
run only when the data directory is empty. Apply migrations explicitly:

```bash
docker compose exec -T postgres psql -U urlshortener -d urlshortener < db/migrations/00X_whatever.sql
```

Take a backup first.

---

## Rollback

```bash
git log --oneline -5
git checkout <previous-commit>
docker compose -f docker-compose.yml -f docker-compose.prod.yml build app
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d app
```

If a migration is involved, restore the database backup taken before it.
