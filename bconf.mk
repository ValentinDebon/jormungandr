CFLAGS+=-std=c2x
CPPFLAGS+=-D_GNU_SOURCE -I$(srcdir)/include

orm-libs-objs:= \
	lib/data.o \
	lib/sandbox.o \
	lib/workdir.o
orm-objs:=src/orm.o

orm-libs:=liborm.a

ifneq ($(ld-so),a)
orm-libs+=liborm.$(ld-so)
endif

$(orm-libs): $(orm-libs-objs)
orm: $(orm-objs) liborm.$(ld-so)

host-bin+=orm
host-lib+=$(orm-libs)
clean-up+=$(host-bin) $(host-lib) $(orm-libs-objs) $(orm-objs)
