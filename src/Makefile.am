# Uncomment for debugging
DEBUG = -g
PTHREADS_CFLAGS = -pthread

#CFLAGS += $(DEBUG)

lib_LTLIBRARIES = liblognorm.la

liblognorm_la_SOURCES = \
	liblognorm.c \
	lognorm.c

liblognorm_la_CPPFLAGS = $(PTHREADS_CFLAGS)
liblognorm_la_LIBADD = $(rt_libs)
liblognorm_la_LDFLAGS = -version-info 0:0:0

include_HEADERS = liblognorm.h
