#!/bin/bash
# READ-ONLY for flash/EEPROM: dump flash (128 KB), data EEPROM (6 KB) and option bytes, check
# sizes, compare. Up to 5 passes until two are identical for every region. Prints BACKUP OK only then.
# The independent watchdog is frozen while the core is halted (DBGMCU, debug-only register): the
# open firmware runs the IWDG, and a watchdog reset in the middle of a read returns wrong bytes.
# Usage: [LABEL=installed] dump.sh [srst]     (LABEL is appended to the directory name)
set -eu
cd "$(dirname "$0")"
OUT=$(cd .. && pwd)/dumps/$(date +%Y%m%d-%H%M%S)${LABEL:+-$LABEL}; mkdir -p "$OUT"
RST='reset_config none'; [ "${1:-}" = srst ] && RST='reset_config srst_only srst_nogate connect_assert_srst'
REGIONS="flash:131072 eeprom:6144 optbytes:128"      # name:expected size

# region $1, size $2: print a pass that has the full size and an identical twin, if any
twin() {
    local n=$1 want=$2 p q
    for p in "${passes[@]}"; do
        [ "$(stat -c %s "$OUT/${n}_$p.bin" 2>/dev/null || echo 0)" = "$want" ] || continue
        for q in "${passes[@]}"; do
            if [ "$p" != "$q" ] && cmp -s "$OUT/${n}_$p.bin" "$OUT/${n}_$q.bin"; then echo "$p"; return; fi
        done
    done
}

passes=()
ok=0
for pass in 1 2 3 4 5; do
    # RCC_APB2ENR.DBGEN (bit 22), DBGMCU_APB1_FZ.DBG_IWDG_STOP (bit 12)
    openocd -f common.cfg -c "$RST" -c "
init
halt
mmw 0x40021034 0x00400000 0
mmw 0x40015808 0x00001000 0
dump_image $OUT/flash_$pass.bin 0x08000000 0x20000
dump_image $OUT/eeprom_$pass.bin 0x08080000 0x1800
dump_image $OUT/optbytes_$pass.bin 0x1FF80000 0x80
resume
shutdown
" 2>&1 | grep -v "^Info : \(clock\|STLINK\|Target voltage\|\[\)" || true
    passes+=("$pass")
    [ "$pass" -ge 2 ] || continue
    ok=1
    for r in $REGIONS; do
        [ -n "$(twin "${r%%:*}" "${r##*:}")" ] || ok=0
    done
    [ $ok = 1 ] && break
    echo "pass $pass: no two identical full-size reads yet, reading again"
done

cd "$OUT"
if [ $ok = 1 ]; then
    for r in $REGIONS; do
        n=${r%%:*}; good=$(twin "$n" "${r##*:}")
        mv "${n}_$good.bin" "$n.bin"
        echo "$n: two identical reads (pass $good, ${#passes[@]} passes)"
    done
    if ls ./*_[0-9].bin >/dev/null 2>&1; then
        mkdir -p extra_reads && mv ./*_[0-9].bin extra_reads/
        echo "other reads kept in extra_reads/"
    fi
    sha256sum ./*.bin | tee SHA256SUMS
    chmod a-w ./*.bin
    echo "Saved to $(pwd)"
    echo "BACKUP OK"
else
    echo "Saved to $(pwd)"
    echo "!! BACKUP NOT VERIFIED - no two identical full-size reads in ${#passes[@]} passes - do not flash"
    exit 1
fi
