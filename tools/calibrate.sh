#!/bin/bash
# Reference-resistor calibration of the ANDRTF3 open firmware (parallel method).
# Usage: calibrate.sh [modbus-address] [device]
# Needs: open firmware, mbpoll, R_cal (default 1000.0 Ohm, holding 523).
set -u
ADDR=${1:-4}
DEV=${2:-/dev/ttyUSB485}
MB="mbpoll -1 -q -m rtu -a $ADDR -b 9600 -P even -0"

rd()  { timeout 5 $MB -t "$1" -r "$2" -c 1 "$DEV" 2>/dev/null | grep -o "]:[[:space:]]*[0-9]*" | grep -o "[0-9]*$" | head -1; }
wr()  { timeout 5 mbpoll -1 -q -m rtu -a "$ADDR" -b 9600 -P even -0 -t 4 -r "$1" "$DEV" "$2" >/dev/null 2>&1; }
state() { rd 4 531; }
wait_capture() {
    for _ in $(seq 40); do
        s=$(state)
        case "$s" in
            3) echo "  captured"; return 0 ;;
            1|2) sleep 0.5 ;;
            *) echo "  unexpected state: $s"; return 1 ;;
        esac
    done
    echo "  timeout"; return 1
}
rref() { echo $(( $(rd 4 520) * 65536 + ( $(rd 4 521) & 0xFFFF ) )); }

ver=$(rd 3 2)
[ $(( ${ver:-0} & 0x8000 )) -ne 0 ] || { echo "open firmware not found (reg 2 = ${ver:-none})"; exit 1; }
echo "Current R_ref: $(rref) mOhm, R_cal: $(( $(rd 4 523) / 10 )).$(( $(rd 4 523) % 10 )) Ohm"
echo
read -r -p "1/4  PT1000 alone (nothing clipped on). Press Enter to capture A ... "
wr 530 4; wr 530 1; wait_capture || exit 1
read -r -p "2/4  Clip the calibration resistor across the PT1000 terminals, then Enter ... "
wr 530 2; wait_capture || exit 1
read -r -p "3/4  Remove the resistor, then Enter to capture A again ... "
wr 530 1; wait_capture || exit 1
wr 530 3
s=$(state)
if [ "$s" != 4 ]; then echo "compute failed, state $s (0x8003 = implausible result)"; exit 1; fi
new=$(rref)
echo "New R_ref: $new mOhm ($(awk -v r="$new" 'BEGIN{printf "%+.3f", (r-10000000)/1e5}') % from nominal)"
echo "PT1000 now reads $(( $(rd 3 52) / 10 )).$(( $(rd 3 52) % 10 )) Ohm, $(rd 3 57) x 0.01 degC"
echo
read -r -p "4/4  Save to EEPROM? [y/N] " yn
if [ "$yn" = y ]; then
    wr 530 $((0x5AFE))
    [ "$(state)" = 5 ] && echo "saved" || echo "save FAILED (state $(state))"
else
    echo "not saved - active until reset (write 530 = 0x10AD to reload the stored values)"
fi
