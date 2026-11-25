# Intelligence backend

This boundary will contain the optional local-first FastAPI, PostgreSQL/pgvector, SQLAlchemy, and Alembic services once persistence-backed memory, integrations, and agents enter scope.

Phase 0 keeps it dependency-free and does not start an unauthenticated API. The action permission and integration capability contracts will be implemented before external integrations.
