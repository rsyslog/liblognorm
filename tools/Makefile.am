bin_PROGRAMS = preprocess
preprocess_SOURCES = preprocess.c syntaxes.c
preprocess_CPPFLAGS =  -I$(top_srcdir)/src $(JSON_C_CFLAGS)
preprocess_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_LIBS)
lognormalizer_DEPENDENCIES = ../src/liblognorm.la

#EXTRA_DIST = 
#include_HEADERS=
