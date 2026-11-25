CROSS   ?= aarch64-linux-gnu-
CC       = $(CROSS)gcc
STRIP    = $(CROSS)strip
CFLAGS   = -Os -Wall -pipe -static
SRC      = main.c config.c backend_env.c backend_mtd.c \
           env_model.c cmd_printenv.c cmd_setenv.c crc32.c
HDR      = config.h backend.h env_model.h cmd_printenv.h \
           cmd_setenv.h crc32.h

all: fw_setenv fw_printenv envtool

fw_setenv: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)
	$(STRIP) $@

fw_printenv: fw_setenv
	ln -sf fw_setenv fw_printenv

envtool: fw_setenv
	ln -sf fw_setenv envtool

clean:
	rm -f fw_setenv fw_printenv envtool
