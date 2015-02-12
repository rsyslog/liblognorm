bin_PROGRAMS = preprocess
preprocess_SOURCES = preprocess.c syntaxes.c
preprocess_CPPFLAGS =  -I$(top_srcdir)/src $(JSON_C_CFLAGS)
preprocess_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_LIBS)
preprocess_DEPENDENCIES = ../src/liblognorm.la

EXTRA_DIST=logrecord.h
#include_HEADERS=
