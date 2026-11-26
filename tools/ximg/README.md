# X-IMG: Disk-Imager, vom PET gesteuert

Kopiert eine ganze Diskette zwischen einem CBM-Laufwerk am IEEE-488-Bus
(4040, 8050, 8250, SFD-1001) und einem Image (D64/D80/D82) auf der petSD.

Der PET ist Controller: er lädt den Laufwerkscode (`ximgdrv.prg`) per
`M-W` in die Floppy und startet ihn einmal per `M-E`: der Code liest bzw.
schreibt die ganze Diskette in einem Durchgang (Zonentabelle vom PET). Die Daten der Spur
laufen nicht über den PET: die petSD nimmt am schnellen Zwei-Draht-Handshake
des Laufwerkscodes selbst teil (`X-IMG:`-Befehle, siehe `src/ximgdev.h`).

- `ximg4.bas` / `ximg4.prg`: PET-Programm (BASIC 4, petcat-Format)
- `ximgdrv.prg`: Laufwerkscode, erzeugt von `sim/ximgtest/drive4040.py`

Beide Dateien auf die Karte kopieren, `ximg4.prg` vom petSD laden und starten.

Fehler: Die petSD merkt sich je Spur die fehlerhaften Blöcke (`X-IMG:L`).
Nach dem Durchgang zeigt das Programm die Zahl der Spuren mit Fehlern und
bietet an, nur diese Spuren noch einmal zu lesen bzw. zu schreiben. Beim
Backup landen die Job-Ergebnisse in der Fehlertabelle des D64.
Bricht der Durchgang ab (Zeitüberschreitung), hängt der Laufwerkscode:
Floppy aus- und einschalten, das Programm macht ab der Abbruchspur weiter.

Zeiten (VICE, true drive, petSD+ simuliert):

| | Backup | Restore |
|---|---|---|
| 4040 D64 | 11 s | 15 s |
| 8050 D80 | 31 s | 42 s |
| 8250 D82 | 103 s | 126 s |
