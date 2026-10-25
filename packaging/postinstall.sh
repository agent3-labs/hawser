#!/bin/sh
# Runs after the package is installed or upgraded. The service is not started:
# nothing can run until `hawser init` or `hawser join` has set the machine up,
# and the unit's ConditionPathExists says so. An upgrade leaves a running
# service alone; the interface stays up, and the next restart picks up the
# new binary.
set -e
if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload || true
fi
echo "hawser installed. Next: 'hawser init ...' on the coordinator, or 'hawser join <invite>' on any other machine."
echo "Then: systemctl enable --now hawser"
