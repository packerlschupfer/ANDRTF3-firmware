#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

/* Debug text on USART5 TX (PC12), 115200 8N1. Blocking, TX only. */
void dbg_init(void);
void dbg_puts(const char *s);
void dbg_put_int(int32_t v);
void dbg_put_hex(uint32_t v, int digits);


#endif /* DEBUG_H */
