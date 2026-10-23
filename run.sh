#!/usr/bin/env bash
# One command for the whole dashboard: sets up Python (first run only), starts the app, opens the browser.
#   ./run.sh          Arduino plugged in (close the Arduino IDE Serial Monitor first)
#   ./run.sh --fake   no hardware: fake readings
# Stop with Ctrl+C.
set -e
cd "$(dirname "$0")/dashboard"

[ -d .venv ] || python3 -m venv .venv
if [ requirements.txt -nt .venv/.installed ]; then  # only when requirements change
  .venv/bin/pip install -q -r requirements.txt
  touch .venv/.installed
fi

[ "$1" = "--fake" ] && export FAKE=1
exec .venv/bin/python -u app.py  # -u: show messages immediately, also when logged to a file
