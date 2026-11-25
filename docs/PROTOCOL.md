# NEXUS protocol 1.0

## Envelope

Every message is one UTF-8 JSON object containing:

- `protocol_version`: currently `1.0`.
- `message_id`: UUID, unique per emitted message.
- `correlation_id`: UUID of the request, or `null`.
- `timestamp`: RFC 3339 UTC timestamp.
- `source` / `destination`: `{kind, id}` endpoints.
- `message_type`: namespaced type such as `telemetry.snapshot`.
- `payload`: type-specific object.
- `auth`: session metadata; secrets are never serialized here.

The canonical schema is `protocol/schemas/envelope.schema.json`.

## Stream framing

Serial and TCP streams use the same frame:

```text
uint32 payload_length_be
payload_length bytes of UTF-8 JSON
```

The maximum accepted payload is 65,536 bytes in the host foundation and 8,192 bytes on the device. A zero length, oversized frame, invalid UTF-8, duplicate key, invalid UUID, or malformed envelope terminates the development session. Future protocol versions may negotiate larger bounds.

Length framing was chosen over newline delimiters so logs, escaped text, and partial serial reads cannot alter message boundaries.

## Session sequence

1. Client sends `session.hello` with identity, role, supported versions, capabilities, and nonce.
2. Host selects a version and replies `session.welcome` with session ID and heartbeat interval.
3. Peers exchange `heartbeat.ping` / `heartbeat.pong`.
4. Host sends a full `state.snapshot`, followed by typed changes.
5. Reconnection repeats negotiation and reconciliation; old session IDs are invalid.

The development TCP transport uses `auth.mode = "development"`, binds only to loopback, and exposes no arbitrary command action. Production device pairing will replace this with a challenge-response session derived from a stored device secret.

## Reliability rules

- Receivers retain a bounded message-ID deduplication window.
- Commands receive an acknowledgment and exactly one terminal response.
- Retries reuse the original `message_id`; they do not invent a new operation.
- Unknown message types produce `error.unsupported_message`, not a crash.
- Heartbeat loss marks the peer stale before reconnecting with exponential backoff.
- State mutations include an expected revision to prevent lost updates.

## Sensitive operations

An approval request contains a canonical operation description, SHA-256 digest, nonce, deadline, target, and risk class. Approval is valid once, for that digest and session only. Parameter changes invalidate it.
