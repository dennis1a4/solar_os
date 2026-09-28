# Host-only build of the same pinned TLS sources/configuration as the firmware.
SOURCES := $(wildcard $(CRYPTO)/library/*.c)
OBJECTS := $(patsubst $(CRYPTO)/library/%.c,crypto-objects/%.o,$(SOURCES))
all: libcrypto-test.a
crypto-objects/%.o: $(CRYPTO)/library/%.c $(PORT)/ssh_compat/teensy_mbedtls_config.h
	@mkdir -p crypto-objects
	$(CC) -O1 -g -DSK_PLAYGROUND=1 -I$(CRYPTO)/include -I$(CRYPTO)/library -DMBEDTLS_CONFIG_FILE='"$(PORT)/ssh_compat/teensy_mbedtls_config.h"' -c $< -o $@
libcrypto-test.a: $(OBJECTS)
	$(AR) rcs $@ $^
