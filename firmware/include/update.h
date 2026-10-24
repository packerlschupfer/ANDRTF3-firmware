#ifndef UPDATE_H
#define UPDATE_H

#include <stdint.h>

/*
 * Firmware update over Modbus (holding registers, see regs.h):
 *   540 = 0x55AA  begin        state -> RECEIVING
 *   FC16 at 544, 66 registers: byte offset (2 regs, big endian) + 128 image bytes (64 regs),
 *                 in order from offset 0; repeating the previous chunk is allowed
 *   540 = 0xC0DE  verify       header + CRC32 of the received image -> VERIFIED
 *   540 = 0xB007  activate     boot record trial = new slot, reboot after ~200 ms
 *   540 = 0xAB0B  abort
 * The image must be the build for the INACTIVE slot (input reg 74 = running slot).
 * After a reboot the new firmware runs on trial; it confirms itself after 60 s of normal running
 * and at least one Modbus reply sent. While on trial, begin and activate are refused (0x8106).
 * If it fails to confirm within 3 boots, the bootloader goes back to the previous slot.
 */
#define UPD_CMD_BEGIN           0x55AAU
#define UPD_CMD_VERIFY          0xC0DEU
#define UPD_CMD_ACTIVATE        0xB007U
#define UPD_CMD_ABORT           0xAB0BU

#define UPD_IDLE                0U
#define UPD_RECEIVING           1U
#define UPD_VERIFIED            2U
#define UPD_ACTIVATING          3U
#define UPD_ERR_SEQUENCE        0x8101U
#define UPD_ERR_OFFSET          0x8102U
#define UPD_ERR_FLASH           0x8103U
#define UPD_ERR_IMAGE           0x8104U     /* header, slot address, size or CRC wrong */
#define UPD_ERR_BAD_COMMAND     0x8105U
#define UPD_ERR_ON_TRIAL        0x8106U     /* running on trial: wait for the confirmation first */

void update_command(uint16_t cmd, uint32_t now_ms);
int update_write_chunk(const volatile uint8_t *data, uint16_t nregs);   /* Modbus exception or 0 */
void update_poll(uint32_t now_ms);
uint16_t update_state(void);
uint32_t update_received(void);
void update_boot_info(uint16_t *slot, uint16_t *rec, uint16_t *tries, uint16_t *boot_ver);

#endif /* UPDATE_H */
