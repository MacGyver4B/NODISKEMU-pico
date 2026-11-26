/* NODISKEMU - disk imager, device side (PET-directed X-IMG)
 *
 * The PET is the bus controller: it sends the drive code to the CBM floppy
 * (M-W) and starts it per track (M-E). The data of that track does not go
 * through the PET: petSD takes part in the fast two-wire handshake of the
 * drive code itself, as receiver (backup) or sender (restore), and works on
 * an image file on the card. The handshake needs no ATN, so petSD stays a
 * device (on the petSD+ DC of the 75161 is tied, it cannot drive ATN).
 *
 * Commands on the command channel of petSD:
 *   X-IMG:B,<type>,<name>   backup: create image <name> (type 64/80/82),
 *                           @<name> replaces an existing one
 *   X-IMG:R,<type>,<name>   restore: open image <name> (type 0: by size)
 *   X-IMG:T,<unit>,<track>  arm: the next M-E to <unit> starts the data
 *                           phase for <track> (after its UNLISTEN)
 *   X-IMG:A,<unit>[,<first>[,<last>]]
 *                           arm for a whole pass: one M-E, the drive code
 *                           (AREAD/AWRITE) runs tracks <first>..<last>
 *                           (default: the whole disk) without a break
 *   X-IMG:L[,<from>]        next track >= <from> with errors since B/R:
 *                           00, OK,<track>,<bad blocks> (255 = track failed),
 *                           00, OK,00,00 when there is none
 *   X-IMG:E                 end: close the image (backup: size, error table)
 * After the data phase petSD pulls EOI low until the next ATN; the PET waits
 * for that, then reads the status: 00, OK,<last track>,<bad blocks>, or
 * 74,DRIVE NOT READY,<track>,<bad blocks> when the pass stopped at <track>
 * (timeout: the drive code hangs then, the floppy needs a reset).
 */
#ifndef XIMGDEV_H
#define XIMGDEV_H

#include <stdint.h>
#include <stdbool.h>

extern uint8_t ximg_armed_dev;          /* floppy unit, 0 = not armed */
extern bool    ximg_go;                 /* its M-E was seen */

/* "X-IMG:..." from the command channel (command_buffer, command_length) */
void ximg_command(void);

/* data phase (ieee.c) */
uint8_t  ximg_dir(void);                /* 'R' floppy -> petSD, 'W' petSD -> floppy */
uint8_t  ximg_first(void);              /* armed track range */
uint8_t  ximg_last(void);
void     ximg_set_track(uint8_t t);     /* track of the data phase */
void     ximg_track_done(bool ok, uint8_t bad);
uint8_t  ximg_nsec(void);               /* sectors of that track */
uint8_t *ximg_block(void);              /* 256 byte buffer */
bool     ximg_put_block(uint8_t sector, uint8_t result);  /* backup: buffer -> image */
bool     ximg_get_block(uint8_t sector, uint8_t sum[2]);  /* restore: image -> buffer */
void     ximg_finish(bool ok, uint16_t bad);

#endif
