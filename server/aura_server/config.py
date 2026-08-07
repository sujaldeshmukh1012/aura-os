from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Settings:
    api_token: str
    db_path: Path
    public_url: str
    admin_user: str
    admin_password: str
    data_dir: Path
    max_audio_bytes: int
    groq_api_key: str
    groq_whisper_model: str
    gemini_api_key: str
    gemini_model: str
    calendar_sync_minutes: int
    icloud_username: str
    icloud_app_password: str
    ms_graph_client_id: str
    ms_graph_tenant_id: str
    ms_graph_token_cache: Path


def load_settings() -> Settings:
    token = os.getenv("AURA_API_TOKEN", "development-only-token")
    db_path = Path(os.getenv("AURA_DB_PATH", "./data/aura.db")).expanduser()
    data_dir = Path(os.getenv("AURA_DATA_DIR", str(db_path.parent))).expanduser()
    return Settings(
        api_token=token,
        db_path=db_path,
        public_url=os.getenv("AURA_PUBLIC_URL", "http://127.0.0.1:8000").rstrip("/"),
        admin_user=os.getenv("AURA_ADMIN_USER", "aura"),
        admin_password=os.getenv("AURA_ADMIN_PASSWORD", "development-only-password"),
        data_dir=data_dir,
        max_audio_bytes=int(os.getenv("AURA_MAX_AUDIO_BYTES", str(100 * 1024 * 1024))),
        groq_api_key=os.getenv("GROQ_API_KEY", ""),
        groq_whisper_model=os.getenv("GROQ_WHISPER_MODEL", "whisper-large-v3-turbo"),
        gemini_api_key=os.getenv("GEMINI_API_KEY", ""),
        gemini_model=os.getenv("GEMINI_MODEL", "gemini-2.5-flash"),
        calendar_sync_minutes=max(1, int(os.getenv("CALENDAR_SYNC_MINUTES", "10"))),
        icloud_username=os.getenv("ICLOUD_USERNAME", ""),
        icloud_app_password=os.getenv("ICLOUD_APP_PASSWORD", ""),
        ms_graph_client_id=os.getenv("MS_GRAPH_CLIENT_ID", ""),
        ms_graph_tenant_id=os.getenv("MS_GRAPH_TENANT_ID", "common"),
        ms_graph_token_cache=Path(os.getenv(
            "MS_GRAPH_TOKEN_CACHE", str(data_dir / "ms-token-cache.json"))),
    )
