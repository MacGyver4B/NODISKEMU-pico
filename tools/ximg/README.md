# X-IMG: Disk-Imager, vom PET gesteuert

Kopiert eine ganze Diskette zwischen einem CBM-Laufwerk am IEEE-488-Bus
(4040, 8050, 8250, SFD-1001) und einem Image (D64/D80/D82) auf der petSD.

Der PET ist Controller: er lädt den Laufwerkscode (`ximgdrv.prg`) per
`M-W` in die Floppy und startet ihn pro Spur per `M-E`. Die Daten der Spur
laufen nicht über den PET: die petSD nimmt am schnellen Zwei-Draht-Handshake
des Laufwerkscodes selbst teil (`X-IMG:`-Befehle, siehe `src/ximgdev.h`).

- `ximg3.bas` / `ximg3.prg`: PET-Programm (BASIC 4, petcat-Format)
- `ximgdrv.prg`: Laufwerkscode, erzeugt von `sim/ximgtest/drive4040.py`

Beide Dateien auf die Karte kopieren, `ximg3.prg` vom petSD laden und starten.
