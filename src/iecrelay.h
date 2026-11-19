/* iecrelay.h - Relais IEEE-488 -> IEC fuer petSD2 */
#ifndef IECRELAY_H
#define IECRELAY_H

#include <stdint.h>
#include <stdbool.h>

extern uint8_t relay_ieee_unit;
extern uint8_t relay_iec_unit;

void relay_init(void);
bool relay_present(void);              /* Laufwerk am IEC-Bus? */
void relay_open_begin(uint8_t sa);      /* OPEN sa unter ATN */
void relay_data_begin(uint8_t sa);      /* Sekundaeradresse, PET sendet */
void relay_rx(uint8_t b);               /* Byte vom PET */
void relay_unlisten(void);              /* Ende der Listen-Phase */
void relay_close(uint8_t sa);
void relay_prefetch(uint8_t sa);        /* vor TALK-Sekundaerbefehl */
bool relay_peek(uint8_t sa, uint8_t *b, bool *eoi);
void relay_advance(uint8_t sa);

#endif
