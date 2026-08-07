# Aura Server

Mission Control lists uploaded watch captures with authenticated in-browser
playback and WAV download controls. The underlying audio route uses the same
HTTP Basic authentication as the rest of Mission Control; recordings are not
publicly accessible.

The Phase 3/4 companion service. It owns Mission Control, normalized calendar
data, audio/transcript memory, and the approval queue, and exposes a compact
authenticated digest for the watch.

## Local development

```sh
cd server
python3 -m venv .venv
. .venv/bin/activate
pip install -e '.[dev]'
cp .env.example .env
# Replace API/admin secrets; optionally add Groq, Gemini, and calendar keys.
uvicorn aura_server.main:app --reload
```

Open `http://127.0.0.1:8000/` for the Mission Control editor. API clients send
`Authorization: Bearer <AURA_API_TOKEN>`. `GET /health` is intentionally public.

The database defaults to `server/data/aura.db`; the directory is created on
startup. The Oracle deployment is kept in `deploy/oracle/` so provisioning the
remote host remains one deliberate, repeatable operation.

Audio can be uploaded as a raw body to `POST /api/v1/audio` with bearer auth and
the `X-Aura-Filename`, `X-Aura-Device`, `X-Aura-Captured-At`, and
`X-Aura-Duration` headers. Without a Groq key, captures remain available for a
manual transcript in Mission Control. Gemini extraction is optional; no
proposed item is written into Today, Calendar, or Saved notes until approved.
