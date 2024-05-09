CFLAGS+=-std=c2x
CPPFLAGS+=-D_GNU_SOURCE -I$(srcdir)/include

orm-libs-objs:= \
	lib/data.o \
	lib/sandbox.o \
	lib/workdir.o
orm-objs:=src/cmdpath.o src/orm.o
lndworm-objs:=src/cmdpath.o src/lndworm.o

src/orm.o src/lndworm.o: CPPFLAGS+= \
	-DCONFIG_DEFAULT_SRCDIR_COMMAND='"$(CONFIG_DEFAULT_SRCDIR_COMMAND)"'

src/lndworm.o: CPPFLAGS+= \
	-DCONFIG_LNDWORM_INPUT_BLOCK_SIZE='$(CONFIG_LNDWORM_INPUT_BLOCK_SIZE)' \
	-DCONFIG_LNDWORM_OUTPUT_BLOCK_SIZE='$(CONFIG_LNDWORM_OUTPUT_BLOCK_SIZE)'

orm-libs:=liborm.a
ifneq ($(ld-so),a)
orm-libs+=liborm.$(ld-so)
endif

$(orm-libs): $(orm-libs-objs)
orm: $(orm-objs) liborm.$(ld-so)
lndworm: $(lndworm-objs) liborm.$(ld-so)

libarchive-CPPFLAGS:=$(shell pkg-config --cflags-only-I libarchive)
libarchive-CFLAGS:=$(shell pkg-config --cflags-only-other libarchive)
libarchive-LDFLAGS:=$(shell pkg-config --libs-only-L libarchive)
libarchive-LDLIBS:=$(shell pkg-config --libs-only-l libarchive)

lndworm: CPPFLAGS+=$(libarchive-CPPFLAGS)
lndworm: CFLAGS+=$(libarchive-CFLAGS)
lndworm: LDFLAGS+=$(libarchive-LDFLAGS)
lndworm: LDLIBS+=$(libarchive-LDLIBS)

host-bin+=orm lndworm
host-lib+=$(orm-libs)
clean-up+=$(host-bin) $(host-lib) $(orm-libs-objs) $(orm-objs) $(lndworm-objs)
