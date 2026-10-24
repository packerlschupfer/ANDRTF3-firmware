#ifndef DIP_H
#define DIP_H

#include <stdint.h>

/*
 * Read both DIP switch banks through the 74HC165 chain.
 * Returns (first byte << 8) | second byte, exactly like the original firmware:
 *   high byte = S2 = Modbus address, low byte = S1 = communication settings.
 */
uint16_t dip_read(void);

#endif /* DIP_H */
