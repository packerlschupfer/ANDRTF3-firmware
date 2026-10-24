#!/bin/bash
# Install the bench monitors as systemd user services.
# usage: install.sh UART_DEV MODBUS_DEV [MODBUS_ADDR]
#   use stable names, e.g. /dev/serial/by-id/usb-Silicon_Labs_CP2102_...-if00-port0
# Without lingering the services stop at logout: loginctl enable-linger "$USER"
set -eu
[ $# -ge 2 ] || { sed -n '3,5p' "$0"; exit 1; }
REPO=$(cd "$(dirname "$0")/../.." && pwd)
DST=~/.config/systemd/user
mkdir -p "$DST" "$REPO/logs"
for u in andrtf3-uartlog andrtf3-modbus; do
    sed -e "s#@REPO@#$REPO#g" -e "s#@UART_DEV@#$1#g" -e "s#@MODBUS_DEV@#$2#g" -e "s#@ADDR@#${3:-4}#g" \
        "$REPO/tools/systemd/$u.service" > "$DST/$u.service"
done
systemctl --user daemon-reload
systemctl --user enable --now andrtf3-uartlog.service andrtf3-modbus.service
systemctl --user --no-pager status andrtf3-uartlog.service andrtf3-modbus.service | grep -E '●|Active'
