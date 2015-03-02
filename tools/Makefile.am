bin_PROGRAMS = preprocess experiment
preprocess_SOURCES = preprocess.c syntaxes.c
preprocess_CPPFLAGS =  -I$(top_srcdir)/src $(JSON_C_CFLAGS)
preprocess_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_LIBS)
preprocess_DEPENDENCIES = ../src/liblognorm.la

experiment_SOURCES = experiment.c syntaxes.c
experiment_CPPFLAGS =  -I$(top_srcdir)/src $(JSON_C_CFLAGS)
experiment_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_LIBS)
experiment_DEPENDENCIES = ../src/liblognorm.la

EXTRA_DIST=logrecord.h
#include_HEADERS=
