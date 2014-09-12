LIBS=`pkg-config --libs clutter-1.0` `pkg-config --libs cogl-1.0` `pkg-config --libs mx-1.0` `pkg-config --libs clutter-gst-1.0`
INCS=`pkg-config --cflags clutter-1.0` `pkg-config --cflags cogl-1.0 ` `pkg-config --cflags mx-1.0` `pkg-config --cflags clutter-gst-1.0`
CFLAGS=-g -O0  

.c.o:
	$(CC) -Wall $(CFLAGS) $(INCS) -c $*.c

all: clutter_02 

clutter_02: clutter_02.o logging.o pinpoint.o pp-clutter.o get_mac.o
	$(CC) -Wall $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -fr *.o clutter_02
