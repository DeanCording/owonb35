LOCATION=/usr/local
CFLAGS=-Wall -O2 -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include
LIBS=-lgattlib -lglib-2.0 -lm

OBJ=owonb35
OFILES=
default: owonb35

.c.o:
	${CC} ${CFLAGS} $(COMPONENTS) -c $*.c

all: ${OBJ}

owonb35: ${OFILES} owonb35.c
	${CC} ${CFLAGS} $(COMPONENTS) owonb35.c ${OFILES} -o owonb35 ${LIBS}

install: ${OBJ}
	cp owonb35 ${LOCATION}/bin/

clean:
	rm -f *.o *core ${OBJ}
