# Uncomment for debugging
DEBUG = -g
PTHREADS_CFLAGS = -pthread

#CFLAGS += $(DEBUG)

# we need to clean the normalizer up once we have reached a decent
# milestone (latest at initial release!)
bin_PROGRAMS = lognormalizer
lognormalizer_SOURCES = lognormalizer.c
lognormalizer_CPPFLAGS =  -I$(top_srcdir) $(WARN_CFLAGS) $(JSON_C_CFLAGS) $(LIBESTR_CFLAGS)
lognormalizer_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_LIBS) $(LIBESTR_LIBS) ../compat/compat.la 
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
	pdag.c \
	pdag.h \
	annot.c \
	annot.h \
	samp.c \
	samp.h \
	lognorm.c \
	lognorm.h \
	parser.c \
	parser.h \
	enc_syslog.c \
	enc_csv.c \
	enc_xml.c \
	enc.h \
	lognorm-features.h \
	helpers.h \
	internal.h

# Users violently requested that v2 shall be able to understand v1
# rulebases. As both are very very different, we now include the
# full v1 engine for this purpose. This here is what does this.
# see also: https://github.com/rsyslog/liblognorm/issues/103
liblognorm_la_SOURCES += \
	v1_liblognorm.c v1_liblognorm.h \
	v1_parser.c v1_parser.h \
	v1_ptree.c v1_ptree.h \
	v1_samp.c v1_samp.h

liblognorm_la_CPPFLAGS = $(JSON_C_CFLAGS) $(WARN_CFLAGS) $(LIBESTR_CFLAGS) $(PCRE_CFLAGS)
liblognorm_la_LIBADD = $(rt_libs) $(JSON_C_LIBS) $(LIBESTR_LIBS) $(PCRE_LIBS) -lestr
# info on version-info:
# http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
# Note: v2 now starts at version 5, as v1 previously also had 4
# 2.1.0: +1 current/+age for the new public Turbo API (lognorm-turbo.h);
# soname stays liblognorm.so.5 (current-age), interface revision bumped.
liblognorm_la_LDFLAGS = -version-info 7:0:2

# Public API.  lognorm-turbo.h is installed unconditionally — it provides no-op
# stubs when Turbo is disabled, so consumers can include it against any build.
# Only the implementation sources stay behind ENABLE_TURBO (below).
include_HEADERS = liblognorm.h lognorm-features.h lognorm-turbo.h

# TurboVM bytecode engine (optional, SIMD-accelerated log parsing)
if ENABLE_TURBO
liblognorm_la_SOURCES += \
	turbo.c \
	turbo.h \
	turbo_vm.c \
	turbo_vm.h \
	turbo_vm_opt.h \
	turbo_opcode.h \
	turbo_simd.c \
	turbo_simd.h \
	turbo_arena.c \
	turbo_arena.h \
	turbo_result.c \
	turbo_result.h \
	turbo_result_fast.c \
	turbo_result_fast.h \
	turbo_json.h \
	turbo_json_fast.c \
	turbo_json_fast.h \
	turbo_json_impl.c \
	turbo_snapshot.c \
	turbo_snapshot.h
liblognorm_la_CPPFLAGS += $(TURBO_CFLAGS)
endif
