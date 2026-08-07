# One-time Oracle deployment

This is deliberately one deployment operation after local Phase 3 validation.

Prerequisites:

1. An Ubuntu 24.04 Ampere A1 instance with a reserved public IP.
2. A DNS `A` record for the chosen Aura hostname pointing at that IP.
3. Oracle ingress rules for TCP 22, 80, and 443 and UDP 443. Use Tailscale or a
   restricted source for SSH when possible; keep only HTTP(S) public.
4. Docker Engine with the Compose plugin installed from Docker's Ubuntu repo.

Deployment:

```sh
git clone <your-aura-repository-url> aura-os
cd aura-os/server
cp .env.example .env
openssl rand -hex 32   # use for AURA_API_TOKEN
openssl rand -hex 32   # use separately for AURA_ADMIN_PASSWORD
# Set DOMAIN, AURA_PUBLIC_URL=https://DOMAIN, API/admin secrets, and optional
# Groq/Gemini/calendar credentials, then:
docker compose up -d --build
curl https://YOUR_DOMAIN/health
docker compose ps
```

Compose forces all Aura state into `/data`; persistent SQLite, captures, token
cache, and Caddy certificates live in named Docker volumes. The API health check
and `restart: unless-stopped` policy restore service after a reboot. Do not use
`docker compose down -v` during an upgrade.

Before each deployment, make a consistent SQLite backup:

```sh
./deploy/oracle/backup.sh
docker compose pull
docker compose up -d --build
docker compose ps
curl https://YOUR_DOMAIN/health
```

For Microsoft Graph, configure the client ID and run the device flow once from
inside the API container:

```sh
docker compose exec api python -m aura_server.calendar_cli
```

For iCloud, use an Apple app-specific password, not the Apple ID password. Keep
`.env`, backups, API tokens, and SSH private keys outside source control. Rotate
the API bearer token if it has appeared in a terminal transcript or chat, then
update the watch configuration to match.
