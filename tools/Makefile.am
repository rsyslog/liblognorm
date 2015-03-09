bin_PROGRAMS = preprocess slsa
preprocess_SOURCES = preprocess.c syntaxes.c
preprocess_CPPFLAGS =  -I$(top_srcdir)/src $(JSON_C_CFLAGS)
preprocess_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_LIBS)
preprocess_DEPENDENCIES = ../src/liblognorm.la

slsa_SOURCES = slsa.c syntaxes.c
slsa_CPPFLAGS =  -I$(top_srcdir)/src $(JSON_C_CFLAGS)
slsa_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_LIBS)
slsa_DEPENDENCIES = ../src/liblognorm.la

EXTRA_DIST=logrecord.h
#include_HEADERS=
