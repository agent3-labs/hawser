#!/bin/sh
# Runs after the package is removed. The machine's own files, /etc/hawser and
# /var/lib/hawser, are left in place: the coordinator's machines file is the
# network, and removing a package should not destroy it.
set -e
if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload || true
fi
