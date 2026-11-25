# Foundation message types

| Type | Direction | Purpose |
| --- | --- | --- |
| `session.hello` | client → host | Identity, versions, and capabilities. |
| `session.welcome` | host → client | Version selection and session parameters. |
| `heartbeat.ping` | either | Liveness request. |
| `heartbeat.pong` | either | Correlated liveness response. |
| `device.status` | device → host | Device, display, touch, UI, heap, and uptime health. |
| `telemetry.snapshot` | host → client | Fresh system and repository telemetry. |
| `state.snapshot` | host → client | Full reconciled device-facing state. |
| `error.unsupported_message` | either | Explicit unknown-type response. |

Message types are additive within protocol 1.x. A peer must ignore unknown payload fields but must reject unknown required envelope fields.
