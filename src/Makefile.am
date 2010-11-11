# Uncomment for debugging
DEBUG = -g
PTHREADS_CFLAGS = -pthread

#CFLAGS += $(DEBUG)

# we need to clean the sample up once we have reached a decent
# milestone (latest at initial release!)
bin_PROGRAMS = sample
sample_SOURCES = sample.c
sample_CPPFLAGS =  -I$(top_srcdir) $(LIBEE_CFLAGS) $(LIBESTR_CFLAGS)
sample_LDADD = $(LIBEE_LIBS) $(LIBLOGNORM_LIBS) $(LIBESTR_LIBS)

lib_LTLIBRARIES = liblognorm.la

liblognorm_la_SOURCES = \
	liblognorm.c \
	ptree.c \
	samp.c \
	lognorm.c

liblognorm_la_CPPFLAGS = $(LIBEE_CFLAGS) $(LIBESTR_CFLAGS)
liblognorm_la_LIBADD = $(rt_libs) $(LIBEE_LIBS) $(LIBESTR_LIBS) -lestr
liblognorm_la_LDFLAGS = -version-info 0:0:0

include_HEADERS = liblognorm.h samp.h lognorm.h ptree.h
