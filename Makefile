CC=gcc
CFLAGS=-O2 -W -Werror -Wall
PROG=fakehttp
OBJS=fakehttp.c fakecommon.c fakedata.c

all: $(PROG)

$(PROG):$(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(PROG) -lpthread

.PHONY:clean

clean:
	        rm -rf *.o $(PROG)
