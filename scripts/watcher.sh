#!/usr/bin/env bash
# Simple watcher script (bash) for *nix or Git Bash on Windows
# Assumptions: `scp`, `ssh`, `eniq_parser` available on PATH.

POLL_INTERVAL=30
SFTP_HOST="sftp.example.com"
SFTP_USER="user"
REMOTE_PATH="/remote/incoming"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INGEST_DIR="$REPO_ROOT/ingest"
INCOMING_DIR="$INGEST_DIR/incoming"
PROCESSING_DIR="$INGEST_DIR/processing"
ARCHIVE_DIR="$INGEST_DIR/archive"
ERROR_DIR="$INGEST_DIR/errors"

PARSER="$REPO_ROOT/build/Release/eniq_parser.exe"
LOADER_CMD_TEMPLATE="echo LOADER: import {csv} into {db}"
DB_PATH="$REPO_ROOT/eniq_data.db"

mkdir -p "$INCOMING_DIR" "$PROCESSING_DIR" "$ARCHIVE_DIR" "$ERROR_DIR"

fetch_remote_files() {
  echo "Fetching remote files..."
  scp -q "$SFTP_USER@$SFTP_HOST:$REMOTE_PATH/*.xml" "$INCOMING_DIR/" 2>/dev/null || true
  scp -q "$SFTP_USER@$SFTP_HOST:$REMOTE_PATH/*.gz" "$INCOMING_DIR/" 2>/dev/null || true
  ssh "$SFTP_USER@$SFTP_HOST" "mkdir -p '$REMOTE_PATH/processed' ; mv $REMOTE_PATH/*.xml $REMOTE_PATH/processed/ 2>/dev/null || true ; mv $REMOTE_PATH/*.gz $REMOTE_PATH/processed/ 2>/dev/null || true" || true
}

process_files() {
  for f in "$INCOMING_DIR"/*; do
    [ -e "$f" ] || continue
    bn=$(basename "$f")
    mv -f "$f" "$PROCESSING_DIR/"
    pf="$PROCESSING_DIR/$bn"
    csv="$PROCESSING_DIR/${bn%.*}.csv"
    echo "Parsing $pf -> $csv"
    "$PARSER" --in "$pf" --out-csv "$csv"
    if [ $? -ne 0 ]; then
      echo "Parser failed for $pf"
      mv -f "$pf" "$ERROR_DIR/"
      continue
    fi
    loader_cmd=${LOADER_CMD_TEMPLATE//\{csv\}/$csv}
    loader_cmd=${loader_cmd//\{db\}/$DB_PATH}
    echo "Running loader: $loader_cmd"
    eval "$loader_cmd"
    if [ $? -ne 0 ]; then
      echo "Loader failed for $csv"
      mv -f "$pf" "$ERROR_DIR/"
      continue
    fi
    mv -f "$pf" "$ARCHIVE_DIR/"
    mv -f "$csv" "$ARCHIVE_DIR/"
    echo "Archived $bn"
  done
}

echo "Watcher started. Polling every $POLL_INTERVAL seconds."
while true; do
  fetch_remote_files
  process_files
  sleep $POLL_INTERVAL
done
