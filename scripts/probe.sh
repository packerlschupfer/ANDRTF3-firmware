#!/bin/bash
# READ-ONLY: chip ID, read-out protection level, option bytes, unique ID.
# Usage: probe.sh [srst]   - "srst" = connect under reset (needs NRST wired)
set -u
cd "$(dirname "$0")"
RST='reset_config none'
[ "${1:-}" = srst ] && RST='reset_config srst_only srst_nogate connect_assert_srst'
openocd -f common.cfg -c "$RST" -c init -c halt \
  -c 'echo "--- DBGMCU_IDCODE (0x40015800) ---"'                    -c 'mdw 0x40015800' \
  -c 'echo "--- FLASH_OPTR (0x4002201C): RDPROT low byte 0xAA=L0 0xCC=L2 other=L1 ---"' -c 'mdw 0x4002201C' \
  -c 'echo "--- FLASH_WRPROT1/2 (0x40022020/0x40022080) ---"'      -c 'mdw 0x40022020' -c 'mdw 0x40022080' \
  -c 'echo "--- Option bytes (0x1FF80000, 8 words) ---"'           -c 'mdw 0x1FF80000 8' \
  -c 'echo "--- Unique ID (0x1FF80050 x2, 0x1FF80064) ---"'         -c 'mdw 0x1FF80050 2' -c 'mdw 0x1FF80064' \
  -c 'echo "--- Flash size KB (0x1FF8007C) ---"'                    -c 'mdh 0x1FF8007C' \
  -c 'echo "--- Vector table (0x08000000) ---"'                     -c 'mdw 0x08000000 8' \
  -c 'echo "--- Data EEPROM first bytes (0x08080000) ---"'          -c 'mdw 0x08080000 8' \
  -c resume -c shutdown 2>&1 | grep -v "^Info : \(The selected\|Listening\|starting gdb\)\|^Licensed\|^For bug\|doxygen\|^$"
