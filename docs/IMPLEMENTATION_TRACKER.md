# Implementation tracker

Updated: 2026-11-24

| Feature | Priority | Status | Dependencies | Location | Test coverage | Known limitation / acceptance criterion |
| --- | --- | --- | --- | --- | --- | --- |
| Protocol envelope and framing | P0 | Implemented | None | `protocol/`, `host/core/nexus_core/protocol.py` | Unit | Authentication is development-only; fragmented and coalesced frames must decode deterministically. |
| Real host telemetry | P0 | Implemented | macOS/portable OS APIs | `host/core/nexus_core/telemetry.py` | Unit | CPU is load-derived; acceptance is fresh CPU/memory/disk/battery/repo snapshot without third-party packages. |
| Loopback host service | P0 | Implemented | Protocol | `host/core/nexus_core/service.py` | Unit + E2E | Loopback only; must reject pre-handshake messages and publish snapshots. |
| Device simulator | P0 | Implemented | Host service | `simulator/` | Unit + E2E | Browser rendering is simulator-only; must represent 480×320 and reconnect after host restart. |
| ES3C40P BSP record | P0 | Implemented | Manufacturer docs | `firmware/components/board`, `docs/HARDWARE.md` | Build | Physical PCB revision unverified; acceptance requires checklist completion on owned board. |
| Native USB framed transport | P0 | Hardware-validated | ESP-IDF, board USB path | `firmware/components/transport` | Firmware build + physical E2E | Physical Type-C welcome/ping/pong and consecutive status frames passed without resets; production macOS serial adapter remains. |
| ST7796S display + LVGL | P0 | Blocked on hardware verification | Board revision/demo init | `firmware/components/display`, `ui` | None | Must render correct 480×320 landscape colors on physical board. |
| FT6336U touch | P0 | Blocked on hardware verification | I²C/display rotation | `firmware/components/touch` | None | Must pass edge/corner transform test in landscape. |
| macOS menu-bar shell | P1 | Scaffolded | AppKit | `host/macos` | Build | Not signed/packaged; must show live service/device status and permission diagnostics. |
| Secure pairing | P1 | Designed | Keychain, NVS, crypto review | `docs/SECURITY.md` | None | Development auth is not acceptable for LAN/production. |
| Workspace manager | P1 | Planned | Persistence, action broker | `host/` | None | Activation must be inspectable and never close processes unless configured. |
| Attention engine | P2 | Planned | Integrations, context | `backend/` | None | Deterministic rules must prevent AI-only silent suppression. |
| Physical approval | P2 | Designed | Secure session, hardware input | Protocol/backend/device | None | Must bind digest, nonce, session, expiry, and single use. |
| AI/memory/agents | P3 | Planned | Backend persistence and permissions | `backend/` | None | Provider-independent; no keys on device. |

## Immediate next actions

1. Obtain front/back board photographs and record the physical PCB revision.
2. Import and audit the manufacturer's ESP-IDF ST7796S/FT6336U initialization sequence.
3. Implement the macOS serial transport against the hardware-validated USB framing.
4. Share a single UI state model between LVGL and simulator fixtures.
