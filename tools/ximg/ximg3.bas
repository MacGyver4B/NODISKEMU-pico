10 rem disk-imager v3: pet steuert, petsd fuehrt die datenphase
20 fori=0to7:readx:poke634+i,x:next:data173,16,232,41,64,208,249,96
30 print"{clr}disk-imager v3":input"(b)ackup/(r)estore";m$
40 input"floppy-geraet";fu:input"petsd-geraet";su:input"image";n$
50 open15,fu,15,"i0":open14,su,15
60 print#15,"m-r"chr$(252)chr$(255)chr$(2):get#15,a$,b$
70 a=asc(a$+chr$(0)):b=asc(b$+chr$(0)):dv=0
80 ifa=43andb=211thendv=21
90 ifa=183andb=242thendv=27
100 ifdv=0thenprint"laufwerk nicht unterstuetzt":goto900
110 open2,fu,2,"#0":bt=0:we=0:ifm$="r"thenwe=3
120 ifm$="r"then300
200 gosub500:ty=dt:ifty=0thenprint"keine cbm-diskette":goto900
210 print#14,"x-img:b,"ty","n$:gosub850:ife then900
220 gosub600:gosub400:print#14,"x-img:e":gosub850
230 print"fertig "ti$" fehler"bt:goto900
300 ty=val(right$(n$,2)):print#14,"x-img:r,"ty","n$:gosub850:ife then900
310 gosub500:ifdt<>tythengosub950
320 gosub600:gosub400:print#15,"i0":gosub800:print#14,"x-img:e":gosub850
330 print"fertig "ti$" fehler"bt:goto900
400 nt=35:ifty=80thennt=77
410 ifty=82thennt=154
420 ti$="000000":ln=0:fort=1tont:gosub700:next:return
500 dt=0:print#15,"u1:2 0 39 0":gosub800:ifethen530
510 print#15,"b-p:2,2":get#2,a$:ifa$<>chr$(67)then530
520 print#15,"u1:2 0 38 3":gosub800:print#15,"b-p:2,5":get#2,a$:dt=82:ifasc(a$+chr$(0))=78thendt=80
525 return
530 print#15,"u1:2 0 18 0":gosub800:ifethenreturn
540 print#15,"b-p:2,2":get#2,a$:ifa$=chr$(65)thendt=64
550 return
600 open3,su,3,"ximgdrv.prg,p,r":get#3,a$,a$:ad=8192
620 s$="":z=0:fori=1to32:get#3,a$:s$=s$+chr$(asc(a$+chr$(0))):z=st:ifztheni=33
630 next:print#15,"m-w"chr$(adand255)chr$(int(ad/256))chr$(len(s$))s$:ad=ad+len(s$):ifz=0then620
640 close3:p$=chr$(0)+chr$(0)+chr$(0)+chr$(7)+chr$(1)+chr$(2)+chr$(7)+chr$(8)+chr$(9)+chr$(10)+chr$(11)
650 p$=p$+chr$(0)+chr$(0)+chr$(0)+chr$(18)+chr$(19)+chr$(48)+chr$(49)+chr$(50)+chr$(51)+chr$(64)
660 p$=p$+chr$(0)+chr$(0)+chr$(0)+chr$(0):print#15,"m-w"chr$(64)chr$(35)chr$(25)p$:return
700 ifty=64thenns=21+2*(t>17)+(t>24)+(t>30)
705 ifty>64thenx=t:ifx>77thenx=x-77
706 ifty>64thenns=29+2*(x>39)+2*(x>53)+2*(x>64)
710 print#14,"x-img:t,"fu","t:gosub850:ifethen900
720 print#15,"m-w"chr$(64)chr$(35)chr$(2)chr$(t)chr$(ns):ifns<>lnthengosub760
740 print#15,"m-e"chr$(we)chr$(32):sys634
750 input#15,e,e$:input#14,e,e$,et,es:bt=bt+es:print"{home}{down}{down}{down}{down}{down}{down}spur"t"fehler"bt"  ":return
760 o$="":fori=0tons-1:o$=o$+chr$(i):next:print#15,"m-w"chr$(119)chr$(35)chr$(ns)o$:ln=ns:return
800 input#15,e,e$,et,es:ife=73thene=0
810 return
850 input#14,e,e$,et,es:ifethenprint"petsd:"e;e$;et;es
860 return
900 close2:close14:close15:end
950 print"formatiere":print#15,"n0:imager,xi":gosub800:ifethenprint"format:"e;e$:goto900
960 close2:open2,fu,2,"#0":gosub500:return
