# Integrations

Integrations expose normalized capabilities rather than vendor objects. No production integration is enabled in Phase 0.

Each adapter must declare supported capabilities, authorization method, scopes, rate limits, webhook/polling behavior, and known API restrictions. Official APIs and documented authentication are mandatory. LinkedIn and Discord personal-account automation must not be inferred beyond their documented APIs.

Initial capability groups are messaging, calendar, tasks, source control, CI, containers, embedded tools, remote systems, and AI providers. Mock providers are required before production credentials.
