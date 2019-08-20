objects = main.o csv.o cidr.o

mm2xtgeoip : $(objects)
	cc -o mm2xtgeoip $(objects)
main.o : mm2xtgeoip.c mm2xtgeoip.h csv.h cidr.h
	cc -c mm2xtgeoip.c -o main.o
csv.o : csv.c csv.h
	cc -c csv.c
cidr.o : cidr.c cidr.h
	cc -c cidr.c

.PHONY: clean
clean:
	rm -f $(objects) mm2xtgeoip

.PHONY: install
install: mm2xtgeoip
	install -d $(DESTDIR)$(PREFIX)/bin/
	install -m 755 mm2xtgeoip $(DESTDIR)$(PREFIX)/bin/
	install -m 755 mm2xtgeoip_dl $(DESTDIR)$(PREFIX)/bin/
