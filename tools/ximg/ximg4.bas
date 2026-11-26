10 rem disk-imager v4: pet directs, petsd does the data phase, whole disk in one pass
20 fori=0to7:readx:poke634+i,x:next:data173,16,232,41,64,208,249,96
21 rem 826: copy file #3 (ximgdrv.prg, after load address) via m-w to floppy $2000
22 fori=0to143:readx:poke826+i,x:next
23 data169,0,141,242,3,169,32,141,243,3,169,0,133,150,141,240,3,162,3,32,198
24 data255,32,207,255,172,240,3,153,208,3,200,140,240,3,165,150,141,244,3,208,4
25 data192,32,208,232,32,204,255,173,240,3,240,89,162,15,32,201,255,169,77,32,210
26 data255,169,45,32,210,255,169,87,32,210,255,173,242,3,32,210,255,173,243,3,32
27 data210,255,173,240,3,32,210,255,169,0,141,241,3,172,241,3,185,208,3,32,210
28 data255,238,241,3,173,241,3,205,240,3,208,236,32,204,255,24,173,242,3,109,240
29 data3,141,242,3,144,3,238,243,3,173,244,3,208,3,76,68,3,96
30 print"{clr}disk-imager v4":input"(b)ackup/(r)estore";m$
40 input"floppy unit";fu:input"petsd unit";su:input"image";n$
45 ti$="000000":db=1:hz=50:dr=0:rem db=0: no step output, hz: 50 or 60 hz pet
50 open15,fu,15,"i0":open14,su,15:va=59456:d$="open, i0":gosub770
60 print#15,"m-r"chr$(252)chr$(255)chr$(2):get#15,a$,b$
70 a=asc(a$+chr$(0)):b=asc(b$+chr$(0)):dv=0
80 ifa=43andb=211thendv=21
90 ifa=183andb=242thendv=27
100 ifdv=0thenprint"drive not supported":goto900
105 d$="dos detect":gosub770
110 open2,fu,2,"#0":ad=6:ifm$="r"thenad=9
115 d$="open #0":gosub770
120 ifm$="r"then300
200 gosub500:ty=dt:d$="disk type":gosub770:ifty=0thenprint"no cbm disk":goto900
210 print#14,"x-img:b,"ty","n$:gosub850:d$="x-img:b":gosub770:ife then900
220 gosub600:gosub400:gosub450:print#14,"x-img:e":gosub850
230 print"{home}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}done in"int((ti-tq)/hz*10)/10"s, total"int(ti/hz*10)/10"s, errors"bt:goto900
300 ty=val(right$(n$,2)):print#14,"x-img:r,"ty","n$:gosub850:d$="x-img:r":gosub770:ife then900
310 gosub500:d$="disk type":gosub770:ifdt<>tythengosub950
320 gosub600:gosub400:gosub450:print#15,"i0":gosub800:print#14,"x-img:e":gosub850
330 print"{home}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}done in"int((ti-tq)/hz*10)/10"s, total"int(ti/hz*10)/10"s, errors"bt:goto900
400 nt=35:ifty=80thennt=77
410 ifty=82thennt=154
415 tp=19:ifty=80thentp=24
416 ifty=82thentp=32
417 ifm$="r"thentp=tp*1.35
420 tq=ti:ft=1:lt=nt:rt=0
430 gosub700:ife=0thenreturn
432 rt=rt+1:ifet<>ftthenrt=1
434 ft=et:m$="aborted at track"+str$(et)+", retrying":gosub760
436 ifrt<=3then445
438 m$="power-cycle the floppy, then press a key":gosub760
440 geta$:ifa$=""then440
442 rt=0
445 close2:close15:open15,fu,15,"i0":open2,fu,2,"#0":gosub500:gosub600:goto430
450 bt=0:nb=0:f=1
455 print#14,"x-img:l,"f:input#14,e,e$,t,es:ift=0then470
460 nb=nb+1:bt=bt+es:f=t+1:iff<=ntthen455
470 ifnb=0thenreturn
475 printnb"track(s) with errors":input"retry these tracks (y/n)";a$:ifa$<>"y"thenreturn
480 f=1
485 print#14,"x-img:l,"f:input#14,e,e$,t,es:ift=0then450
490 ft=t:lt=t:gosub430
495 f=t+1:iff<=ntthen485
499 goto450
500 dt=0:print#15,"u1:2 0 39 0":gosub800:ifethen530
510 print#15,"b-p:2,2":get#2,a$:ifa$<>chr$(67)then530
520 print#15,"u1:2 0 38 3":gosub800:print#15,"b-p:2,5":get#2,a$:dt=82:ifasc(a$+chr$(0))=78thendt=80
525 return
530 print#15,"u1:2 0 18 0":gosub800:ifethenreturn
540 print#15,"b-p:2,2":get#2,a$:ifa$=chr$(65)thendt=64
550 return
600 open3,su,3,"ximgdrv.prg,p,r":get#3,a$,a$:sys826
640 close3:d$="code upload":gosub770:p$=chr$(0)+chr$(0)+chr$(0)+chr$(7)+chr$(1)+chr$(2)+chr$(7)+chr$(8)+chr$(9)+chr$(10)+chr$(11)
650 p$=p$+chr$(0)+chr$(0)+chr$(0)+chr$(18)+chr$(19)+chr$(48)+chr$(49)+chr$(50)+chr$(51)+chr$(64)
660 p$=p$+chr$(0)+chr$(0)+chr$(0)+chr$(0):print#15,"m-w"chr$(67)chr$(35)chr$(25)p$
670 o$="":fori=0to28:o$=o$+chr$(i):next:print#15,"m-w"chr$(122)chr$(35)chr$(29)o$
680 restore:fori=0to151:readx:next:z$="":fori=1to16:readx:z$=z$+chr$(x):next
685 ifty=64thenfori=1to8:readx:z$=left$(z$,i-1)+chr$(x)+mid$(z$,i+1):next
690 print#15,"m-w"chr$(240)chr$(35)chr$(16)z$:d$="parameters":gosub770:return
695 data40,29,54,27,65,25,78,23,117,29,131,27,142,25,155,23
696 data18,21,25,19,31,18,36,17
700 print#14,"x-img:a,"fu","ft","lt:gosub850:ifethen900
710 print#15,"m-w"chr$(238)chr$(35)chr$(2)chr$(ft)chr$(lt+1):d$="x-img:a, range":gosub770
720 print"{home}{down}{down}{down}{down}{down}{down}track"str$(ft)" to"str$(lt)"      "
730 print#15,"m-e"chr$(ad)chr$(32):t0=ti:tl=ti:v=0:x=0:q=-1
732 if(peek(59408)and64)=0then740
734 w=peek(59456)and192:ifw<>vthenv=w:tl=ti
735 ifti-tl>600thenx=1:m$="no response, aborting":gosub760:pokeva,peek(va)and251:fori=1to20:next:pokeva,peek(va)or4:goto740
736 y=ft+int((tl-t0)/tp):ify>lttheny=lt
737 ify<>qthenq=y:print"{home}{down}{down}{down}{down}{down}{down}{down}track ca."str$(y)"   "
738 goto732
740 input#15,e,e$:input#14,e,e$,et,es:return
760 print"{home}{down}{down}{down}{down}{down}{down}{down}{down}"left$(m$+"                                        ",39):return
770 ifdbthenprint"{home}"left$("{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}",13+dr)d$tab(20)int(ti/hz*10)/10"s":dr=dr+1
771 return
800 input#15,e,e$,et,es:ife=73thene=0
810 return
850 input#14,e,e$,et,es:ifethenprint"petsd:"e;e$;et;es
860 return
900 close2:close14:close15:end
950 print"formatting":print#15,"n0:imager,xi":gosub800:ifethenprint"format:"e;e$:goto900
960 close2:open2,fu,2,"#0":gosub500:return
