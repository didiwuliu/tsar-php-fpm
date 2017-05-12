CFLAGS = -Wall -fPIC --shared -g -O2
CC = gcc
INCLUDE_DIR = /usr/local/tsar/devel
LINK = $(CC) -I$(INCLUDE_DIR) $(CFLAGS)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LINK += -Wl,-undefined -Wl,dynamic_lookup
endif


OBJS =  mod_php_fpm.so

all: $(OBJS)

$(OBJS): %.so: %.c
	$(LINK) $< -o $@
clean:
	rm -f *.so;
install:
	mkdir -p ${DESTDIR}/etc/tsar/conf.d/
	mkdir -p ${DESTDIR}/usr/local/tsar/modules/
	cp ./mod_php_fpm.so ${DESTDIR}/usr/local/tsar/modules/
	cp ./mod_php_fpm.conf ${DESTDIR}/etc/tsar/conf.d/php_fpm.conf
uninstall:
	rm ${DESTDIR}/usr/local/tsar/modules/mod_php_fpm.so
	rm ${DESTDIR}/etc/tsar/conf.d/php_fpm.conf
