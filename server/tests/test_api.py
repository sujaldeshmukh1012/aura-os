from datetime import datetime, timedelta, timezone

from fastapi.testclient import TestClient

from aura_server import main
from aura_server.db import Database


def client_for(tmp_path):
    main.db = Database(tmp_path / "test.db")
    main.db.initialize()
    return TestClient(main.app), {"Authorization": f"Bearer {main.settings.api_token}"}


def test_health_is_public(tmp_path):
    client, _ = client_for(tmp_path)
    response = client.get("/health")
    assert response.status_code == 200
    assert response.json()["status"] == "ok"


def test_digest_requires_auth(tmp_path):
    client, _ = client_for(tmp_path)
    assert client.get("/api/v1/digest").status_code == 401


def test_web_editor_requires_admin_auth(tmp_path):
    client, _ = client_for(tmp_path)
    assert client.get("/").status_code == 401
    response = client.get("/", auth=(main.settings.admin_user,
                                     main.settings.admin_password))
    assert response.status_code == 200


def test_mission_control_round_trip(tmp_path):
    client, headers = client_for(tmp_path)
    project = client.post("/api/v1/projects", headers=headers,
                          json={"name": "Aura", "area": "Projects"}).json()
    task = client.post("/api/v1/tasks", headers=headers, json={
        "title": "Validate Phase 3", "project_id": project["id"], "priority": 2
    }).json()
    digest = client.get("/api/v1/digest", headers=headers).json()
    assert digest["tasks"][0]["title"] == "Validate Phase 3"
    response = client.post(f"/api/v1/tasks/{task['id']}/complete", headers=headers)
    assert response.status_code == 200
    assert client.get("/api/v1/digest", headers=headers).json()["tasks"] == []


def test_task_delete_requires_auth_and_removes_task(tmp_path):
    client, headers = client_for(tmp_path)
    task = client.post(
        "/api/v1/tasks", headers=headers, json={"title": "Disposable task"}
    ).json()
    url = f"/api/v1/tasks/{task['id']}"
    assert client.delete(url).status_code == 401
    assert client.delete(url, headers=headers).status_code == 204
    assert client.get("/api/v1/digest", headers=headers).json()["tasks"] == []
    assert client.delete(url, headers=headers).status_code == 404


def test_calendar_event_appears_in_digest(tmp_path):
    client, headers = client_for(tmp_path)
    start = datetime.now(timezone.utc) + timedelta(hours=2)
    response = client.post("/api/v1/events", headers=headers, json={
        "external_id": "test-event", "provider": "test", "title": "Office hours",
        "starts_at": start.isoformat(),
    })
    assert response.status_code == 201
    digest = client.get("/api/v1/digest", headers=headers).json()
    assert digest["events"][0]["title"] == "Office hours"


def test_capture_upload_is_idempotent_and_accepts_manual_transcript(tmp_path):
    client, headers = client_for(tmp_path)
    audio = b"RIFF" + b"\0" * 128
    upload_headers = headers | {
        "Content-Type": "audio/wav",
        "X-Aura-Filename": "capture-001.wav",
        "X-Aura-Device": "test-watch",
    }
    first = client.post("/api/v1/audio", headers=upload_headers, content=audio)
    assert first.status_code == 202
    assert first.json()["status"] == "uploaded"
    second = client.post("/api/v1/audio", headers=upload_headers, content=audio)
    assert second.status_code == 202
    assert second.json()["id"] == first.json()["id"]

    capture_id = first.json()["id"]
    response = client.post(
        f"/api/v1/captures/{capture_id}/transcript", headers=headers,
        json={"text": "Remember that the prototype passed its validation."},
    )
    assert response.status_code == 200
    captures = client.get("/api/v1/captures", headers=headers).json()
    assert captures[0]["status"] == "ready"
    assert "prototype passed" in captures[0]["transcript"]


def test_capture_audio_requires_admin_and_streams_file(tmp_path):
    client, _ = client_for(tmp_path)
    audio = b"RIFF" + b"test audio bytes"
    path = tmp_path / "capture.wav"
    path.write_bytes(audio)
    with main.db.connect() as connection:
        capture_id = connection.execute(
            """INSERT INTO audio_captures(
                   filename, content_type, storage_path, byte_count, sha256)
               VALUES (?, ?, ?, ?, ?)""",
            ("capture.wav", "audio/wav", str(path), len(audio), "test-audio-sha"),
        ).lastrowid

    url = f"/web/captures/{capture_id}/audio"
    assert client.get(url).status_code == 401
    response = client.get(
        url, auth=(main.settings.admin_user, main.settings.admin_password)
    )
    assert response.status_code == 200
    assert response.headers["content-type"] == "audio/wav"
    assert response.headers["content-disposition"] == "inline"
    assert response.content == audio
    editor = client.get(
        "/", auth=(main.settings.admin_user, main.settings.admin_password)
    )
    assert f"/web/captures/{capture_id}/audio" in editor.text
    assert "<audio controls" in editor.text
    assert "Download WAV" in editor.text


def test_capture_audio_returns_404_when_file_is_missing(tmp_path):
    client, _ = client_for(tmp_path)
    with main.db.connect() as connection:
        capture_id = connection.execute(
            """INSERT INTO audio_captures(
                   filename, content_type, storage_path, byte_count, sha256)
               VALUES (?, ?, ?, ?, ?)""",
            ("missing.wav", "audio/wav", str(tmp_path / "missing.wav"), 1,
             "missing-audio-sha"),
        ).lastrowid
    response = client.get(
        f"/web/captures/{capture_id}/audio",
        auth=(main.settings.admin_user, main.settings.admin_password),
    )
    assert response.status_code == 404


def test_capture_retry_requires_auth_and_queues_processing(tmp_path, monkeypatch):
    client, headers = client_for(tmp_path)
    path = tmp_path / "retry.wav"
    path.write_bytes(b"RIFFretry")
    with main.db.connect() as connection:
        capture_id = connection.execute(
            """INSERT INTO audio_captures(
                   filename, content_type, storage_path, byte_count, sha256,
                   status, error)
               VALUES (?, ?, ?, ?, ?, 'failed', 'old error')""",
            ("retry.wav", "audio/wav", str(path), path.stat().st_size,
             "retry-audio-sha"),
        ).lastrowid
    processed = []
    monkeypatch.setattr(
        main, "process_capture",
        lambda database, settings, item_id: processed.append(item_id),
    )

    url = f"/api/v1/captures/{capture_id}/retry"
    assert client.post(url).status_code == 401
    response = client.post(url, headers=headers)
    assert response.status_code == 202
    assert response.json() == {"id": capture_id, "status": "queued"}
    assert processed == [capture_id]
    with main.db.connect() as connection:
        capture = connection.execute(
            "SELECT status, error FROM audio_captures WHERE id=?", (capture_id,)
        ).fetchone()
    assert capture["status"] == "queued"
    assert capture["error"] is None


def test_web_capture_retry_is_available(tmp_path, monkeypatch):
    client, _ = client_for(tmp_path)
    path = tmp_path / "web-retry.wav"
    path.write_bytes(b"RIFFweb retry")
    with main.db.connect() as connection:
        capture_id = connection.execute(
            """INSERT INTO audio_captures(
                   filename, content_type, storage_path, byte_count, sha256,
                   status)
               VALUES (?, ?, ?, ?, ?, 'awaiting_transcription')""",
            ("web-retry.wav", "audio/wav", str(path), path.stat().st_size,
             "web-retry-audio-sha"),
        ).lastrowid
    processed = []
    monkeypatch.setattr(
        main, "process_capture",
        lambda database, settings, item_id: processed.append(item_id),
    )
    auth = (main.settings.admin_user, main.settings.admin_password)
    editor = client.get("/", auth=auth)
    assert f"/web/captures/{capture_id}/retry" in editor.text
    assert "Retry AI processing" in editor.text
    response = client.post(
        f"/web/captures/{capture_id}/retry", auth=auth,
        follow_redirects=False,
    )
    assert response.status_code == 303
    assert processed == [capture_id]


def test_proposal_approval_creates_a_task(tmp_path):
    client, headers = client_for(tmp_path)
    with main.db.connect() as connection:
        proposal_id = connection.execute(
            """INSERT INTO proposals(kind, title, details, priority)
               VALUES ('task', 'Submit phase report', 'Before Friday', 2)"""
        ).lastrowid
    digest = client.get("/api/v1/digest", headers=headers).json()
    assert digest["proposals"][0]["title"] == "Submit phase report"
    response = client.post(
        f"/api/v1/proposals/{proposal_id}/resolve", headers=headers,
        json={"approved": True},
    )
    assert response.status_code == 200
    digest = client.get("/api/v1/digest", headers=headers).json()
    assert digest["proposals"] == []
    assert digest["tasks"][0]["title"] == "Submit phase report"


def test_approved_note_is_saved_as_memory(tmp_path):
    client, headers = client_for(tmp_path)
    with main.db.connect() as connection:
        proposal_id = connection.execute(
            """INSERT INTO proposals(kind, title, details)
               VALUES ('note', 'Prototype result', 'Phase 4 passed')"""
        ).lastrowid
    response = client.post(
        f"/api/v1/proposals/{proposal_id}/resolve", headers=headers,
        json={"approved": True},
    )
    assert response.status_code == 200
    memories = client.get("/api/v1/memories", headers=headers).json()
    assert memories[0]["title"] == "Prototype result"
    assert memories[0]["body"] == "Phase 4 passed"
