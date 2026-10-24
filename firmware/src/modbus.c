/*
 * modbus.c - minimal Modbus RTU slave (FC03, FC04, FC06, FC16)
 *
 * Frame end is detected by the USART receiver timeout (3.5 character times,
 * fixed 1.75 ms above 19200 baud). The ISR only collects bytes; frames are
 * handled and answered from the main loop.
 */
#include "modbus.h"
#include "board.h"
#include "regs.h"
#include "debug.h"
#include "update.h"

#define FRAME_MAX       256U

#define EX_ILLEGAL_FUNCTION 1
#define EX_ILLEGAL_ADDRESS  2
#define EX_ILLEGAL_VALUE    3

static uint8_t s_addr;
static uint32_t s_baud;
static char s_parity;
static uint8_t s_stop;

static volatile uint8_t s_rx[FRAME_MAX];
static volatile uint16_t s_rx_len;
static volatile uint8_t s_frame_ready;
static uint8_t s_tx[FRAME_MAX];

static volatile modbus_stats_t s_st;

static uint16_t crc16(const volatile uint8_t *p, uint16_t n)
{
    uint16_t crc = 0xFFFF;

    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

void USART1_IRQHandler(void)
{
    uint32_t isr = USART1->ISR;

    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_PE | USART_ISR_NE)) {
        if (isr & USART_ISR_FE) {
            s_st.framing++;
        }
        if (isr & USART_ISR_PE) {
            s_st.parity++;
        }
        if (isr & USART_ISR_NE) {
            s_st.noise++;
        }
        if (isr & USART_ISR_ORE) {
            s_st.overrun++;
        }
        USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_PECF | USART_ICR_NCF;
        if (!s_frame_ready) {
            s_rx_len = FRAME_MAX;           /* poison the frame: dropped at the CRC check */
        }
    }
    if (isr & USART_ISR_RXNE) {
        uint8_t b = (uint8_t)USART1->RDR;
        if (!s_frame_ready && s_rx_len < FRAME_MAX) {
            s_rx[s_rx_len++] = b;
            pin_set(ACT_RX_PORT, ACT_RX_PIN);
        }
    }
    if (isr & USART_ISR_RTOF) {
        USART1->ICR = USART_ICR_RTOCF;
        if (s_rx_len > 0 && !s_frame_ready) {
            s_frame_ready = 1;
        }
    }
}

void modbus_init(uint8_t address, uint8_t cfg)
{
    uint32_t cr1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_RTOIE;
    uint32_t parity_sel = (((cfg >> 3) & 3U) + 2U) & 3U;   /* same mapping as the original */
    uint32_t rto_bits;

    s_addr = address;
    s_baud = ((cfg & 0xC0U) == 0xC0U) ? 57600U : (9600U << (cfg >> 6));
    s_stop = (cfg & 0x02U) ? 2U : 1U;
    if (parity_sel == 2U) {
        s_parity = 'E';
        cr1 |= USART_CR1_M0 | USART_CR1_PCE;
    } else if (parity_sel == 3U) {
        s_parity = 'O';
        cr1 |= USART_CR1_M0 | USART_CR1_PCE | USART_CR1_PS;
    } else {
        s_parity = 'N';
    }
    if (cfg & 0x04U) {
        dbg_puts("WARN: DIP selects Modbus ASCII - not supported, using RTU\n");
    }

    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    board_gpio_mode(RS485_PORT, RS485_TX_PIN, GPIO_AF, 0, PULL_UP);
    board_gpio_mode(RS485_PORT, RS485_RX_PIN, GPIO_AF, 0, PULL_UP);
    board_gpio_mode(RS485_PORT, RS485_DE_PIN, GPIO_AF, 5, PULL_DOWN);

    USART1->CR1 = 0;
    USART1->BRR = SYSCLK_HZ / s_baud;
    rto_bits = (s_baud > 19200U) ? (s_baud * 175U) / 100000U : 35U + 4U;  /* 3.5 chars, 11-bit frames */
    USART1->RTOR = rto_bits;
    USART1->CR2 = USART_CR2_RTOEN | ((s_stop == 2U) ? USART_CR2_STOP_1 : 0U);
    USART1->CR3 = USART_CR3_DEM | USART_CR3_OVRDIS;           /* DE active high on PB3 */
    cr1 |= (8U << USART_CR1_DEAT_Pos) | (8U << USART_CR1_DEDT_Pos);
    USART1->CR1 = cr1;
    USART1->CR1 |= USART_CR1_UE;

    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);
}

uint32_t modbus_baud(void) { return s_baud; }
void modbus_get_stats(modbus_stats_t *st) { *st = s_st; }
char modbus_parity(void) { return s_parity; }
uint8_t modbus_stopbits(void) { return s_stop; }

static void send(uint16_t len)
{
    uint16_t crc = crc16(s_tx, len);

    s_tx[len++] = (uint8_t)(crc & 0xFFU);
    s_tx[len++] = (uint8_t)(crc >> 8);

    pin_set(ACT_TX_PORT, ACT_TX_PIN);
    for (uint16_t i = 0; i < len; i++) {
        while (!(USART1->ISR & USART_ISR_TXE)) {
        }
        USART1->TDR = s_tx[i];
    }
    while (!(USART1->ISR & USART_ISR_TC)) {
    }
    pin_clr(ACT_TX_PORT, ACT_TX_PIN);
}

static uint16_t exception(uint8_t fc, uint8_t code)
{
    s_st.exceptions++;
    s_tx[1] = (uint8_t)(fc | 0x80U);
    s_tx[2] = code;
    return 3;
}

/* len = PDU incl. address byte, excluding the CRC */
static uint16_t handle(const volatile uint8_t *rx, uint16_t len, uint32_t now_ms)
{
    uint8_t fc = rx[1];
    uint16_t addr = (uint16_t)((rx[2] << 8) | rx[3]);
    uint16_t n = (uint16_t)((rx[4] << 8) | rx[5]);
    uint16_t val;
    int ex;

    s_tx[0] = s_addr;
    s_tx[1] = fc;

    switch (fc) {
    case 0x03:
    case 0x04:
        if (len != 6 || n < 1 || n > 125) {
            return exception(fc, EX_ILLEGAL_VALUE);
        }
        s_tx[2] = (uint8_t)(n * 2U);
        for (uint16_t i = 0; i < n; i++) {
            ex = (fc == 0x04) ? regs_read_input((uint16_t)(addr + i), &val, now_ms)
                              : regs_read_holding((uint16_t)(addr + i), &val);
            if (ex) {
                return exception(fc, (uint8_t)ex);
            }
            s_tx[3 + 2 * i] = (uint8_t)(val >> 8);
            s_tx[4 + 2 * i] = (uint8_t)val;
        }
        return (uint16_t)(3U + n * 2U);

    case 0x06:
        if (len != 6) {
            return exception(fc, EX_ILLEGAL_VALUE);
        }
        ex = regs_write_holding(addr, n);
        if (ex) {
            return exception(fc, (uint8_t)ex);
        }
        for (int i = 2; i < 6; i++) {
            s_tx[i] = rx[i];
        }
        return 6;

    case 0x10:
        if (len < 7 || n < 1 || n > 123 || rx[6] != n * 2U || len != 7U + n * 2U) {
            return exception(fc, EX_ILLEGAL_VALUE);
        }
        if (addr == REG_UPD_DATA) {             /* firmware update chunk, see update.h */
            ex = update_write_chunk(&rx[7], n);
            if (ex) {
                return exception(fc, (uint8_t)ex);
            }
            for (int i = 2; i < 6; i++) {
                s_tx[i] = rx[i];
            }
            return 6;
        }
        for (uint16_t i = 0; i < n; i++) {     /* validate the whole range first */
            if (regs_read_holding((uint16_t)(addr + i), &val)) {
                return exception(fc, EX_ILLEGAL_ADDRESS);
            }
        }
        for (uint16_t i = 0; i < n; i++) {     /* values are checked per register: on a rejected
                                                  value the registers before it are already written */
            ex = regs_write_holding((uint16_t)(addr + i), (uint16_t)((rx[7 + 2 * i] << 8) | rx[8 + 2 * i]));
            if (ex) {
                return exception(fc, (uint8_t)ex);
            }
        }
        for (int i = 2; i < 6; i++) {
            s_tx[i] = rx[i];
        }
        return 6;

    default:
        return exception(fc, EX_ILLEGAL_FUNCTION);
    }
}

void modbus_poll(uint32_t now_ms)
{
    uint16_t len, out;
    uint8_t dest;

    if (!s_frame_ready) {
        return;
    }
    len = s_rx_len;
    dest = s_rx[0];
    s_st.frames++;

    if (len < 4 || len >= FRAME_MAX) {
        s_st.bad_length++;              /* also frames poisoned by a UART error */
    } else if (crc16(s_rx, len) != 0) {
        s_st.crc++;
    } else if ((dest == s_addr || dest == 0) && s_addr >= 1 && s_addr <= 247) {
        s_st.handled++;
        out = handle(s_rx, (uint16_t)(len - 2U), now_ms);
        if (dest != 0) {                /* no reply to broadcasts */
            send(out);
            s_st.replies++;
        }
    }

    pin_clr(ACT_RX_PORT, ACT_RX_PIN);
    s_rx_len = 0;
    s_frame_ready = 0;
}
