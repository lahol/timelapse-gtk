CC ?= gcc
PKG_CONFIG := pkg-config

CFLAGS ?= -Wall -g
INCLUDES := `$(PKG_CONFIG) --cflags glib-2.0 gtk+-3.0 gstreamer-0.10 gdk-3.0 gstreamer-interfaces-0.10`
LDFLAGS ?= 
LIBS := `$(PKG_CONFIG) --libs glib-2.0 gtk+-3.0 gstreamer-0.10 gdk-3.0 gstreamer-interfaces-0.10`

TLVERSION := '$(shell [ -f TL_VERSION ] && cat TL_VERSION)'
VERSION := '$(shell [ -f VERSION ] && cat VERSION)'
ifeq ('',$(TLVERSION))
TLVERSION := '$(shell git describe --tags --always) ($(shell git log --pretty=format:%cd --date=short -n1), branch \"$(shell git describe --tags --always --all | sed s:heads/::)\")'
VERSION := $(shell git describe --tags --abbrev=0)
endif

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
	install $(APPNAME).desktop $(PREFIX)/share/applications

uninstall:
	rm -f $(PREFIX)/bin/$(APPNAME)

dist: $(tl_SRC) $(tl_HEADERS) Makefile
	[ ! -d ${APPNAME}-${VERSION} ] || rm -rf ${APPNAME}-${VERSION}
	[ ! -e ${APPNAME}-${VERSION}.tar.gz ] || rm ${APPNAME}-${VERSION}.tar.gz
	mkdir ${APPNAME}-${VERSION}
	cp $(tl_SRC) $(tl_HEADERS) Makefile ${APPNAME}.desktop LICENSE ${APPNAME}-${VERSION}
	echo -n ${TLVERSION} > ${APPNAME}-${VERSION}/TL_VERSION
	echo -n ${VERSION} > ${APPNAME}-${VERSION}/VERSION
	tar cfz ${APPNAME}-${VERSION}.tar.gz ${APPNAME}-${VERSION}
	rm -rf ${APPNAME}-${VERSION}

clean:
	rm -f $(APPNAME) $(tl_OBJ)

.PHONY: all clean install
