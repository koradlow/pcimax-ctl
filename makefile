#Define the compiler we want to use
CC = gcc
#Define the compiler options for this project
CFLAGS += -Wall -O3 -std=gnu99
#Define the libraries that are used for this project
LDLIBS += 

#Define the output target
TARGET = pcimax-ctl

#All source packages
SOURCES = pcimax-ctl.c

#Define all object files
OBJ = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)

%.o: %.c $(SOURCES)
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(OBJ)

PREFIX:= /usr/local

install: all
	test -d $(PREFIX) || mkdir $(PREFIX)
	test -d $(PREFIX)/bin || mkdir $(PREFIX)/bin
	install -m 0755 $(TARGET) $(PREFIX)/bin; \

uninstall:
	test $(PREFIX)/bin/$(TARGET)
	rm -f $(PREFIX)/bin/$(TARGET)
