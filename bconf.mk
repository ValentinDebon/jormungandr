CFLAGS+=-std=c2x
CPPFLAGS+=-D_GNU_SOURCE -I$(srcdir)/include

orm-libs-objs:= \
	lib/data.o \
	lib/sandbox.o \
	lib/workdir.o

orm-objs:=src/orm.o \
	src/common/cmdpath.o

lndworm-objs:=src/lndworm.o \
	src/common/bsysexec.o \
	src/common/cmdpath.o \
	src/common/extract.o \
	src/common/isdir.o

gitworm-objs:=src/gitworm.o \
	src/common/bsysexec.o \
	src/common/extract.o \
	src/common/isdir.o

src/orm.o src/lndworm.o: CPPFLAGS+= \
	-DCONFIG_DEFAULT_SRCDIR_COMMAND='"$(CONFIG_DEFAULT_SRCDIR_COMMAND)"'

src/lndworm.o: CPPFLAGS+= \
	-DCONFIG_ARCHIVE_OUTPUT_BLOCK_SIZE='$(CONFIG_ARCHIVE_OUTPUT_BLOCK_SIZE)'

src/gitworm.o: CPPFLAGS+= \
	-DCONFIG_DEFAULT_GIT_EXEC_PATH='"$(CONFIG_DEFAULT_GIT_EXEC_PATH)"'

src/common/extract.o: CPPFLAGS+= \
	-DCONFIG_ARCHIVE_INPUT_BLOCK_SIZE='$(CONFIG_ARCHIVE_INPUT_BLOCK_SIZE)'

orm-libs:=liborm.a
ifneq ($(ld-so),a)
orm-libs+=liborm.$(ld-so)
endif

$(orm-libs): $(orm-libs-objs)
orm: $(orm-objs) liborm.$(ld-so)
lndworm: $(lndworm-objs) liborm.$(ld-so)
gitworm: $(gitworm-objs) liborm.$(ld-so)

libarchive-CPPFLAGS:=$(shell pkg-config --cflags-only-I libarchive)
libarchive-CFLAGS:=$(shell pkg-config --cflags-only-other libarchive)
libarchive-LDFLAGS:=$(shell pkg-config --libs-only-L libarchive)
libarchive-LDLIBS:=$(shell pkg-config --libs-only-l libarchive)

lndworm gitworm: CPPFLAGS+=$(libarchive-CPPFLAGS)
lndworm gitworm: CFLAGS+=$(libarchive-CFLAGS)
lndworm gitworm: LDFLAGS+=$(libarchive-LDFLAGS)
lndworm gitworm: LDLIBS+=$(libarchive-LDLIBS)

host-bin+=orm lndworm gitworm
host-lib+=$(orm-libs)
clean-up+=$(host-bin) $(host-lib) $(orm-libs-objs) $(orm-objs) $(lndworm-objs) $(gitworm-objs)
