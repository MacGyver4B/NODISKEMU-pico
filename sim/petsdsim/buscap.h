/* buscap.h - automatische VCD-Mitschnitte des IEEE-488-Busses (GTKWave) */
#ifndef BUSCAP_H
#define BUSCAP_H

#include <stdint.h>

/* Mitschnitte in Verzeichnis dir; Ende nach idle_ms ohne Flanke (0: 250 ms) */
void buscap_init(const char *dir, double idle_ms);
/* Zustand zum Zeitpunkt t (ps); pull/data wie simlink (gesetzt = low) */
void buscap_state(uint64_t t, uint8_t pet_pull, uint8_t pet_data, uint8_t avr_pull, uint8_t avr_data,
                  int te, int sd_cs, int isr);
/* regelmaessig aufrufen: beendet einen Mitschnitt nach der Ruhezeit */
void buscap_poll(uint64_t t);
void buscap_close(void);

#endif
