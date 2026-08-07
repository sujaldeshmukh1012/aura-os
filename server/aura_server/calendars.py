from __future__ import annotations

import asyncio
import json
from datetime import date, datetime, time, timedelta, timezone
from pathlib import Path
from typing import Any, Iterable

import httpx

from .config import Settings
from .db import Database


def _iso(value: date | datetime | None) -> tuple[str | None, bool]:
    if value is None:
        return None, False
    if isinstance(value, datetime):
        if value.tzinfo is None:
            value = value.replace(tzinfo=timezone.utc)
        return value.astimezone(timezone.utc).isoformat(), False
    return datetime.combine(value, time.min, tzinfo=timezone.utc).isoformat(), True


def _upsert(db: Database, events: Iterable[dict[str, Any]]) -> int:
    count = 0
    with db.connect() as connection:
        for event in events:
            connection.execute(
                """INSERT INTO events(external_id, provider, calendar_name,
                                      title, starts_at, ends_at, location, all_day)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                   ON CONFLICT(external_id) DO UPDATE SET
                     provider=excluded.provider, calendar_name=excluded.calendar_name,
                     title=excluded.title, starts_at=excluded.starts_at,
                     ends_at=excluded.ends_at, location=excluded.location,
                     all_day=excluded.all_day, updated_at=CURRENT_TIMESTAMP""",
                (event["external_id"], event["provider"], event["calendar_name"],
                 event["title"], event["starts_at"], event.get("ends_at"),
                 event.get("location"), int(event.get("all_day", False))),
            )
            count += 1
    return count


def _status(db: Database, provider: str, status: str, detail: str) -> None:
    with db.connect() as connection:
        connection.execute(
            """INSERT INTO provider_sync(provider, status, detail)
               VALUES (?, ?, ?) ON CONFLICT(provider) DO UPDATE SET
               status=excluded.status, detail=excluded.detail,
               synced_at=CURRENT_TIMESTAMP""",
            (provider, status, detail[:1000]),
        )


def sync_icloud(db: Database, settings: Settings) -> int:
    if not settings.icloud_username or not settings.icloud_app_password:
        return 0
    try:
        import caldav
        client = caldav.DAVClient(
            url="https://caldav.icloud.com",
            username=settings.icloud_username,
            password=settings.icloud_app_password,
        )
        principal = client.principal()
        start, end = datetime.now(timezone.utc), datetime.now(timezone.utc) + timedelta(days=30)
        normalized: list[dict[str, Any]] = []
        for calendar in principal.calendars():
            for result in calendar.search(start=start, end=end, event=True,
                                          expand=True):
                component = result.icalendar_component
                starts_at, all_day = _iso(component.decoded("dtstart"))
                ends_at, _ = _iso(component.decoded("dtend", None))
                uid = str(component.get("uid", result.url))
                normalized.append({
                    "external_id": f"icloud:{uid}", "provider": "icloud",
                    "calendar_name": calendar.name or "iCloud",
                    "title": str(component.get("summary", "Untitled")),
                    "starts_at": starts_at, "ends_at": ends_at,
                    "location": str(component.get("location", "")) or None,
                    "all_day": all_day,
                })
        count = _upsert(db, normalized)
        _status(db, "icloud", "ok", f"{count} upcoming events")
        return count
    except Exception as error:
        _status(db, "icloud", "error", str(error))
        return 0


def _load_msal_cache(settings: Settings):
    import msal
    cache = msal.SerializableTokenCache()
    if settings.ms_graph_token_cache.exists():
        cache.deserialize(settings.ms_graph_token_cache.read_text())
    return cache


def _save_msal_cache(cache, path: Path) -> None:
    if cache.has_state_changed:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(cache.serialize())
        path.chmod(0o600)


def graph_access_token(settings: Settings) -> str | None:
    if not settings.ms_graph_client_id:
        return None
    import msal
    cache = _load_msal_cache(settings)
    app = msal.PublicClientApplication(
        settings.ms_graph_client_id,
        authority=f"https://login.microsoftonline.com/{settings.ms_graph_tenant_id}",
        token_cache=cache,
    )
    accounts = app.get_accounts()
    result = app.acquire_token_silent(["Calendars.Read"], account=accounts[0]) if accounts else None
    _save_msal_cache(cache, settings.ms_graph_token_cache)
    return result.get("access_token") if result else None


def sync_microsoft(db: Database, settings: Settings) -> int:
    if not settings.ms_graph_client_id:
        return 0
    try:
        token = graph_access_token(settings)
        if not token:
            _status(db, "microsoft", "authorization_required",
                    "Run python -m aura_server.calendar_cli microsoft-login")
            return 0
        start = datetime.now(timezone.utc)
        end = start + timedelta(days=30)
        response = httpx.get(
            "https://graph.microsoft.com/v1.0/me/calendarView",
            headers={"Authorization": f"Bearer {token}"},
            params={"startDateTime": start.isoformat(), "endDateTime": end.isoformat(),
                    "$orderby": "start/dateTime", "$top": "250"},
            timeout=60,
        )
        response.raise_for_status()
        normalized = []
        for item in response.json().get("value", []):
            start_value = datetime.fromisoformat(item["start"]["dateTime"].replace("Z", "+00:00"))
            end_value = datetime.fromisoformat(item["end"]["dateTime"].replace("Z", "+00:00"))
            starts_at, _ = _iso(start_value)
            ends_at, _ = _iso(end_value)
            normalized.append({
                "external_id": f"microsoft:{item['id']}", "provider": "microsoft",
                "calendar_name": "Microsoft", "title": item.get("subject") or "Untitled",
                "starts_at": starts_at, "ends_at": ends_at,
                "location": (item.get("location") or {}).get("displayName") or None,
                "all_day": bool(item.get("isAllDay")),
            })
        count = _upsert(db, normalized)
        _status(db, "microsoft", "ok", f"{count} upcoming events")
        return count
    except Exception as error:
        _status(db, "microsoft", "error", str(error))
        return 0


def sync_all(db: Database, settings: Settings) -> dict[str, int]:
    return {"icloud": sync_icloud(db, settings),
            "microsoft": sync_microsoft(db, settings)}


async def calendar_sync_loop(db: Database, settings: Settings) -> None:
    while True:
        await asyncio.to_thread(sync_all, db, settings)
        await asyncio.sleep(settings.calendar_sync_minutes * 60)
