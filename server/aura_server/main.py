from __future__ import annotations

import asyncio
import hashlib
import html
import secrets
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Annotated

from fastapi import (BackgroundTasks, Depends, FastAPI, File, Form, Header,
                     HTTPException, Query, Request, UploadFile, status)
from fastapi.security import HTTPBasic, HTTPBasicCredentials
from fastapi.responses import FileResponse, HTMLResponse, RedirectResponse
from pydantic import BaseModel, Field

from . import __version__
from .config import Settings, load_settings
from .calendars import calendar_sync_loop, sync_all
from .db import Database
from .memory import approve_proposal, extract, process_capture, store_transcript_and_proposals


settings: Settings = load_settings()
db = Database(settings.db_path)


@asynccontextmanager
async def lifespan(_: FastAPI):
    db.initialize()
    settings.data_dir.mkdir(parents=True, exist_ok=True)
    sync_task = asyncio.create_task(calendar_sync_loop(db, settings))
    try:
        yield
    finally:
        sync_task.cancel()
        try:
            await sync_task
        except asyncio.CancelledError:
            pass


app = FastAPI(title="Aura Server", version=__version__, lifespan=lifespan)
basic = HTTPBasic()


def require_token(authorization: Annotated[str | None, Header()] = None) -> None:
    expected = f"Bearer {settings.api_token}"
    if authorization is None or not secrets.compare_digest(authorization, expected):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED,
                            detail="invalid bearer token")


Auth = Annotated[None, Depends(require_token)]


def require_admin(credentials: Annotated[HTTPBasicCredentials, Depends(basic)]) -> None:
    valid_user = secrets.compare_digest(credentials.username, settings.admin_user)
    valid_password = secrets.compare_digest(credentials.password, settings.admin_password)
    if not (valid_user and valid_password):
        raise HTTPException(status_code=401, detail="invalid admin credentials",
                            headers={"WWW-Authenticate": "Basic"})


class ProjectCreate(BaseModel):
    name: str = Field(min_length=1, max_length=80)
    area: str = Field(default="Personal", min_length=1, max_length=40)
    color: str = Field(default="#38B6FF", pattern=r"^#[0-9A-Fa-f]{6}$")


class TaskCreate(BaseModel):
    title: str = Field(min_length=1, max_length=160)
    project_id: int | None = None
    due_at: datetime | None = None
    priority: int = Field(default=0, ge=0, le=3)


class EventUpsert(BaseModel):
    external_id: str | None = Field(default=None, max_length=255)
    provider: str = Field(default="manual", max_length=32)
    calendar_name: str = Field(default="Aura", max_length=80)
    title: str = Field(min_length=1, max_length=180)
    starts_at: datetime
    ends_at: datetime | None = None
    location: str | None = Field(default=None, max_length=180)
    all_day: bool = False


class ManualTranscript(BaseModel):
    text: str = Field(min_length=1, max_length=100_000)
    language: str | None = Field(default=None, max_length=20)


class ProposalResolution(BaseModel):
    approved: bool


def row_dict(row):
    return dict(row) if row is not None else None


def event_digest(row):
    item = row_dict(row)
    item["start_epoch"] = int(datetime.fromisoformat(item["starts_at"]).timestamp())
    return item


@app.get("/health")
def health():
    with db.connect() as connection:
        connection.execute("SELECT 1").fetchone()
    return {"status": "ok", "version": __version__,
            "time": datetime.now(timezone.utc).isoformat()}


@app.get("/api/v1/digest", dependencies=[Depends(require_token)])
def digest(hours: int = Query(default=48, ge=1, le=168)):
    now = datetime.now(timezone.utc)
    until = now + timedelta(hours=hours)
    with db.connect() as connection:
        events = connection.execute(
            """SELECT id, provider, calendar_name, title, starts_at, ends_at,
                      location, all_day
               FROM events WHERE starts_at >= ? AND starts_at < ?
               ORDER BY starts_at LIMIT 24""",
            (now.isoformat(), until.isoformat()),
        ).fetchall()
        tasks = connection.execute(
            """SELECT t.id, t.title, t.due_at, t.priority, t.project_id,
                      COALESCE(p.name, 'Inbox') AS project
               FROM tasks t LEFT JOIN projects p ON p.id=t.project_id
               WHERE t.completed=0
               ORDER BY CASE WHEN t.due_at IS NULL THEN 1 ELSE 0 END,
                        t.due_at, t.priority DESC, t.id LIMIT 12"""
        ).fetchall()
        proposals = connection.execute(
            """SELECT id, kind, title, details, due_at, starts_at, priority
               FROM proposals WHERE status='pending'
               ORDER BY created_at DESC LIMIT 10"""
        ).fetchall()
    return {
        "schema": 1,
        "generated_at": now.isoformat(),
        "events": [event_digest(row) for row in events],
        "tasks": [row_dict(row) for row in tasks],
        "proposals": [row_dict(row) for row in proposals],
    }


@app.get("/api/v1/projects", dependencies=[Depends(require_token)])
def list_projects():
    with db.connect() as connection:
        rows = connection.execute(
            "SELECT * FROM projects WHERE archived=0 ORDER BY area, name"
        ).fetchall()
    return [row_dict(row) for row in rows]


@app.post("/api/v1/projects", status_code=201, dependencies=[Depends(require_token)])
def create_project(project: ProjectCreate):
    with db.connect() as connection:
        cursor = connection.execute(
            "INSERT INTO projects(name, area, color) VALUES (?, ?, ?)",
            (project.name, project.area, project.color),
        )
        row = connection.execute("SELECT * FROM projects WHERE id=?",
                                 (cursor.lastrowid,)).fetchone()
    return row_dict(row)


@app.get("/api/v1/tasks", dependencies=[Depends(require_token)])
def list_tasks(include_completed: bool = False):
    where = "" if include_completed else "WHERE t.completed=0"
    with db.connect() as connection:
        rows = connection.execute(
            f"""SELECT t.*, COALESCE(p.name, 'Inbox') AS project
                 FROM tasks t LEFT JOIN projects p ON p.id=t.project_id
                 {where} ORDER BY t.completed, t.due_at, t.priority DESC"""
        ).fetchall()
    return [row_dict(row) for row in rows]


@app.post("/api/v1/tasks", status_code=201, dependencies=[Depends(require_token)])
def create_task(task: TaskCreate):
    due_at = task.due_at.astimezone(timezone.utc).isoformat() if task.due_at else None
    with db.connect() as connection:
        cursor = connection.execute(
            "INSERT INTO tasks(title, project_id, due_at, priority) VALUES (?, ?, ?, ?)",
            (task.title, task.project_id, due_at, task.priority),
        )
        row = connection.execute("SELECT * FROM tasks WHERE id=?",
                                 (cursor.lastrowid,)).fetchone()
    return row_dict(row)


@app.post("/api/v1/tasks/{task_id}/complete", dependencies=[Depends(require_token)])
def complete_task(task_id: int, completed: bool = True):
    with db.connect() as connection:
        cursor = connection.execute(
            "UPDATE tasks SET completed=?, completed_at=? WHERE id=?",
            (int(completed), datetime.now(timezone.utc).isoformat() if completed else None,
             task_id),
        )
        if cursor.rowcount == 0:
            raise HTTPException(status_code=404, detail="task not found")
    return {"id": task_id, "completed": completed}


@app.delete("/api/v1/tasks/{task_id}", status_code=204,
            dependencies=[Depends(require_token)])
def delete_task(task_id: int):
    with db.connect() as connection:
        cursor = connection.execute("DELETE FROM tasks WHERE id=?", (task_id,))
        if cursor.rowcount == 0:
            raise HTTPException(status_code=404, detail="task not found")


@app.post("/api/v1/events", status_code=201, dependencies=[Depends(require_token)])
def upsert_event(event: EventUpsert):
    external_id = event.external_id or f"manual:{secrets.token_hex(12)}"
    values = (external_id, event.provider, event.calendar_name, event.title,
              event.starts_at.astimezone(timezone.utc).isoformat(),
              event.ends_at.astimezone(timezone.utc).isoformat() if event.ends_at else None,
              event.location, int(event.all_day))
    with db.connect() as connection:
        connection.execute(
            """INSERT INTO events(external_id, provider, calendar_name, title,
                                  starts_at, ends_at, location, all_day)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?)
               ON CONFLICT(external_id) DO UPDATE SET
                 provider=excluded.provider, calendar_name=excluded.calendar_name,
                 title=excluded.title, starts_at=excluded.starts_at,
                 ends_at=excluded.ends_at, location=excluded.location,
                 all_day=excluded.all_day, updated_at=CURRENT_TIMESTAMP""",
            values,
        )
        row = connection.execute("SELECT * FROM events WHERE external_id=?",
                                 (external_id,)).fetchone()
    return row_dict(row)


@app.post("/api/v1/calendar/sync", dependencies=[Depends(require_token)])
def sync_calendars():
    return sync_all(db, settings)


@app.get("/api/v1/calendar/status", dependencies=[Depends(require_token)])
def calendar_status():
    with db.connect() as connection:
        rows = connection.execute(
            "SELECT provider, status, detail, synced_at FROM provider_sync ORDER BY provider"
        ).fetchall()
    return [row_dict(row) for row in rows]


@app.post("/api/v1/audio", status_code=202, dependencies=[Depends(require_token)])
async def upload_audio(
    request: Request,
    background: BackgroundTasks,
    x_aura_filename: Annotated[str | None, Header()] = None,
    x_aura_device: Annotated[str | None, Header()] = None,
    x_aura_captured_at: Annotated[str | None, Header()] = None,
    x_aura_duration: Annotated[float | None, Header()] = None,
):
    content_length = request.headers.get("content-length")
    if content_length and int(content_length) > settings.max_audio_bytes:
        raise HTTPException(413, "audio capture is too large")
    body = await request.body()
    if not body:
        raise HTTPException(400, "empty audio capture")
    if len(body) > settings.max_audio_bytes:
        raise HTTPException(413, "audio capture is too large")
    digest_hex = hashlib.sha256(body).hexdigest()
    with db.connect() as connection:
        existing = connection.execute(
            "SELECT * FROM audio_captures WHERE sha256=?", (digest_hex,)
        ).fetchone()
    if existing is not None:
        return row_dict(existing)
    suffix = Path(x_aura_filename or "capture.wav").suffix.lower()
    if suffix not in {".wav", ".pcm", ".opus", ".ogg", ".m4a"}:
        suffix = ".wav"
    capture_dir = settings.data_dir / "audio"
    capture_dir.mkdir(parents=True, exist_ok=True)
    path = capture_dir / f"{digest_hex}{suffix}"
    path.write_bytes(body)
    with db.connect() as connection:
        cursor = connection.execute(
            """INSERT INTO audio_captures(
                   device_id, filename, content_type, storage_path, byte_count,
                   sha256, duration_seconds, captured_at)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
            ((x_aura_device or "aura-watch")[:80],
             Path(x_aura_filename or path.name).name[:180],
             request.headers.get("content-type", "application/octet-stream")[:100],
             str(path), len(body), digest_hex, x_aura_duration, x_aura_captured_at),
        )
        capture_id = int(cursor.lastrowid)
        row = connection.execute(
            "SELECT * FROM audio_captures WHERE id=?", (capture_id,)
        ).fetchone()
    background.add_task(process_capture, db, settings, capture_id)
    return row_dict(row)


@app.get("/api/v1/captures", dependencies=[Depends(require_token)])
def list_captures(limit: int = Query(default=50, ge=1, le=200)):
    with db.connect() as connection:
        rows = connection.execute(
            """SELECT c.*, t.id AS transcript_id, t.text AS transcript
               FROM audio_captures c LEFT JOIN transcripts t ON t.capture_id=c.id
               ORDER BY c.uploaded_at DESC LIMIT ?""", (limit,)
        ).fetchall()
    return [row_dict(row) for row in rows]


@app.get("/web/captures/{capture_id}/audio",
         dependencies=[Depends(require_admin)])
def play_capture(capture_id: int):
    with db.connect() as connection:
        capture = connection.execute(
            "SELECT filename, content_type, storage_path FROM audio_captures WHERE id=?",
            (capture_id,),
        ).fetchone()
    if capture is None:
        raise HTTPException(404, "capture not found")
    path = Path(capture["storage_path"])
    if not path.is_file():
        raise HTTPException(404, "capture audio is missing")
    media_type = capture["content_type"] or "audio/wav"
    return FileResponse(
        path,
        media_type=media_type,
        headers={"Content-Disposition": "inline"},
    )


def queue_capture_processing(capture_id: int, background: BackgroundTasks) -> dict:
    with db.connect() as connection:
        capture = connection.execute(
            "SELECT id, status, storage_path FROM audio_captures WHERE id=?",
            (capture_id,),
        ).fetchone()
        if capture is None:
            raise HTTPException(404, "capture not found")
        if capture["status"] in {"transcribing", "extracting"}:
            raise HTTPException(409, "capture is already being processed")
        if not Path(capture["storage_path"]).is_file():
            raise HTTPException(404, "capture audio is missing")
        connection.execute(
            "UPDATE audio_captures SET status='queued', error=NULL WHERE id=?",
            (capture_id,),
        )
    background.add_task(process_capture, db, settings, capture_id)
    return {"id": capture_id, "status": "queued"}


@app.post("/api/v1/captures/{capture_id}/retry", status_code=202,
          dependencies=[Depends(require_token)])
def retry_capture(capture_id: int, background: BackgroundTasks):
    return queue_capture_processing(capture_id, background)


@app.post("/api/v1/captures/{capture_id}/transcript",
          dependencies=[Depends(require_token)])
def set_manual_transcript(capture_id: int, transcript: ManualTranscript):
    with db.connect() as connection:
        exists = connection.execute(
            "SELECT id FROM audio_captures WHERE id=?", (capture_id,)
        ).fetchone()
    if exists is None:
        raise HTTPException(404, "capture not found")
    proposals = extract(transcript.text, settings)
    transcript_id = store_transcript_and_proposals(
        db, capture_id, transcript.text, transcript.language, "manual", proposals)
    return {"capture_id": capture_id, "transcript_id": transcript_id,
            "proposal_count": len(proposals)}


@app.get("/api/v1/proposals", dependencies=[Depends(require_token)])
def list_proposals(proposal_status: str = Query(default="pending", alias="status")):
    if proposal_status not in {"pending", "approved", "rejected", "all"}:
        raise HTTPException(400, "invalid proposal status")
    where = "" if proposal_status == "all" else "WHERE p.status=?"
    params = () if proposal_status == "all" else (proposal_status,)
    with db.connect() as connection:
        rows = connection.execute(
            f"""SELECT p.*, t.text AS transcript FROM proposals p
                 LEFT JOIN transcripts t ON t.id=p.transcript_id
                 {where} ORDER BY p.created_at DESC""", params
        ).fetchall()
    return [row_dict(row) for row in rows]


@app.get("/api/v1/memories", dependencies=[Depends(require_token)])
def list_memories(limit: int = Query(default=100, ge=1, le=500)):
    with db.connect() as connection:
        rows = connection.execute(
            "SELECT * FROM memories ORDER BY created_at DESC LIMIT ?", (limit,)
        ).fetchall()
    return [row_dict(row) for row in rows]


@app.post("/api/v1/proposals/{proposal_id}/resolve",
          dependencies=[Depends(require_token)])
def resolve_proposal(proposal_id: int, resolution: ProposalResolution):
    item = approve_proposal(db, proposal_id, resolution.approved)
    if item is None:
        raise HTTPException(404, "proposal not found")
    return item


def page(body: str) -> str:
    return f"""<!doctype html><html><head><meta name=viewport content='width=device-width'>
    <title>Aura Mission Control</title><style>
    :root{{color-scheme:dark;--blue:#38b6ff;--card:#151518;--muted:#999}}
    *{{box-sizing:border-box}}body{{font:16px system-ui;margin:0;background:#08080a;color:#f2f2f2}}
    main{{max-width:920px;margin:auto;padding:24px 16px 64px}}h1{{color:var(--blue)}}h2{{margin-top:40px}}
    nav{{position:sticky;top:0;background:#08080aee;padding:12px 0;z-index:2;display:flex;gap:14px;overflow:auto}}
    nav a{{color:var(--blue);text-decoration:none;white-space:nowrap}}
    form,.card{{background:var(--card);padding:16px;border-radius:16px;margin:12px 0}}
    audio{{display:block;width:100%;margin:12px 0}}.download{{color:var(--blue)}}
    .inline{{display:inline;background:none;padding:0;margin:0}}.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}}
    input,select,textarea,button{{font:inherit;padding:12px;border-radius:10px;border:1px solid #333;background:#222;color:#fff;max-width:100%}}
    textarea{{width:100%;min-height:100px}}input[name=title]{{width:min(460px,100%)}}
    button{{background:#0b4968;cursor:pointer}}button.reject{{background:#522}}.done{{opacity:.5;text-decoration:line-through}}
    small,.muted{{color:var(--muted)}}.pill{{font-size:12px;background:#29292d;border-radius:999px;padding:4px 9px}}</style></head>
    <body><main><nav><a href=#today>Today</a><a href=#memory>Memory</a><a href=#calendar>Calendar</a><a href=#system>System</a></nav>{body}</main></body></html>"""


@app.get("/", response_class=HTMLResponse, dependencies=[Depends(require_admin)])
def mission_editor():
    with db.connect() as connection:
        tasks = connection.execute(
            """SELECT t.*, COALESCE(p.name, 'Inbox') project FROM tasks t
               LEFT JOIN projects p ON p.id=t.project_id
               ORDER BY t.completed, t.due_at, t.priority DESC"""
        ).fetchall()
        projects = connection.execute(
            "SELECT * FROM projects WHERE archived=0 ORDER BY area,name"
        ).fetchall()
        proposals = connection.execute(
            "SELECT * FROM proposals WHERE status='pending' ORDER BY created_at DESC"
        ).fetchall()
        captures = connection.execute(
            """SELECT c.*, t.text transcript FROM audio_captures c
               LEFT JOIN transcripts t ON t.capture_id=c.id
               ORDER BY uploaded_at DESC LIMIT 20"""
        ).fetchall()
        events = connection.execute(
            """SELECT * FROM events WHERE starts_at>=? ORDER BY starts_at LIMIT 20""",
            (datetime.now(timezone.utc).isoformat(),),
        ).fetchall()
        providers = connection.execute(
            "SELECT * FROM provider_sync ORDER BY provider"
        ).fetchall()
        memories = connection.execute(
            "SELECT * FROM memories ORDER BY created_at DESC LIMIT 20"
        ).fetchall()
    cards = "".join(
        f"<div class='card {'done' if row['completed'] else ''}'>"
        f"<strong>{html.escape(row['title'])}</strong><br>"
        f"<small>{html.escape(row['project'])} · priority {row['priority']}</small> "
        f"<form method=post action='/web/tasks/{row['id']}/toggle' class=inline>"
        f"<button>{'Reopen' if row['completed'] else 'Complete'}</button></form></div>"
        for row in tasks
    ) or "<p>No tasks yet.</p>"
    project_options = "<option value=''>Inbox</option>" + "".join(
        f"<option value={row['id']}>{html.escape(row['name'])}</option>" for row in projects)
    proposal_cards = "".join(
        f"<div class=card><span class=pill>{html.escape(row['kind'])}</span> "
        f"<strong>{html.escape(row['title'])}</strong><p>{html.escape(row['details'] or '')}</p>"
        f"<form class=inline method=post action='/web/proposals/{row['id']}/approve'><button>Approve</button></form> "
        f"<form class=inline method=post action='/web/proposals/{row['id']}/reject'><button class=reject>Reject</button></form></div>"
        for row in proposals) or "<p class=muted>Nothing waiting for review.</p>"
    capture_cards = "".join(
        f"<div class=card><strong>{html.escape(row['filename'])}</strong> <span class=pill>{html.escape(row['status'])}</span>"
        f"<p><small>{row['byte_count']} bytes · {html.escape(row['uploaded_at'])}</small></p>"
        f"<audio controls preload=metadata src='/web/captures/{row['id']}/audio'></audio>"
        f"<a class=download href='/web/captures/{row['id']}/audio' download='{html.escape(row['filename'])}'>Download WAV</a>"
        + (f"<p class=muted>{html.escape(row['error'])}</p>" if row['error'] else "")
        + (f"<form class=inline method=post action='/web/captures/{row['id']}/retry'><button>Retry AI processing</button></form>"
           if row['status'] not in {"transcribing", "extracting"} else "")
        + (f"<p>{html.escape(row['transcript'])}</p>" if row['transcript'] else
           f"<form method=post action='/web/captures/{row['id']}/transcript'><textarea name=text placeholder='Enter transcript manually'></textarea><button>Save and extract</button></form>")
        + "</div>" for row in captures) or "<p class=muted>No captures uploaded yet.</p>"
    memory_cards = "".join(
        f"<div class=card><strong>{html.escape(row['title'])}</strong>"
        f"<p>{html.escape(row['body'] or '')}</p>"
        f"<small>{html.escape(row['created_at'])}</small></div>"
        for row in memories) or "<p class=muted>No approved notes yet.</p>"
    event_cards = "".join(
        f"<div class=card><strong>{html.escape(row['title'])}</strong><br>"
        f"<small>{html.escape(row['starts_at'])} · {html.escape(row['calendar_name'])}</small></div>"
        for row in events) or "<p class=muted>No upcoming events.</p>"
    provider_cards = "".join(
        f"<div class=card><strong>{html.escape(row['provider'])}</strong> "
        f"<span class=pill>{html.escape(row['status'])}</span><br><small>{html.escape(row['detail'] or '')}</small></div>"
        for row in providers) or "<p class=muted>Calendar providers are not configured yet.</p>"
    body = f"""<h1>Aura Mission Control</h1>
      <section id=today><h2>Today</h2><form method=post action=/web/tasks>
      <input name=title placeholder='What needs doing?' maxlength=160 required>
      <select name=project_id>{project_options}</select>
      <select name=priority><option value=0>Normal</option><option value=1>Important</option>
      <option value=2>High</option><option value=3>Critical</option></select>
      <button>Add task</button></form>{cards}</section>
      <section id=memory><h2>Memory review</h2>{proposal_cards}<h3>Saved notes</h3>
      {memory_cards}<h3>Captures</h3>
      <form method=post action=/web/audio enctype=multipart/form-data><input type=file name=file accept='audio/*' required><button>Upload audio</button></form>
      {capture_cards}</section><section id=calendar><h2>Calendar</h2>{event_cards}</section>
      <section id=system><h2>System</h2>{provider_cards}<form method=post action=/web/calendar/sync><button>Sync calendars now</button></form>
      <p><a href=/health>Public health check</a> · API version {html.escape(__version__)}</p></section>"""
    return page(body)


@app.post("/web/tasks", dependencies=[Depends(require_admin)])
def web_create_task(title: Annotated[str, Form(min_length=1, max_length=160)],
                    priority: Annotated[int, Form(ge=0, le=3)] = 0,
                    project_id: Annotated[int | None, Form()] = None):
    with db.connect() as connection:
        connection.execute("INSERT INTO tasks(title, priority, project_id) VALUES (?, ?, ?)",
                           (title, priority, project_id))
    return RedirectResponse("/", status_code=303)


@app.post("/web/proposals/{proposal_id}/{decision}", dependencies=[Depends(require_admin)])
def web_resolve_proposal(proposal_id: int, decision: str):
    if decision not in {"approve", "reject"}:
        raise HTTPException(400, "invalid decision")
    if approve_proposal(db, proposal_id, decision == "approve") is None:
        raise HTTPException(404, "proposal not found")
    return RedirectResponse("/#memory", status_code=303)


@app.post("/web/captures/{capture_id}/transcript", dependencies=[Depends(require_admin)])
def web_transcript(capture_id: int,
                   text: Annotated[str, Form(min_length=1, max_length=100_000)]):
    with db.connect() as connection:
        exists = connection.execute("SELECT id FROM audio_captures WHERE id=?",
                                    (capture_id,)).fetchone()
    if exists is None:
        raise HTTPException(404, "capture not found")
    proposals = extract(text, settings)
    store_transcript_and_proposals(db, capture_id, text, None, "manual", proposals)
    return RedirectResponse("/#memory", status_code=303)


@app.post("/web/captures/{capture_id}/retry", dependencies=[Depends(require_admin)])
def web_retry_capture(capture_id: int, background: BackgroundTasks):
    queue_capture_processing(capture_id, background)
    return RedirectResponse("/#memory", status_code=303)


@app.post("/web/audio", dependencies=[Depends(require_admin)])
async def web_audio(background: BackgroundTasks, file: Annotated[UploadFile, File()]):
    body = await file.read(settings.max_audio_bytes + 1)
    if not body or len(body) > settings.max_audio_bytes:
        raise HTTPException(413, "audio capture is empty or too large")
    digest_hex = hashlib.sha256(body).hexdigest()
    capture_dir = settings.data_dir / "audio"
    capture_dir.mkdir(parents=True, exist_ok=True)
    suffix = Path(file.filename or "capture.wav").suffix.lower() or ".wav"
    path = capture_dir / f"{digest_hex}{suffix}"
    path.write_bytes(body)
    with db.connect() as connection:
        existing = connection.execute("SELECT id FROM audio_captures WHERE sha256=?",
                                      (digest_hex,)).fetchone()
        if existing is None:
            cursor = connection.execute(
                """INSERT INTO audio_captures(filename, content_type, storage_path,
                                              byte_count, sha256)
                   VALUES (?, ?, ?, ?, ?)""",
                (Path(file.filename or path.name).name[:180], file.content_type or "audio/wav",
                 str(path), len(body), digest_hex),
            )
            capture_id = int(cursor.lastrowid)
        else:
            capture_id = int(existing["id"])
    background.add_task(process_capture, db, settings, capture_id)
    return RedirectResponse("/#memory", status_code=303)


@app.post("/web/calendar/sync", dependencies=[Depends(require_admin)])
def web_calendar_sync(background: BackgroundTasks):
    background.add_task(sync_all, db, settings)
    return RedirectResponse("/#system", status_code=303)


@app.post("/web/tasks/{task_id}/toggle", dependencies=[Depends(require_admin)])
def web_toggle_task(task_id: int):
    with db.connect() as connection:
        row = connection.execute("SELECT completed FROM tasks WHERE id=?",
                                 (task_id,)).fetchone()
        if row is not None:
            complete = not bool(row["completed"])
            connection.execute(
                "UPDATE tasks SET completed=?, completed_at=? WHERE id=?",
                (int(complete), datetime.now(timezone.utc).isoformat() if complete else None,
                 task_id),
            )
    return RedirectResponse("/", status_code=303)
