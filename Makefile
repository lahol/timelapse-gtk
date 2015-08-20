CC ?= gcc
PKG_CONFIG := pkg-config

CFLAGS ?= -Wall -g
INCLUDES := `$(PKG_CONFIG) --cflags glib-2.0 gtk+-3.0 gstreamer-0.10 gdk-3.0 gstreamer-interfaces-0.10` `imlib2-config --cflags`
LDFLAGS ?= 
LIBS := `$(PKG_CONFIG) --libs glib-2.0 gtk+-3.0 gstreamer-0.10 gdk-3.0 gstreamer-interfaces-0.10` `imlib2-config --libs`

TLVERSION := '$(shell [ -f TL_VERSION ] && cat TL_VERSION)'
VERSION := '$(shell [ -f VERSION ] && cat VERSION)'
ifeq ('',$(TLVERSION))
TLVERSION := '$(shell git describe --tags --always) ($(shell git log --pretty=format:%cd --date=short -n1), branch \"$(shell git describe --tags --always --all | sed s:heads/::)\")'
VERSION := $(shell git describe --tags --abbrev=0)
endif

APPNAME := timelapse-gtk
PREFIX ?= /usr
LOCALEDIR ?= $(PREFIX)/share/locale

tl_SRC := $(wildcard *.c)
tl_OBJ := $(tl_SRC:.c=.o)
tl_HEADERS := $(wildcard *.h)

CFLAGS += -DTLVERSION=\"${TLVERSION}\"
CFLAGS += -DAPPNAME=\"${APPNAME}\"
CFLAGS += -DLOCALEDIR=\"${LOCALEDIR}\"

all: $(APPNAME)

$(APPNAME): $(tl_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

%.o: %.c $(tl_HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

locales-prepare: $(tl_SRC)
	mkdir -p translations
	xgettext --keyword=_ -d $(APPNAME) -s -o translations/$(APPNAME).pot $(tl_SRC)

install: $(APPNAME) install-locales
	install $(APPNAME) $(PREFIX)/bin
	install $(APPNAME).desktop $(PREFIX)/share/applications

# FIXME: make this more general (Makefile in subdir)
install-locales:
	install translations/de/LC_MESSAGES/$(APPNAME).mo $(LOCALEDIR)/de/LC_MESSAGES

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

.PHONY: all clean install locales-prepare install-locales
