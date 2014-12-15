# Uncomment for debugging
DEBUG = -g
PTHREADS_CFLAGS = -pthread

#CFLAGS += $(DEBUG)

# we need to clean the normalizer up once we have reached a decent
# milestone (latest at initial release!)
bin_PROGRAMS = lognormalizer
lognormalizer_SOURCES = lognormalizer.c
lognormalizer_CPPFLAGS =  -I$(top_srcdir) $(JSON_C_CFLAGS) $(LIBESTR_CFLAGS)
lognormalizer_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_LIBS) $(LIBESTR_LIBS) 
lognormalizer_DEPENDENCIES = liblognorm.la

check_PROGRAMS = ln_test
ln_test_SOURCES = $(lognormalizer_SOURCES)
ln_test_CPPFLAGS = $(lognormalizer_CPPFLAGS)
ln_test_LDADD = $(lognormalizer_LDADD)
ln_test_DEPENDENCIES = $(lognormalizer_DEPENDENCIES)
ln_test_LDFLAGS = -no-install

lib_LTLIBRARIES = liblognorm.la

liblognorm_la_SOURCES = \
	liblognorm.c \
	ptree.c \
	annot.c \
	samp.c \
	lognorm.c \
	parser.c \
	enc_syslog.c \
	enc_csv.c \
	enc_xml.c \
	json_compatibility.c

liblognorm_la_CPPFLAGS = $(JSON_C_CFLAGS) $(LIBESTR_CFLAGS)
liblognorm_la_LIBADD = $(rt_libs) $(JSON_C_LIBS) $(LIBESTR_LIBS) -lestr
liblognorm_la_LDFLAGS = -version-info 1:0:0

EXTRA_DIST = \
	internal.h \
	liblognorm.h \
	lognorm.h \
	ptree.h \
	annot.h \
	samp.h \
	enc.h \
	parser.h

include_HEADERS = liblognorm.h samp.h lognorm.h ptree.h annot.h enc.h parser.h lognorm-features.h
