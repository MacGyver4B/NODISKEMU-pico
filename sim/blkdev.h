/* blkdev.h - SD-Karte als Datei oder Laufwerk (512-Byte-Bloecke)
 *
 * Gemeinsam fuer petsdsim und psd2sim. Ein Pfad ist entweder eine
 * Abbilddatei oder ein ganzes Laufwerk (Linux /dev/sdX, Windows
 * \\.\PhysicalDriveN). Die Auswahl und Sicherheitspruefung eines
 * Laufwerks macht petsd-sim.py; hier wird nur gelesen und geschrieben.
 */
#ifndef BLKDEV_H
#define BLKDEV_H

#include <stdint.h>

/* 0 = ok; blocks = Anzahl 512-Byte-Bloecke */
int  blk_open(const char *path, uint64_t *blocks);
/* 0 = ok; bei Fehlern wird einmal je Art gemeldet (prefix: Programmname) */
int  blk_read(uint64_t blk, uint8_t *d);
int  blk_write(uint64_t blk, const uint8_t *d);
void blk_close(void);
void blk_set_prefix(const char *p);

#endif
