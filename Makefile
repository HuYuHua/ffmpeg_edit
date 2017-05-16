CC=g++
CPLUSPLUS=g++

EXEC = merger 

all:    $(EXEC)

INCLUDES = -I . -I/usr/local/include


CFLAGS = $(INCLUDES) -g -Wall -D__STDC_CONSTANT_MACROS 


LIBS=-L/usr/local/lib \
     -lavcodec -lavfilter -lavutil -lavdevice -lavformat -lswscale -lswresample 

OBJS = merger.o

$(OBJS) : merger.cpp
	$(CC) -c $(CFLAGS) $< -o $@

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJS) $(LIBS)

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

.cpp.o:
	$(CPLUSPLUS) -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o *~
	rm -f $(EXEC)
