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
| ST7796S display + LVGL | P0 | Hardware-active | Visual signoff | `firmware/components/display`, `ui` | Firmware build + physical runtime | Vendor sequence, 40 MHz SPI, backlight, and LVGL UI are active; visually confirm RGB order and 480×320 rotation. |
| FT6336U touch | P0 | Hardware-validated | Final transform calibration | `firmware/components/touch` | Firmware build + physical runtime | Chip ID `0x64` and live press coordinates passed; edge/corner transform accuracy still needs signoff. |
| macOS menu-bar shell | P1 | Scaffolded | AppKit | `host/macos` | Build | Not signed/packaged; must show live service/device status and permission diagnostics. |
| Secure pairing | P1 | Designed | Keychain, NVS, crypto review | `docs/SECURITY.md` | None | Development auth is not acceptable for LAN/production. |
| Workspace manager | P1 | Planned | Persistence, action broker | `host/` | None | Activation must be inspectable and never close processes unless configured. |
| Attention engine | P2 | Planned | Integrations, context | `backend/` | None | Deterministic rules must prevent AI-only silent suppression. |
| Physical approval | P2 | Designed | Secure session, hardware input | Protocol/backend/device | None | Must bind digest, nonce, session, expiry, and single use. |
| AI/memory/agents | P3 | Planned | Backend persistence and permissions | `backend/` | None | Provider-independent; no keys on device. |

## Immediate next actions

1. Visually confirm display RGB order/orientation and tap all four corners to sign off the touch transform.
2. Obtain front/back board photographs and record the physical PCB revision.
3. Implement the macOS serial transport against the hardware-validated USB framing.
4. Share a single UI state model between LVGL and simulator fixtures.
