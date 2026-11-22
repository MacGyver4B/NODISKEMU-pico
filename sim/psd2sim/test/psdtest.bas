10 dim r$(60):n=0:f=0:open15,8,15
20 gosub9000:t$="start":x=0:gosub9100
100 rem datei fehlt
110 open2,8,2,"gibtsnicht,s,r":gosub9000:close2:t$="fnf":x=62:gosub9100
200 rem seq schreiben und lesen
210 open2,8,2,"@:seq1,s,w":gosub9000:t$="seq w":x=0:gosub9100
220 fori=1to50:print#2,"zeile";chr$(44);i:next:close2:gosub9000:t$="seq close":x=0:gosub9100
230 open2,8,2,"seq1,s,r":gosub9000:t$="seq r":x=0:gosub9100
240 k=0:fori=1to50:input#2,a$,v:ifa$="zeile"andv=ithenk=k+1
250 next:close2:t$="seq inhalt":x=50:er=k:gosub9100
300 rem rename copy scratch
310 print#15,"r:seq2=seq1":gosub9000:t$="rename":x=0:gosub9100
320 open2,8,2,"seq1,s,r":gosub9000:close2:t$="alt weg":x=62:gosub9100
330 print#15,"c:seq3=seq2":gosub9000:t$="copy":x=0:gosub9100
340 open2,8,2,"seq3,s,r":input#2,a$,v:close2:t$="copy inhalt":x=1:er=v:gosub9100
350 print#15,"s:seq2,seq3":gosub9000:t$="scratch":x=1:gosub9100
360 t$="scratch anz":x=2:er=et:gosub9100
400 rem relative datei
410 open2,8,2,"rel1,l,"+chr$(20):gosub9000:t$="rel open":x=0:gosub9100
420 fori=1to10:print#15,"p"chr$(2)chr$(i)chr$(0)chr$(1):print#2,"satz"+str$(i):next
430 print#15,"p"chr$(2)chr$(7)chr$(0)chr$(1):input#2,a$
440 t$="rel satz7":x=0:er=-(a$<>"satz 7"):gosub9100
450 close2:open2,8,2,"rel1":print#15,"p"chr$(2)chr$(10)chr$(0)chr$(1):input#2,a$:close2
460 t$="rel neu":x=0:er=-(a$<>"satz 10"):gosub9100
500 rem images
510 fori=1to4:readd$:print#15,"cd:"+d$:gosub9000:t$="cd "+d$:x=0:gosub9100
520 open2,8,2,"@:img,s,w":print#2,d$:close2:gosub9000:t$="w "+d$:x=0:gosub9100
530 open2,8,2,"img,s,r":input#2,a$:close2:t$="r "+d$:x=0:er=-(a$<>d$):gosub9100
540 print#15,"cd:"+chr$(95):gosub9000:t$="cd zurueck":x=0:gosub9100
550 next
560 data t.d64,t.d80,t.d81,t.d82
600 rem unterverzeichnis
610 print#15,"md:ordner":gosub9000:t$="md":x=0:gosub9100
620 print#15,"cd:ordner":gosub9000:t$="cd ordner":x=0:gosub9100
630 open2,8,2,"@:drin,s,w":print#2,"x":close2:gosub9000:t$="w ordner":x=0:gosub9100
640 print#15,"cd:"+chr$(95):gosub9000:t$="cd zurueck":x=0:gosub9100
700 rem m-r ohne emulierten speicher liest 0 (andyg 0016)
710 print#15,"m-r"chr$(164)chr$(67)chr$(1):get#15,a$:t$="m-r 43a4":x=0:er=asc(a$+chr$(0)):gosub9100
720 rem sektorbefehle im image, bam-zwischenspeicher (andyg 0017)
730 print#15,"cd:t.d64":gosub9000:t$="cd d64 sektor":x=0:gosub9100
740 open5,8,5,"#":gosub9000:t$="puffer":x=0:gosub9100
750 print#15,"u1:5,0,18,0":gosub9000:t$="u1 18,0":x=0:gosub9100
760 print#15,"b-p:5,144":print#5,"q";:print#15,"u2:5,0,18,0":gosub9000:t$="u2 18,0":x=0:gosub9100
770 open2,8,2,"@:u2test,s,w":print#2,"a":close2:gosub9000:t$="datei nach u2":x=0:gosub9100
780 print#15,"u1:5,0,18,0":print#15,"b-p:5,144":get#5,a$:close5
790 t$="u2 bleibt":x=0:er=-(a$<>"q"):gosub9100
795 print#15,"cd:"+chr$(95):gosub9000:t$="cd zurueck":x=0:gosub9100
900 open3,8,3,"@:testlog,s,w":fori=1ton:print#3,r$(i):next:close3
910 print:print"ergebnis";n-f;"von";n;"ok":close15:end
9000 input#15,er,er$,et,es:return
9100 n=n+1:o$="ok":ifer<>xtheno$="fehler":f=f+1
9110 r$(n)=t$+": "+o$+str$(er)+" "+er$
9120 printt$;" ";o$;er:return
