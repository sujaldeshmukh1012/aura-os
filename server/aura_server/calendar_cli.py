from __future__ import annotations

import sys

from .config import load_settings


def microsoft_login() -> None:
    import msal
    from .calendars import _load_msal_cache, _save_msal_cache

    settings = load_settings()
    if not settings.ms_graph_client_id:
        raise SystemExit("Set MS_GRAPH_CLIENT_ID first")
    cache = _load_msal_cache(settings)
    app = msal.PublicClientApplication(
        settings.ms_graph_client_id,
        authority=f"https://login.microsoftonline.com/{settings.ms_graph_tenant_id}",
        token_cache=cache,
    )
    flow = app.initiate_device_flow(scopes=["Calendars.Read"])
    if "user_code" not in flow:
        raise SystemExit(str(flow))
    print(flow["message"], flush=True)
    result = app.acquire_token_by_device_flow(flow)
    if "access_token" not in result:
        raise SystemExit(result.get("error_description", str(result)))
    _save_msal_cache(cache, settings.ms_graph_token_cache)
    print("Microsoft calendar authorization saved.")


if __name__ == "__main__":
    if sys.argv[1:] != ["microsoft-login"]:
        raise SystemExit("Usage: python -m aura_server.calendar_cli microsoft-login")
    microsoft_login()
