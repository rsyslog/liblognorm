# Uncomment for debugging
DEBUG = -g
PTHREADS_CFLAGS = -pthread

#CFLAGS += $(DEBUG)

# we need to clean the normalizer up once we have reached a decent
# milestone (latest at initial release!)
bin_PROGRAMS = lognormalizer
lognormalizer_SOURCES = lognormalizer.c
lognormalizer_CPPFLAGS =  -I$(top_srcdir) $(JSON_C_CFLAGS) $(LIBESTR_CFLAGS)
lognormalizer_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_J_LIBS) $(LIBESTR_LIBS) 
lognormalizer_DEPENDENCIES = liblognorm_j.la

lib_LTLIBRARIES = liblognorm_j.la

liblognorm_j_la_SOURCES = \
	liblognorm.c \
	ptree.c \
	annot.c \
	samp.c \
	lognorm.c \
	parser.c \
	enc_syslog.c \
	enc_csv.c \
	enc_xml.c

liblognorm_j_la_CPPFLAGS = $(JSON_C_CFLAGS) $(LIBESTR_CFLAGS)
liblognorm_j_la_LIBADD = $(rt_libs) $(JSON_C_LIBS) $(LIBESTR_LIBS) -lestr
liblognorm_j_la_LDFLAGS = -version-info 0:4:0

EXTRA_DIST = \
	internal.h \
	liblognorm.h \
	lognorm.h \
	ptree.h \
	annot.h \
	samp.h \
	parser.h

include_HEADERS = liblognorm.h samp.h lognorm.h ptree.h annot.h
