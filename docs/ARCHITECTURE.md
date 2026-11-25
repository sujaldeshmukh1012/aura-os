# Architecture

## Product boundary

NEXUS has four layers connected through a versioned domain protocol:

1. **Device** — ESP32-S3 firmware owns physical input, local rendering, cached state, audio capture, and host transport.
2. **Host** — a local background service observes authorized macOS state and performs approved local actions.
3. **Backend** — optional local-first intelligence, memory, integrations, agents, and workflow persistence.
4. **Configuration** — a transient dashboard or native shell for setup, diagnostics, privacy, and permissions.

The commercial USB-C hub is a separate subsystem. NEXUS firmware neither implements nor controls high-speed hub switching.

## Foundation data flow

```text
macOS telemetry adapters
        |
        v
  host state reducer ----> framed NEXUS envelopes ----> device or simulator
        ^                         |
        |                         v
 authorized actions <---- command/response + operation-bound approval
```

The host core is portable Python with no third-party runtime dependency. The macOS shell is Swift/AppKit. Platform adapters may be rewritten natively without changing the protocol.

## State model

- Producers emit typed events.
- A host-side reducer creates authoritative snapshots.
- Device and simulator maintain bounded read models, not independent business databases.
- Each snapshot has an observation timestamp and source freshness.
- Reconnection starts with capability negotiation followed by full state reconciliation.
- Every mutation is correlated to one request and one audit record.

## Trust boundaries

- Device-to-host pairing identity.
- Host action broker and macOS permissions.
- Backend secrets and provider credentials.
- External integration content, which is always untrusted.
- Physical approval, bound to an operation digest, expiration, and nonce.

The current TCP development transport binds to loopback and does not expose command execution. Production LAN WebSocket support requires authenticated pairing before it may be enabled.

## Initial modules

- `firmware/components/board`: ES3C40P capabilities and pins.
- `firmware/components/transport`: framing and native USB serial/JTAG I/O.
- `host/core/nexus_core/protocol.py`: canonical envelope and decoder.
- `host/core/nexus_core/telemetry.py`: real macOS/system telemetry.
- `host/core/nexus_core/service.py`: session handshake and snapshot publisher.
- `simulator/nexus_simulator`: protocol client and 480×320 device view.

See `docs/adr/0001-foundation-boundaries.md` for the rationale.
