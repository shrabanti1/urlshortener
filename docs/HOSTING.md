# Hosting this on a VPS with a DuckDNS subdomain

End-to-end, about 45 minutes. Cost: the server only (~$5/month).

The image is built by GitHub Actions and pulled by the server, so the VPS
never compiles Drogon. That matters: a source build needs 20-40 minutes and
more than 2 GB of RAM, and is routinely OOM-killed on small instances.

```
GitHub  --push-->  Actions builds image  -->  GHCR
                                               |
                                        server pulls
                                               v
Internet --> nginx (TLS) --> app --> postgres + redis
```

---

## Step 1 — Put the code on GitHub

The server pulls the compose files and nginx config from your repo, and Actions
needs the code to build the image.

```bash
cd ~/Desktop/URLShortener
gh repo create urlshortener --public --source=. --remote=origin --push
```

No `gh` CLI? Create an empty repo on github.com, then:

```bash
git remote add origin https://github.com/YOURNAME/urlshortener.git
git push -u origin main
```

**Check `.env` was not pushed** — it holds your secrets:

```bash
git ls-files | grep -c '^\.env$'     # must print 0
```

Pushing to `main` triggers the build. Watch it under the repo's **Actions** tab;
the first run takes 15-25 minutes because Drogon is compiled from scratch.
Later runs hit the cache and take ~2 minutes.

It publishes **two** images: `urlshortener-app` (the C++ binary) and
`urlshortener-nginx` (nginx with the built React bundle baked in). The server
pulls both, so it never runs a compiler or npm.

When the run finishes, make **both** packages pullable without a login:
**GitHub → your profile → Packages →** for each of `urlshortener-app` and
`urlshortener-nginx` **→ Package settings → Change visibility → Public.**

---

## Step 2 — Create the server

Any provider works. Pick an **x86_64** instance, since the workflow builds
`linux/amd64`:

| Provider | Plan | Price |
|---|---|---|
| Hetzner | CX22 (2 vCPU, 4 GB) | ~EUR 3.79/mo |
| DigitalOcean | Basic (1 vCPU, 2 GB) | $12/mo |
| Vultr | Regular (1 vCPU, 2 GB) | $10/mo |

Hetzner is the cheapest by a wide margin. Choose **Ubuntu 24.04**, and add your
SSH key during creation.

> Picking an ARM instance (Hetzner CAX) means adding `linux/arm64` to
> `platforms:` in `.github/workflows/publish.yml`.

Note the server's public IPv4 address.

---

## Step 3 — DuckDNS

1. Go to <https://www.duckdns.org> and sign in (GitHub/Google).
2. Type a subdomain, e.g. `yourname-short`, and click **add domain**.
3. Put your server's IP in the **current ip** box and press **update ip**.

You now own `yourname-short.duckdns.org`, free, and Let's Encrypt will issue a
real certificate for it.

Verify from your laptop before going further:

```bash
dig +short yourname-short.duckdns.org      # must print your server IP
```

DNS must be correct before requesting a certificate: Let's Encrypt validates by
fetching a file over HTTP, and a wrong record burns one of your five allowed
failures per hour.

---

## Step 4 — Prepare the server

```bash
ssh root@YOUR_SERVER_IP
```

```bash
# A non-root user to run the app
adduser --disabled-password --gecos "" deploy
usermod -aG sudo deploy
rsync --archive --chown=deploy:deploy ~/.ssh /home/deploy/

apt update && apt upgrade -y
apt install -y ca-certificates curl git ufw fail2ban

# Docker
install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] \
https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo $VERSION_CODENAME) stable" \
  > /etc/apt/sources.list.d/docker.list
apt update && apt install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin
usermod -aG docker deploy

# Firewall: SSH, HTTP, HTTPS. Nothing else -- postgres and redis stay internal.
ufw default deny incoming
ufw default allow outgoing
ufw allow OpenSSH
ufw allow 80/tcp
ufw allow 443/tcp
ufw --force enable
```

Harden SSH (`/etc/ssh/sshd_config`), then `systemctl restart ssh`:

```
PermitRootLogin no
PasswordAuthentication no
```

---

## Step 5 — Deploy

```bash
su - deploy
git clone https://github.com/YOURNAME/urlshortener.git
cd urlshortener
```

Write the environment file. **Generate fresh secrets** -- never reuse the
development ones:

```bash
cat > .env <<CONF
DOMAIN=yourname-short.duckdns.org
LETSENCRYPT_EMAIL=you@example.com

IMAGE=ghcr.io/YOURNAME/urlshortener-app
NGINX_IMAGE=ghcr.io/YOURNAME/urlshortener-nginx
IMAGE_TAG=latest

DB_NAME=urlshortener
DB_USER=urlshortener
DB_PASSWORD=$(openssl rand -base64 32 | tr -d '/+=')
DB_POOL_SIZE=8

JWT_SECRET=$(openssl rand -hex 32)
JWT_EXPIRY_MINUTES=15
REFRESH_TOKEN_DAYS=30
IP_HASH_SECRET=$(openssl rand -hex 32)

REDIS_TTL_SECONDS=3600
LOG_LEVEL=INFO
CONF
chmod 600 .env
```

Everything below uses both compose files, so define a shortcut:

```bash
alias dc='docker compose -f docker-compose.yml -f docker-compose.prod.yml'
echo "alias dc='docker compose -f docker-compose.yml -f docker-compose.prod.yml'" >> ~/.bashrc
```

Pull the image and start the data services:

```bash
dc pull
dc up -d postgres redis app
dc ps                      # wait until all three are healthy
```

Get the certificate (creates a placeholder, starts nginx, swaps in the real one):

```bash
export $(grep -E '^(DOMAIN|LETSENCRYPT_EMAIL)=' .env | xargs)
./scripts/init-letsencrypt.sh
```

Start everything:

```bash
dc up -d
dc ps
```

---

## Step 6 — Verify

```bash
curl -I  http://yourname-short.duckdns.org/          # 301 -> https
curl -sS https://yourname-short.duckdns.org/health   # {"status":"ok",...}
```

Then open **https://yourname-short.duckdns.org** in a browser. You should get
the web UI, with a padlock. Create an account, shorten a URL, click it.

---

## Step 7 — Backups

```bash
sudo mkdir -p /var/backups/urlshortener
sudo chown deploy:deploy /var/backups/urlshortener
crontab -e
```

```
0 3 * * * cd /home/deploy/urlshortener && BACKUP_DIR=/var/backups/urlshortener ./scripts/backup.sh >> /var/log/urlshortener-backup.log 2>&1
```

Test it once by hand, and practise a restore before you need one:

```bash
BACKUP_DIR=/var/backups/urlshortener ./scripts/backup.sh
```

A backup on the same disk as the database does not protect against losing the
disk. Copy them off with `rclone`, `scp`, or your provider's snapshots.

---

## Updating

```bash
# on your laptop
git push                      # Actions rebuilds and pushes the image

# on the server, once the workflow is green
cd ~/urlshortener && git pull
dc pull app && dc up -d app
curl -sS https://yourname-short.duckdns.org/health
```

A few seconds of downtime while the container restarts.

Schema changes **are** automatic: the app applies `db/schema.sql` and
`db/migrations/*.sql` at start-up, in filename order, on every boot. Every
statement is idempotent, so re-running is safe.

Adding a migration therefore means dropping a numbered `.sql` file into
`db/migrations/` and redeploying -- there is no separate step to forget. Still
take a backup before a schema change:

```bash
BACKUP_DIR=/var/backups/urlshortener ./scripts/backup.sh
```

---

## Troubleshooting

**`denied` when pulling an image** — one of the two GHCR packages is still
private. Both must be public (Step 1), or log in on the server:
```bash
echo YOUR_GITHUB_PAT | docker login ghcr.io -u YOURNAME --password-stdin
```

**Certificate request fails** — DNS is not pointing at the server yet
(`dig +short yourname-short.duckdns.org`), or port 80 is blocked (`ufw status`).
Add `--staging` to the certbot line in `init-letsencrypt.sh` while debugging:
the real endpoint allows only **5 failures per hostname per hour**.

**nginx will not start** — it resolves upstream names at start-up, so `app` and
`swagger` must already be running. `dc up -d app swagger` then `dc up -d nginx`.

**Everything looks healthy but the site is unreachable** — check `ufw status`
and that the provider's own firewall allows 80/443.

**Out of disk** — `docker system prune -a` removes unused images. Log rotation
is already capped in `docker-compose.prod.yml`.

---

## Costs

| Item | Cost |
|---|---|
| Hetzner CX22 | ~EUR 3.79/mo |
| DuckDNS subdomain | free |
| Let's Encrypt certificate | free |
| GitHub Actions + GHCR (public repo) | free |

Swap DuckDNS for a real domain later by changing `DOMAIN` in `.env`, pointing
an A record at the server, and re-running `init-letsencrypt.sh`.
