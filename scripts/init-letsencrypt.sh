#!/usr/bin/env bash
# Obtains the first TLS certificate. Run ONCE, after DNS points at this server.
#
# Chicken-and-egg problem: nginx will not start without a certificate file,
# but certbot needs a running nginx to answer the ACME challenge. The fix is
# to create a throwaway self-signed certificate, start nginx, get the real
# certificate, then reload.
set -euo pipefail

: "${DOMAIN:?set DOMAIN, e.g. export DOMAIN=short.example.com}"
: "${LETSENCRYPT_EMAIL:?set LETSENCRYPT_EMAIL for expiry notices}"

COMPOSE="docker compose -f docker-compose.yml -f docker-compose.prod.yml"
CERT_PATH="/etc/letsencrypt/live/${DOMAIN}"

echo "==> 1/5 creating a temporary self-signed certificate for ${DOMAIN}"
$COMPOSE run --rm --entrypoint "\
  sh -c 'mkdir -p ${CERT_PATH} && \
  openssl req -x509 -nodes -newkey rsa:2048 -days 1 \
    -keyout ${CERT_PATH}/privkey.pem \
    -out ${CERT_PATH}/fullchain.pem \
    -subj \"/CN=${DOMAIN}\"'" certbot

echo "==> 2/5 starting nginx with the placeholder certificate"
$COMPOSE up -d nginx

echo "==> 3/5 deleting the placeholder"
$COMPOSE run --rm --entrypoint "rm -rf /etc/letsencrypt/live/${DOMAIN} \
  /etc/letsencrypt/archive/${DOMAIN} /etc/letsencrypt/renewal/${DOMAIN}.conf" certbot

echo "==> 4/5 requesting the real certificate from Let's Encrypt"
# Add --staging while testing: the production endpoint allows only 5 failures
# per hostname per hour, and it is easy to get locked out.
$COMPOSE run --rm --entrypoint "\
  certbot certonly --webroot -w /var/www/certbot \
    --email ${LETSENCRYPT_EMAIL} \
    --agree-tos --no-eff-email \
    -d ${DOMAIN} \
    --rsa-key-size 2048 \
    --non-interactive" certbot

echo "==> 5/5 reloading nginx with the real certificate"
$COMPOSE exec nginx nginx -s reload

echo "Done. https://${DOMAIN} should now serve a valid certificate."
