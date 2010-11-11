# Uncomment for debugging
DEBUG = -g
PTHREADS_CFLAGS = -pthread

#CFLAGS += $(DEBUG)

# we need to clean the sample up once we have reached a decent
# milestone (latest at initial release!)
sbin_PROGRAMS = sample
sample_SOURCES = sample.c \
	liblognorm.c lognorm.c samp.c ptree.c

lib_LTLIBRARIES = liblognorm.la

liblognorm_la_SOURCES = \
	liblognorm.c \
	ptree.c \
	samp.c \
	lognorm.c

liblognorm_la_CPPFLAGS = $(PTHREADS_CFLAGS)
liblognorm_la_LIBADD = $(rt_libs)
liblognorm_la_LDFLAGS = -version-info 0:0:0

include_HEADERS = liblognorm.h samp.h lognorm.h ptree.h
