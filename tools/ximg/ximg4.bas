10 rem disk-imager v4: pet steuert, petsd fuehrt die datenphase, ganze diskette am stueck
20 fori=0to7:readx:poke634+i,x:next:data173,16,232,41,64,208,249,96
30 print"{clr}disk-imager v4":input"(b)ackup/(r)estore";m$
40 input"floppy-geraet";fu:input"petsd-geraet";su:input"image";n$
50 open15,fu,15,"i0":open14,su,15
60 print#15,"m-r"chr$(252)chr$(255)chr$(2):get#15,a$,b$
70 a=asc(a$+chr$(0)):b=asc(b$+chr$(0)):dv=0
80 ifa=43andb=211thendv=21
90 ifa=183andb=242thendv=27
100 ifdv=0thenprint"laufwerk nicht unterstuetzt":goto900
110 open2,fu,2,"#0":ad=6:ifm$="r"thenad=9
120 ifm$="r"then300
200 gosub500:ty=dt:ifty=0thenprint"keine cbm-diskette":goto900
210 print#14,"x-img:b,"ty","n$:gosub850:ife then900
220 gosub600:gosub400:gosub450:print#14,"x-img:e":gosub850
230 print"fertig "ti$" fehler"bt:goto900
300 ty=val(right$(n$,2)):print#14,"x-img:r,"ty","n$:gosub850:ife then900
310 gosub500:ifdt<>tythengosub950
320 gosub600:gosub400:gosub450:print#15,"i0":gosub800:print#14,"x-img:e":gosub850
330 print"fertig "ti$" fehler"bt:goto900
400 nt=35:ifty=80thennt=77
410 ifty=82thennt=154
420 ti$="000000":ft=1:lt=nt
430 gosub700:ife=0thenreturn
435 print"abbruch bei spur"et:print"floppy aus- und einschalten, dann taste"
440 geta$:ifa$=""then440
445 close2:close15:open15,fu,15,"i0":open2,fu,2,"#0":ft=et:gosub600:goto430
450 bt=0:nb=0:f=1
455 print#14,"x-img:l,"f:input#14,e,e$,t,es:ift=0then470
460 nb=nb+1:bt=bt+es:f=t+1:iff<=ntthen455
470 ifnb=0thenreturn
475 printnb"spur(en) mit fehlern":input"nochmal lesen/schreiben (j/n)";a$:ifa$<>"j"thenreturn
480 f=1
485 print#14,"x-img:l,"f:input#14,e,e$,t,es:ift=0then450
490 ft=t:lt=t:gosub700:ifethenprint"abbruch bei spur"et:goto900
495 f=t+1:iff<=ntthen485
499 goto450
500 dt=0:print#15,"u1:2 0 39 0":gosub800:ifethen530
510 print#15,"b-p:2,2":get#2,a$:ifa$<>chr$(67)then530
520 print#15,"u1:2 0 38 3":gosub800:print#15,"b-p:2,5":get#2,a$:dt=82:ifasc(a$+chr$(0))=78thendt=80
525 return
530 print#15,"u1:2 0 18 0":gosub800:ifethenreturn
540 print#15,"b-p:2,2":get#2,a$:ifa$=chr$(65)thendt=64
550 return
600 open3,su,3,"ximgdrv.prg,p,r":get#3,a$,a$:a=8192
620 s$="":z=0:fori=1to32:get#3,a$:s$=s$+chr$(asc(a$+chr$(0))):z=st:ifztheni=33
630 next:print#15,"m-w"chr$(aand255)chr$(int(a/256))chr$(len(s$))s$:a=a+len(s$):ifz=0then620
640 close3:p$=chr$(0)+chr$(0)+chr$(0)+chr$(7)+chr$(1)+chr$(2)+chr$(7)+chr$(8)+chr$(9)+chr$(10)+chr$(11)
650 p$=p$+chr$(0)+chr$(0)+chr$(0)+chr$(18)+chr$(19)+chr$(48)+chr$(49)+chr$(50)+chr$(51)+chr$(64)
660 p$=p$+chr$(0)+chr$(0)+chr$(0)+chr$(0):print#15,"m-w"chr$(67)chr$(35)chr$(25)p$
670 o$="":fori=0to28:o$=o$+chr$(i):next:print#15,"m-w"chr$(122)chr$(35)chr$(29)o$
680 restore:fori=0to7:readx:next:z$="":fori=1to16:readx:z$=z$+chr$(x):next
685 ifty=64thenfori=1to8:readx:z$=left$(z$,i-1)+chr$(x)+mid$(z$,i+1):next
690 print#15,"m-w"chr$(240)chr$(35)chr$(16)z$:return
695 data40,29,54,27,65,25,78,23,117,29,131,27,142,25,155,23
696 data18,21,25,19,31,18,36,17
700 print#14,"x-img:a,"fu","ft","lt:gosub850:ifethen900
710 print#15,"m-w"chr$(238)chr$(35)chr$(2)chr$(ft)chr$(lt+1)
720 print"{home}{down}{down}{down}{down}{down}{down}spur"mid$(str$(ft),2)" bis"str$(lt)" laeuft     "
730 print#15,"m-e"chr$(ad)chr$(32):sys634
740 input#15,e,e$:input#14,e,e$,et,es:return
800 input#15,e,e$,et,es:ife=73thene=0
810 return
850 input#14,e,e$,et,es:ifethenprint"petsd:"e;e$;et;es
860 return
900 close2:close14:close15:end
950 print"formatiere":print#15,"n0:imager,xi":gosub800:ifethenprint"format:"e;e$:goto900
960 close2:open2,fu,2,"#0":gosub500:return
