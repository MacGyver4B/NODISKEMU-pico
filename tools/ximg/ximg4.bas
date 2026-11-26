10 rem disk-imager v4: pet directs, petsd does the data phase, whole disk in one pass
20 fori=0to7:readx:poke634+i,x:next:data173,16,232,41,64,208,249,96
21 data162,15,32,201,255,169,77,32,210,255,169,45,32,210,255,169,87,32,210,255,173,242,3,32,210,255,173,243,3
22 fori=0to81:readx:poke826+i,x:next:fori=0to147:readx:poke642+i,x:next
23 data32,210,255,173,240,3,32,210,255,169,0,141,241,3,172,241,3,185,208,3,32,210,255,238,241,3,173,241,3
24 data205,240,3,208,236,32,204,255,24,173,242,3,109,240,3,141,242,3,144,3,238,243,3,96,169,0,141,240,3
25 data141,244,3,165,40,141,9,3,165,41,141,10,3,160,1,32,8,3,240,100,160,4,32,8,3,201,131,208,66
26 data200,32,8,3,201,35,208,58,238,244,3,200,32,8,3,240,54,32,12,3,10,10,10,10,141,241,3,200,32
27 data8,3,32,12,3,13,241,3,174,240,3,157,208,3,232,142,240,3,224,32,208,216,152,72,32,58,3,169,0
28 data141,240,3,104,168,76,175,2,173,244,3,208,20,160,0,32,8,3,72,200,32,8,3,141,10,3,104,141,9
29 data3,76,148,2,173,240,3,240,3,32,58,3,96,185,0,0,96,201,65,144,3,233,55,96,41,15,96
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
205 ifleft$(right$("    "+n$,4),2)<>".d"thenn$=n$+".d"+mid$(str$(ty),2)
210 print#14,"x-img:b,"ty","n$:input#14,e,e$,et,es:ife=63thenprint"{home}{down}{down}{down}{down}{down}{down}{down}{down}";:input"file exists, replace (y/n)";a$:ifa$="y"thenprint#14,"x-img:b,"ty",@"n$:input#14,e,e$,et,es
211 m$="":gosub760:ifethenprint"petsd:"e;e$;et;es
212 d$="x-img:b":gosub770:ife then900
220 gosub600:gosub400:gosub450:print#14,"x-img:e":gosub850
230 print"{home}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}done in"int((ti-tq)/hz*10)/10"s, total"int(ti/hz*10)/10"s, errors"bt:goto900
300 print#14,"x-img:r,0,"n$:gosub850:d$="x-img:r":gosub770:ife then900
305 ty=et
310 gosub500:d$="disk type":gosub770:ifdt<>tythengosub950
320 gosub600:gosub400:gosub450:print#15,"i0":gosub800:print#14,"x-img:e":gosub850
330 print"{home}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}done in"int((ti-tq)/hz*10)/10"s, total"int(ti/hz*10)/10"s, errors"bt:goto900
400 nt=35:ifty=80thennt=77
410 ifty=82thennt=154
415 tp=19:ifty=80thentp=24
416 ifty=82thentp=24
417 ifm$="r"thentp=tp*1.4
418 kk=ty-100*(m$="r"):ifpeek(1013)=kkandpeek(1018)=fuand((kk+fu+peek(1015)+peek(1016))and255)=peek(1017)thentp=(peek(1015)+256*peek(1016))/10
420 tq=ti:ft=1:lt=nt:rt=0
430 gosub700:ife=0andrt=0andft=1andlt=ntthengosub780
431 ife=0thenreturn
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
600 poke1010,0:poke1011,32:sys642
610 n=peek(1010)+256*peek(1011)-8192:ifn<800thenprint"drive code upload:"n"bytes":goto900
640 d$="code upload":gosub770:p$=chr$(0)+chr$(0)+chr$(0)+chr$(7)+chr$(1)+chr$(2)+chr$(7)+chr$(8)+chr$(9)+chr$(10)+chr$(11)
650 p$=p$+chr$(0)+chr$(0)+chr$(0)+chr$(18)+chr$(19)+chr$(48)+chr$(49)+chr$(50)+chr$(51)+chr$(64)
660 p$=p$+chr$(0)+chr$(0)+chr$(0)+chr$(0):print#15,"m-w"chr$(67)chr$(35)chr$(25)p$
670 o$="":fori=0to28:o$=o$+chr$(i):next:print#15,"m-w"chr$(122)chr$(35)chr$(29)o$
680 restore:fori=0to237:readx:next:z$="":fori=1to16:readx:z$=z$+chr$(x):next
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
780 v=int((ti-t0)/nt*10):poke1015,vand255:poke1016,int(v/256):poke1013,kk:poke1018,fu
781 poke1017,(kk+fu+peek(1015)+peek(1016))and255:return
800 input#15,e,e$,et,es:ife=73thene=0
810 return
850 input#14,e,e$,et,es:ifethenprint"petsd:"e;e$;et;es
860 return
900 close2:close14:close15:end
950 print"formatting":print#15,"n0:imager,xi":gosub800:ifethenprint"format:"e;e$:goto900
960 close2:open2,fu,2,"#0":gosub500:return
2000 data#4cef214c5122a2002ca203bd01208d3220bd02208d3320adee238d4323a200ad
2001 data#4323ddf0239004e8e8d0f7bdf1238d4423200000ee4323ad4323cdef23d0de60
2002 data#58ad8002091c29fd8d8002850329ef8502a50329fb8504ad5b230a0a0aaabd21
2003 data#108d7423bd22108d7523a9008d70238d71238d7723ae4623caa9ff9d5c23ca10
2004 data#fa60ac7023cc4423b039b97a239d5c23ee7023a9029d6623bd47230a0a0aa8ad
2005 data#7423992110ad7523992210ad4323992310bd5c23992410bc4723ad45230d7623
2006 data#99031060bd51238d1a218d26218d37218d4e218d5f218d9a2160c901f0138d79
2007 data#23bc5c23b9d123c901d006ad792399d1236020c420bd5c23aa8e0202a5028d80
2008 data#02ae73232c820230fb8e0202a5038d80022c820210fba000be00008e0202a502
2009 data#8d8002c8be00002c820230fb8e0202a5038d8002c8be00002c820210fbc000d0
2010 data#da60a0002c800270fbad0002990000a5048d8002c82c800250fbad0002990000
2011 data#a5038d8002c8d0dcac7023b97a230aa82c800270fbad0002999723a5048d8002
2012 data#2c800250fbad0002999823a5038d800260a900850618a0007900008505650685
2013 data#06a505c8d0f2ae7223bd5c230aa8a505d99723d008a506d99823d00160a9074c
2014 data#da20a000bed1238e0202a5028d8002c8bed1232c820230fb8e0202a5038d8002
2015 data#c8bed1232c820210fbcc7823d0d960204020a9808d7623a200208220e8ec4623
2016 data#d0f7a200bd5c233031bc4723b903103029c902900ebc6623f009de6623209820
2017 data#4c3a228d73238e722320f220ee7123ae7223a9ff9d5c23208220ad7123cd4423
2018 data#f009e8ec4623d0bc4c0222a9ff8d020260204020a9908d7623ac442388a90199
2019 data#d1238810faad7023cd4423f03eae7723bd5c233010bc4723b9031030f820da20
2020 data#a9ff9d5c2320c420204221ae7723ac7023b97a239d5c23ee7023209820e8ec46
2021 data#23d002a2008e77234c6522a200bd5c233010bc4723b9031030f820da20a9ff9d
2022 data#5c23e8ec4623d0e5a9808d7623a9008d70238d7123a200208220e8ec4623d0f7
2023 data#a200bd5c233037bc4723b90310302fc9029014bc6623f009de66232098204c1e
2024 data#2320da204c13238e722320c420209121ae7223ee7123a9ff9d5c23208220ad71
2025 data#23cd4423f009e8ec4623d0b64ce022ad442318690129fe8d782320c221a9ff8d
2026 data#020260
