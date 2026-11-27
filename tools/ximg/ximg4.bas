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
40 print"floppy unit (unit:drive)? ";:gosub790:fu$=l$:input"petsd unit";su:input"image";n$
42 fu=val(fu$):dn=0:fori=1tolen(fu$):c$=mid$(fu$,i,1):ifc$=":"orc$="."thendn=val(mid$(fu$,i+1))
43 next:u$=mid$(str$(dn),2)
45 ti$="000000":db=1:hz=50:dr=0:dp=1:iv=1:rem db=0: no step output, hz: 50/60 hz pet, dp=0: legacy via pet, iv: interleave
50 open15,fu,15,"i"+u$:open14,su,15:va=59456:d$="open, i0":gosub770
60 print#15,"m-r"chr$(252)chr$(255)chr$(2):get#15,a$,b$
70 a=asc(a$+chr$(0)):b=asc(b$+chr$(0)):dv=0
80 ifa=43andb=211thendv=21
90 ifa=183andb=242thendv=27
95 ifa=197thenlg=1:dv=26:rem 2031: legacy d64
96 ifa=142thenlg=2:dv=10:print#15,"i"+u$:rem 2040 dos 1: legacy d67
97 ifa=20andb=242thendv=30:print#15,"m-r"chr$(157)chr$(16)chr$(1):get#15,a$:h3=asc(a$+chr$(0)):d3=-60*(h3=4)-90*(h3=6):ifd3=0thendv=0
100 ifdv=0thenprint"drive not supported":goto900
105 d$="dos detect":gosub770
110 open2,fu,2,"#0":ad=6:ifm$="r"thenad=9
115 d$="open #0":gosub770
117 iflgthen3000
118 ifdv=30andm$="r"thenprint"hard disk: backup only":goto900
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
307 gosub796
310 gosub500:d$="disk type":gosub770:ifdt<>tyorfmthengosub950
320 gosub600:gosub400:gosub450:print#15,"i"+u$:gosub800:print#14,"x-img:e":gosub850
330 print"{home}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}done in"int((ti-tq)/hz*10)/10"s, total"int(ti/hz*10)/10"s, errors"bt:goto900
400 nt=35:ifty=80thennt=77
410 ifty=82thennt=154
413 ifty=60orty=90thennt=153
415 tp=19:ifty=80thentp=24
416 tp=tp-5*(ty=82):ifty=60orty=90thentp=ty*.83
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
445 close2:close15:open15,fu,15,"i"+u$:open2,fu,2,"#0":gosub500:gosub600:goto430
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
500 dt=0:ifdv=30thendt=d3:print#15,"u1:2 "u$" 1 0":goto800
501 print#15,"u1:2 "u$" 39 0":gosub800:ifethen530
510 print#15,"b-p:2,2":get#2,a$:ifa$<>chr$(67)then530
520 print#15,"u1:2 "u$" 38 3":gosub800:print#15,"b-p:2,5":get#2,a$:dt=82:ifasc(a$+chr$(0))=78thendt=80
525 return
530 print#15,"u1:2 "u$" 18 0":gosub800:ifethenreturn
540 print#15,"b-p:2,2":get#2,a$:ifa$=chr$(65)thendt=64
550 return
600 poke1010,0:poke1011,32:sys642
610 n=peek(1010)+256*peek(1011)-8192:ifn<800thenprint"drive code upload:"n"bytes":goto900
640 d$="code upload":gosub770:p$=chr$(0)+chr$(0)+chr$(dn)+chr$(7)+chr$(1)+chr$(2)+chr$(7)+chr$(8)+chr$(9)+chr$(10)+chr$(11)
650 p$=p$+chr$(0)+chr$(0)+chr$(0)+chr$(18)+chr$(19)+chr$(48)+chr$(49)+chr$(50)+chr$(51)+chr$(64)
660 p$=p$+chr$(0)+chr$(0)+chr$(0)+chr$(0):print#15,"m-w"chr$(67)chr$(35)chr$(25)p$
680 z$=chr$(40)+chr$(29)+chr$(54)+chr$(27)+chr$(65)+chr$(25)+chr$(78)+chr$(23)+chr$(117)+chr$(29)+chr$(131)+chr$(27)+chr$(142)+chr$(25)+chr$(155)+chr$(23)
685 ifty=64thenz$=chr$(18)+chr$(21)+chr$(25)+chr$(19)+chr$(31)+chr$(18)+chr$(36)+chr$(17)+mid$(z$,9)
686 ifty=60orty=90thenz$=chr$(153)+chr$(ty*32/15)+mid$(z$,3)
690 print#15,"m-w"chr$(240)chr$(35)chr$(16)z$:d$="parameters":gosub770:return
694 rem zone tables 695/696 are in 680/685 (reading them took 1.5 s); 3002 skips them
695 data40,29,54,27,65,25,78,23,117,29,131,27,142,25,155,23
696 data18,21,25,19,31,18,36,17
697 data76,128,2,76,180,2,174,224,3,240,32,32,198,255,173,226,3,141,227,3,32,207,255,172,227,3,153,58,3,238,227,3,173,227,3,205,225,3,208,236,76,204,255,172,226,3,169,0,153,58,3,200,204,225,3,208,247,96,174,224,3,32,201,255,173,226,3
698 data141,227,3,172,227,3,185,58,3,32,210,255,238,227,3,173,227,3,205,225,3,208,236,76,162,2,169,0,133,160,133,150,173,229,3,133,212,173,228,3,32,215,240,173,230,3,133,165,32,9,241,173,232,3,133,212,173,231,3,32,215,240,173,233,3,32,147
699 data241,173,234,3,141,235,3,173,64,232,9,2,141,64,232,44,64,232,16,8,165,155,201,239,208,245,240,29,173,64,232,41,253,141,64,232,169,60,141,33,232,44,64,232,16,251,169,52,141,33,232,206,235,3,208,207,44,230,150,32,174,241,76,185,241
700 o3=-(ty=60orty=90):print#14,"x-img:a,"fu","ft","lt:gosub850:ifethen900
710 print#15,"m-w"chr$(238)chr$(35)chr$(2)chr$(ft-o3)chr$(lt+1-o3):d$="x-img:a, range":gosub770
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
790 l$=""
791 geta$:ifa$=""then791
792 ifa$=chr$(13)thenprint:return
793 ifa$=chr$(20)andlen(l$)>0thenl$=left$(l$,len(l$)-1):printa$;:goto791
794 ifa$>=" "andlen(l$)<8thenl$=l$+a$:printa$;
795 goto791
796 print"{home}{down}{down}{down}{down}{down}{down}{down}{down}format disk first (y/n)? ";:gosub790:fm=(l$="y"):return
800 input#15,e,e$,et,es:ife=73thene=0
810 return
850 input#14,e,e$,et,es:ifethenprint"petsd:"e;e$;et;es
860 return
900 close2:close14:close15:end
950 print"formatting":print#15,"n"u$":imager,xi":gosub800:ifethenprint"format:"e;e$:goto900
960 close2:open2,fu,2,"#0":gosub500:return
2000 data#4cea214c4e22a2002ca203bd01208d3220bd02208d3320adee238d4323a200ad
2001 data#4323ddf0239004e8e8d0f7bdf1238d4423200000ee4323ad4323cdef23d0de60
2002 data#58ad8002091c29fd8d8002850329ef8502a50329fb8504ad5b230a0a0aaabd21
2003 data#108d7423bd22108d7523a9008d70238d71238d7723ae4623caa9ff9d5c23ca10
2004 data#fa60ac7023cc4423b037989d5c23ee7023a9029d6623bd47230a0a0aa8ad7423
2005 data#992110ad7523992210ad4323992310bd5c23992410bc4723ad45230d76239903
2006 data#1060bd51238d18218d24218d35218d4c218d5d218d952160c901f0138d7923bc
2007 data#5c23b9d123c901d006ad792399d1236020c220bd5c23aa8e0202a5028d8002ae
2008 data#73232c820230fb8e0202a5038d80022c820210fba000be00008e0202a5028d80
2009 data#02c8be00002c820230fb8e0202a5038d8002c8be00002c820210fbc000d0da60
2010 data#a0002c800270fbad0002990000a5048d8002c82c800250fbad0002990000a503
2011 data#8d8002c8d0dcad70230aa82c800270fbad0002999723a5048d80022c800250fb
2012 data#ad0002999823a5038d800260a900850618a000790000850565068506a505c8d0
2013 data#f2ae7223bd5c230aa8a505d99723d008a506d99823d00160a9074cd820a000be
2014 data#d1238e0202a5028d8002c8bed1232c820230fb8e0202a5038d8002c8bed1232c
2015 data#820210fbcc7823d0d960204020a9808d7623a200208220e8ec4623d0f7a200bd
2016 data#5c23c9c0b031bc4723b903103029c902900ebc6623f009de66232096204c3722
2017 data#8d73238e722320f020ee7123ae7223a9ff9d5c23208220ad7123cd4423f009e8
2018 data#ec4623d0ba4cfd21a9ff8d020260204020a9908d7623ac442388a90199d12388
2019 data#10faad7023cd4423f03bae7723bd5c233010bc4723b9031030f820d820a9ff9d
2020 data#5c2320c220204021ae7723ad70239d5c23ee7023209620e8ec4623d002a2008e
2021 data#77234c6222a200bd5c233010bc4723b9031030f820d820a9ff9d5c23e8ec4623
2022 data#d0e5a9808d7623a9008d70238d7123a200208220e8ec4623d0f7a200bd5c2330
2023 data#37bc4723b90310302fc9029014bc6623f009de66232096204c182320d8204c0d
2024 data#238e722320c220208c21ae7223ee7123a9ff9d5c23208220ad7123cd4423f009
2025 data#e8ec4623d0b64cda22ad442318690129fe8d782320bd21a9ff8d020260
3000 close2:open2,fu,2,"#":ty=64-3*(lg=2):ns=683-7*(lg=2):nt=35
3002 restore:fori=1to262:readx:next:fori=0to92:readx:poke634+i,x:next:fori=0to105:readx:poke826+i,x:next
3004 print"{home}{down}{down}{down}{down}{down}{down}legacy mode (u1/u2), d"mid$(str$(ty),2):ifm$="r"then3200
3006 iflg=2thengosub3500
3020 ifleft$(right$("    "+n$,4),2)<>".d"thenn$=n$+".d"+mid$(str$(ty),2)
3022 open4,su,4,n$+",p,w":input#14,e,e$:ife=63thenclose4:print"{home}{down}{down}{down}{down}{down}{down}{down}{down}";:input"file exists, replace (y/n)";a$:ifa$="y"thenopen4,su,4,"@:"+n$+",p,w":input#14,e,e$
3024 ifethenprint"petsd:"e;e$:goto900
3026 dimer%(690),u%(31):ec=0:k=0:tq=ti:poke996,64:poke997,fu:poke998,98:poke999,32:poke1000,su:poke1001,100
3030 fort=1tont:gosub3400:print"{home}{down}{down}{down}{down}{down}{down}{down}track"str$(t)" of"str$(nt)"   ":b0=k:fori=0ton-1:u%(i)=0:next:s=0
3040 fori=0ton-1:print#15,"u1:2 "u$t;s:input#15,e,e$,et,es
3042 c=1:ife>=20ande<=29thenc=e-18
3044 ife>0andc=1thenc=4
3046 er%(b0+s)=c:k=k+1:ifc>1thenec=ec+1
3047 ifiv>1thenb=b0+s:print#14,"p"chr$(4)chr$(0)chr$(band255)chr$(b/256)chr$(0)
3048 ifethenpoke992,0:poke994,0:poke993,128:sys634:poke992,4:sys637:sys637:goto3060
3049 ifdpandlg=1thenprint#15,"b-p:2,0":poke1002,0:sys826:on-(st<>0)goto3990:goto3060
3050 iflg=1thenprint#15,"b-p:2,0":poke994,0:poke993,128:poke992,2:sys634:poke992,4:sys637:poke992,2:sys634:poke992,4:sys637:goto3060
3051 ifdpthenprint#15,"m-r"chr$(0)chr$(pg)chr$(1):get#15,a$:print#4,chr$(asc(a$+chr$(0)));:print#15,"b-p:2,1":poke1002,255:sys826:on-(st<>0)goto3990:goto3060
3052 print#15,"m-r"chr$(0)chr$(pg)chr$(1):get#15,a$:poke826,asc(a$+chr$(0))
3054 print#15,"b-p:2,1":poke994,1:poke993,128:poke992,2:sys634:poke994,0:poke992,4:sys637:poke992,2:sys634:poke992,4:sys637
3060 u%(s)=1:s=s+iv:ifs>=nthens=s-n
3061 ifi<n-1thenifu%(s)thens=s+1:ifs>=nthens=0
3062 ifi<n-1thenifu%(s)then3061
3063 nexti:nextt:ifec=0then3090
3072 fork=0tons-1step128:c=128:ifk+128>nsthenc=ns-k
3074 forj=0toc-1:poke826+j,er%(k+j):next:poke992,4:poke994,0:poke993,c:sys637:nextk
3090 close4:print"{home}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}done in"int((ti-tq)/hz*10)/10"s, total"int(ti/hz*10)/10"s, bad sectors"ec:goto900
3200 print#14,"x-img:r,0,"n$:gosub850:ifethen900
3201 x=et:print#14,"x-img:e":gosub850:ifx<>tythenprint"image is not a d"mid$(str$(ty),2):goto900
3202 gosub796
3203 print#15,"u1:2 "u$" 18 0":input#15,e,e$:ifeorfmthenprint"formatting":print#15,"n"u$":imager,xi":input#15,e,e$:ifethenprint"format:"e;e$:goto900
3204 open4,su,4,n$+",p,r":input#14,e,e$:ifethenprint"petsd:"e;e$:goto900
3205 close2:open2,fu,2,"#":iflg=2thengosub3500
3206 tq=ti:ec=0:poke996,32:poke997,fu:poke998,98:poke999,64:poke1000,su:poke1001,100
3210 fort=1tont:gosub3400:print"{home}{down}{down}{down}{down}{down}{down}{down}track"str$(t)" of"str$(nt)"   "
3220 fors=0ton-1:iflg=2then3224
3221 ifdpthenprint#15,"b-p:2,0":poke1002,0:sys826:on-(st<>0)goto3990:goto3230
3222 print#15,"b-p:2,0":poke994,0:poke993,128:poke992,4:sys634:poke992,2:sys637:poke992,4:sys634:poke992,2:sys637:goto3230
3224 ifdpthenget#4,a$:print#15,"m-w"chr$(0)chr$(pg)chr$(1)chr$(asc(a$+chr$(0))):print#15,"b-p:2,1":poke1002,255:sys826:on-(st<>0)goto3990:goto3230
3225 poke994,0:poke993,128:poke992,4:sys634:print#15,"m-w"chr$(0)chr$(pg)chr$(1)chr$(peek(826)):print#15,"b-p:2,1"
3226 poke994,1:poke992,2:sys637:poke994,0:poke992,4:sys634:poke992,2:sys637
3230 print#15,"u2:2 "u$t;s:input#15,e,e$,et,es:ifethenec=ec+1
3232 nexts:nextt:print#15,"i"+u$:close4
3234 print"{home}{down}{down}{down}{down}{down}{down}{down}{down}{down}{down}done in"int((ti-tq)/hz*10)/10"s, total"int(ti/hz*10)/10"s, write errors"ec:goto900
3400 n=21:ift>17thenn=19-(lg=2)
3402 ift>24thenn=18
3404 ift>30thenn=17
3406 return
3500 print#15,"b-p:2,1":print#2,chr$(170)chr$(85)chr$(170)chr$(85);:pg=0
3502 forp=16to79:print#15,"m-r"chr$(1)chr$(p)chr$(1):get#15,a$:ifasc(a$+chr$(0))<>170then3506
3504 print#15,"m-r"chr$(2)chr$(p)chr$(1):get#15,a$:ifasc(a$+chr$(0))=85thenpg=p:p=79
3506 nextp:ifpg=0thenprint"drive buffer not found":goto900
3508 return
3990 print:print"aborted (stop key or no data)":goto900
