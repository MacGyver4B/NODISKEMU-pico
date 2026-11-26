10 rem atn-abbruchtest
20 fori=0to7:readx:poke634+i,x:next:data173,16,232,41,64,208,249,96
30 fu=9:su=11:ty=82:dl=400:ut=39:ifty=64thenut=18
40 nt=35:ifty=80thennt=77
45 ifty=82thennt=154
50 open15,fu,15,"i0":open14,su,15:open2,fu,2,"#0":print#15,"u1:2 0"ut"0"
60 print#14,"x-img:b,"ty",abt"ty:gosub850
70 gosub600
80 print#14,"x-img:a,"fu",1,"nt:gosub850
90 print#15,"m-w"chr$(238)chr$(35)chr$(2)chr$(1)chr$(nt+1)
100 print#15,"m-e"chr$(6)chr$(32):fori=1todl:next
110 close2:print#15,"i0":open2,fu,2,"#0":print#15,"u1:2 0"ut"0":input#15,e,e$,et,es:print"floppy nach atn:"e;e$
120 input#14,e,e$,et,es:print"petsd nach atn:"e;e$;et;es
130 print#15,"m-r"chr$(0)chr$(32)chr$(3):get#15,a$,b$,c$:print"code:"asc(a$+chr$(0))asc(b$+chr$(0))
140 print#14,"x-img:a,"fu",1,"nt:gosub850
150 print#15,"m-w"chr$(238)chr$(35)chr$(2)chr$(1)chr$(nt+1)
160 print#15,"m-e"chr$(6)chr$(32):sys634
170 input#15,e,e$:print"floppy:"e;e$:input#14,e,e$,et,es:print"petsd:"e;e$;et;es
180 print#14,"x-img:e":gosub850:print"ende"
190 close2:close14:close15:end
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
850 input#14,e,e$,et,es:ifethenprint"petsd:"e;e$;et;es
860 return
