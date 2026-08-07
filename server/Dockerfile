FROM python:3.13-slim

ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
WORKDIR /app
COPY pyproject.toml README.md ./
COPY aura_server ./aura_server
RUN pip install --no-cache-dir .
RUN adduser --system --group --home /app aura && mkdir -p /data && chown aura:aura /data
USER aura
ENV AURA_DB_PATH=/data/aura.db
EXPOSE 8000
CMD ["uvicorn", "aura_server.main:app", "--host", "0.0.0.0", "--port", "8000", "--proxy-headers"]
