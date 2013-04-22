# Uncomment for debugging
DEBUG = -g
PTHREADS_CFLAGS = -pthread

#CFLAGS += $(DEBUG)

# we need to clean the normalizer up once we have reached a decent
# milestone (latest at initial release!)
bin_PROGRAMS = lognormalizer
lognormalizer_SOURCES = lognormalizer.c
lognormalizer_CPPFLAGS =  -I$(top_srcdir) $(LIBEE_CFLAGS) $(LIBESTR_CFLAGS)
lognormalizer_LDADD = $(LIBEE_LIBS) $(LIBLOGNORM_LIBS) $(LIBESTR_LIBS)
lognormalizer_DEPENDENCIES = liblognorm.la

lib_LTLIBRARIES = liblognorm.la

liblognorm_la_SOURCES = \
	liblognorm.c \
	ptree.c \
	annot.c \
	samp.c \
	lognorm.c

liblognorm_la_CPPFLAGS = $(LIBEE_CFLAGS) $(LIBESTR_CFLAGS)
liblognorm_la_LIBADD = $(rt_libs) $(LIBEE_LIBS) $(LIBESTR_LIBS) -lestr
liblognorm_la_LDFLAGS = -version-info 0:0:0

EXTRA_DIST = \
	internal.h \
	liblognorm.h \
	lognorm.h \
	ptree.h \
	annot.h \
	samp.h

include_HEADERS = liblognorm.h samp.h lognorm.h ptree.h annot.h
