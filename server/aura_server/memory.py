from __future__ import annotations

import json
import re
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import httpx

from .config import Settings
from .db import Database


EXTRACTION_PROMPT = """You extract useful memory from a private transcript.
Return JSON only, as an array. Each object must contain kind (task, event,
reminder, or note), title, details, priority (0-3), due_at, starts_at, and
ends_at. Dates must be ISO-8601 with an offset, or null. Do not invent facts.
When nothing actionable or worth remembering exists, return []. Current UTC
time is {now}.

Transcript:
{transcript}
"""


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _set_capture(db: Database, capture_id: int, status: str,
                 error: str | None = None) -> None:
    with db.connect() as connection:
        connection.execute(
            "UPDATE audio_captures SET status=?, error=? WHERE id=?",
            (status, error, capture_id),
        )


def transcribe(path: Path, settings: Settings) -> tuple[str, str | None]:
    if not settings.groq_api_key:
        raise RuntimeError("GROQ_API_KEY is not configured")
    with path.open("rb") as audio:
        response = httpx.post(
            "https://api.groq.com/openai/v1/audio/transcriptions",
            headers={"Authorization": f"Bearer {settings.groq_api_key}"},
            data={"model": settings.groq_whisper_model,
                  "response_format": "verbose_json"},
            files={"file": (path.name, audio, "audio/wav")},
            timeout=120,
        )
    response.raise_for_status()
    payload = response.json()
    return str(payload.get("text", "")).strip(), payload.get("language")


def _json_array(text: str) -> list[dict[str, Any]]:
    text = re.sub(r"^```(?:json)?\s*|\s*```$", "", text.strip(), flags=re.I)
    value = json.loads(text)
    if not isinstance(value, list):
        raise ValueError("extraction response was not an array")
    return [item for item in value if isinstance(item, dict)]


def extract(transcript: str, settings: Settings) -> list[dict[str, Any]]:
    if not settings.gemini_api_key:
        return []
    url = ("https://generativelanguage.googleapis.com/v1beta/models/"
           f"{settings.gemini_model}:generateContent")
    response = httpx.post(
        url,
        params={"key": settings.gemini_api_key},
        json={
            "contents": [{"parts": [{"text": EXTRACTION_PROMPT.format(
                now=utc_now(), transcript=transcript)}]}],
            "generationConfig": {"responseMimeType": "application/json",
                                 "temperature": 0.1},
        },
        timeout=90,
    )
    response.raise_for_status()
    payload = response.json()
    text = payload["candidates"][0]["content"]["parts"][0]["text"]
    return _json_array(text)


def store_transcript_and_proposals(db: Database, capture_id: int, text: str,
                                   language: str | None, provider: str,
                                   proposals: list[dict[str, Any]]) -> int:
    if not text.strip():
        raise ValueError("transcript is empty")
    with db.connect() as connection:
        old = connection.execute(
            "SELECT id FROM transcripts WHERE capture_id=?", (capture_id,)
        ).fetchone()
        if old is not None:
            connection.execute(
                "DELETE FROM proposals WHERE transcript_id=?", (old["id"],)
            )
        connection.execute("DELETE FROM transcripts WHERE capture_id=?", (capture_id,))
        cursor = connection.execute(
            "INSERT INTO transcripts(capture_id, text, language, provider) VALUES (?, ?, ?, ?)",
            (capture_id, text.strip(), language, provider),
        )
        transcript_id = int(cursor.lastrowid)
        for item in proposals:
            kind = str(item.get("kind", "note")).lower()
            if kind not in {"task", "event", "reminder", "note"}:
                kind = "note"
            title = str(item.get("title", "")).strip()[:180]
            if not title:
                continue
            priority = item.get("priority", 0)
            priority = priority if isinstance(priority, int) and 0 <= priority <= 3 else 0
            connection.execute(
                """INSERT INTO proposals(
                       transcript_id, kind, title, details, proposed_at, due_at,
                       starts_at, ends_at, priority)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                (transcript_id, kind, title, str(item.get("details") or "")[:2000],
                 utc_now(), item.get("due_at"), item.get("starts_at"),
                 item.get("ends_at"), priority),
            )
        connection.execute(
            "UPDATE audio_captures SET status='ready', error=NULL WHERE id=?",
            (capture_id,),
        )
    return transcript_id


def process_capture(db: Database, settings: Settings, capture_id: int) -> None:
    with db.connect() as connection:
        row = connection.execute(
            "SELECT storage_path FROM audio_captures WHERE id=?", (capture_id,)
        ).fetchone()
    if row is None:
        return
    if not settings.groq_api_key:
        _set_capture(db, capture_id, "awaiting_transcription",
                     "Add GROQ_API_KEY or enter a transcript in Mission Control")
        return
    try:
        _set_capture(db, capture_id, "transcribing")
        text, language = transcribe(Path(row["storage_path"]), settings)
        _set_capture(db, capture_id, "extracting")
        proposals = extract(text, settings)
        store_transcript_and_proposals(
            db, capture_id, text, language, "groq", proposals)
    except Exception as error:  # Worker errors belong in the capture record.
        _set_capture(db, capture_id, "failed", str(error)[:1000])


def approve_proposal(db: Database, proposal_id: int, approve: bool) -> dict[str, Any] | None:
    with db.connect() as connection:
        row = connection.execute(
            "SELECT * FROM proposals WHERE id=?", (proposal_id,)
        ).fetchone()
        if row is None:
            return None
        if row["status"] != "pending":
            return dict(row)
        status = "approved" if approve else "rejected"
        if approve and row["kind"] in {"task", "reminder"}:
            connection.execute(
                "INSERT INTO tasks(title, due_at, priority) VALUES (?, ?, ?)",
                (row["title"], row["due_at"], row["priority"]),
            )
        elif approve and row["kind"] == "event" and row["starts_at"]:
            connection.execute(
                """INSERT INTO events(external_id, provider, calendar_name,
                                      title, starts_at, ends_at)
                   VALUES (?, 'aura-memory', 'Aura', ?, ?, ?)""",
                (f"proposal:{proposal_id}", row["title"], row["starts_at"],
                 row["ends_at"]),
            )
        elif approve and row["kind"] == "note":
            connection.execute(
                "INSERT INTO memories(proposal_id, title, body) VALUES (?, ?, ?)",
                (proposal_id, row["title"], row["details"]),
            )
        connection.execute(
            "UPDATE proposals SET status=?, resolved_at=? WHERE id=?",
            (status, utc_now(), proposal_id),
        )
        updated = connection.execute(
            "SELECT * FROM proposals WHERE id=?", (proposal_id,)
        ).fetchone()
    return dict(updated)
