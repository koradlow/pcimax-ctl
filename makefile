#Define the compiler we want to use
CC = gcc
#Define the compiler options for this project
CFLAGS += -Wall -O3 -std=gnu99
#Define the libraries that are used for this project
LDLIBS += -ludev

#Define the output target
TARGET = pcimax-ctl

#All source packages
SOURCES = ./include/inih/ini.c ./pcimax-ctl.c
VPATH := ./include/inih

#Define all object files
#(remove path information from source files)
COMMON_OBJS := $(patsubst %.c, %.o, $(notdir $(SOURCES)))

#Build all object files
%.o : %.c $(SOURCES)
	@echo creating "$@" ...
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(COMMON_OBJS)
	@echo building target binary "$(TARGET)" ...
	$(CC) -o $(TARGET) $(COMMON_OBJS) $(LDLIBS)

all: $(TARGET)

clean:
	rm -f $(COMMON_OBJS)

PREFIX:= /usr/local

install: all
	test -d $(PREFIX) || mkdir $(PREFIX)
	test -d $(PREFIX)/bin || mkdir $(PREFIX)/bin
	install -m 0755 $(TARGET) $(PREFIX)/bin; \

uninstall:
	test $(PREFIX)/bin/$(TARGET)
	rm -f $(PREFIX)/bin/$(TARGET)
