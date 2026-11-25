# ADR 0001: Local-first, protocol-separated foundation

Status: accepted — 2026-11-24

## Decision

Use a transport-neutral framed JSON protocol between a constrained device read model and a local host authority. Implement the portable host core in standard-library Python, retain a native Swift/AppKit shell for macOS UX and permissions, and defer PostgreSQL/FastAPI infrastructure until persistence-backed features require it.

## Consequences

- Firmware, TCP simulator, and future WebSocket transports share message semantics.
- The foundation runs without package installation or cloud services.
- Native platform adapters can replace Python modules without protocol changes.
- JSON is less compact than binary formats but materially easier to inspect during board bring-up.
- A future measured bottleneck may justify negotiated binary payloads without changing domain message types.
