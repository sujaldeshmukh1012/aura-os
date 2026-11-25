# Security and privacy

## Defaults

- Network development services bind to `127.0.0.1`.
- Remote control is disabled.
- Integrations and autonomous external actions are disabled until configured.
- Provider and OAuth secrets remain on the host/backend and use macOS Keychain or runtime secret injection.
- Firmware never stores AI provider keys.
- Audio and sensitive context capture require a visible active indicator and explicit enablement.

## Permission classes

1. Read-only.
2. Reversible local change.
3. External write.
4. Destructive operation.
5. Financial, credential, identity, or otherwise sensitive action.

Classes 3–5 require policy authorization; classes 4–5 require explicit operation-bound approval. Agents cannot change their own tool scope.

## Untrusted input

External messages, websites, files, issue text, build logs, model responses, and tool results are data. Instructions inside them never override user policy or grant tools.

## Planned pairing

- User initiates pairing from both host and device.
- Host displays a short authentication string confirmed on the device.
- A random per-device secret is stored in NVS and Keychain.
- Sessions derive fresh keys using nonces and authenticate each message or protected stream.
- Revocation removes both credentials and invalidates active sessions.

The development TCP mode is not production pairing and must not listen beyond loopback.
