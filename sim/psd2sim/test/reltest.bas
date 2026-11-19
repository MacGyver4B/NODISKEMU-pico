10 dim r$(20):n=0:f=0:open15,8,15:gosub9000
20 readd$:ifd$="ende"then900
30 print#15,"cd:"+d$:gosub9000:t$="cd "+d$:x=0:gosub9100
40 open2,8,2,"rel2,l,"+chr$(30):gosub9000:t$="open":x=0:ifer=50thenx=50
45 gosub9100
50 fori=1to20:print#15,"p"chr$(98)chr$(i)chr$(0)chr$(1):print#2,"satz"+str$(i):next:close2
60 open2,8,2,"rel2":gosub9000:t$="reopen":x=0:gosub9100
70 print#15,"p"chr$(98)chr$(13)chr$(0)chr$(1):input#2,a$:close2
80 t$="satz13 "+d$:x=0:er=-(a$<>"satz 13"):gosub9100
90 print#15,"cd:"+chr$(95):gosub9000:goto20
100 data t.d80,t.d82,t.d64,ende
900 open3,8,3,"@:rellog,s,w":fori=1ton:print#3,r$(i):next:close3
910 print"ergebnis";n-f;"von";n:close15:end
9000 input#15,er,er$,et,es:return
9100 n=n+1:o$="ok":ifer<>xtheno$="fehler":f=f+1
9110 r$(n)=t$+": "+o$+str$(er)+" "+er$:printr$(n):return
