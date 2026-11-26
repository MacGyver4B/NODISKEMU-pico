# X-IMG: Disk-Imager, vom PET gesteuert

Kopiert eine ganze Diskette zwischen einem CBM-Laufwerk am IEEE-488-Bus
(4040, 8050, 8250, SFD-1001) und einem Image (D64/D80/D82) auf der petSD.

Der PET ist Controller: er lädt den Laufwerkscode (DATA-Zeilen ab 2000 in
`ximg4.bas`) per
`M-W` in die Floppy und startet ihn einmal per `M-E`: der Code liest bzw.
schreibt die ganze Diskette in einem Durchgang (Zonentabelle vom PET). Die Daten der Spur
laufen nicht über den PET: die petSD nimmt am schnellen Zwei-Draht-Handshake
des Laufwerkscodes selbst teil (`X-IMG:`-Befehle, siehe `src/ximgdev.h`).

- `ximg4.bas` / `ximg4.prg`: PET-Programm (BASIC 4, petcat-Format)

`ximg4.prg` ist alles, was gebraucht wird: auf die Karte oder eine Diskette
kopieren (auch per LOAD/SAVE), laden und starten.

Laufwerkscode und PET-Routinen stehen als DATA-Zeilen im Programm: die
Routinen dezimal in den Zeilen 21-29, der Laufwerkscode hexadezimal als
`DATA#...` ab Zeile 2000. Eine Maschinencode-Routine ($0282) liest die
`DATA#`-Zeilen direkt aus dem Programmtext und schickt sie per M-W (1 s).
Nach einer Aenderung am Laufwerkscode (`sim/ximgtest/drive4040.py`) die
Zeilen mit `sim/ximgtest/petupload.py` neu erzeugen.

Einstellungen in Zeile 45: `db=0` schaltet die Schrittanzeige ab, `hz=60`
fuer einen 60-Hz-PET.

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
