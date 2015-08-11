CC ?= gcc
PKG_CONFIG := pkg-config

CFLAGS ?= -Wall -g
INCLUDES := `$(PKG_CONFIG) --cflags glib-2.0 gtk+-3.0`
LDFLAGS ?= 
LIBS := `$(PKG_CONFIG) --libs glib-2.0 gtk+-3.0`

TLVERSION := '$(shell git describe --tags --always) ($(shell git log --pretty=format:%cd --date=short -n1), branch \"$(shell git describe --tags --always --all | sed s:heads/::)\")'

APPNAME := timelapse-gtk
PREFIX ?= /usr

tl_SRC := $(wildcard *.c)
tl_OBJ := $(tl_SRC:.c=.o)
tl_HEADERS := $(wildcard *.h)

CFLAGS += -DTLVERSION=\"${TLVERSION}\"
CFLAGS += -DAPPNAME=\"${APPNAME}\"

all: $(APPNAME)

$(APPNAME): $(tl_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

%.o: %.c $(tl_HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

install: $(APPNAME)
	install $(APPNAME) $(PREFIX)/bin

uninstall:
	rm -f $(PREFIX)/bin/$(APPNAME)

clean:
	rm -f $(APPNAME) $(tl_OBJ)

.PHONY: all clean install
