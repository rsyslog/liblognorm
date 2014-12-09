check_PROGRAMS = json_eq
json_eq_SOURCES = json_eq.c $(top_srcdir)/src/json_compatibility.c
json_eq_CPPFLAGS =  $(JSON_C_CFLAGS) -I$(top_srcdir)/src
json_eq_LDADD = $(JSON_C_LIBS)
json_eq_LDFLAGS = -no-install

TESTS = \
	field_tokenized.sh \
	field_tokenized_with_invalid_ruledef.sh \
	field_recursive.sh \
	field_tokenized_recursive.sh

if ENABLE_REGEXP
TESTS +=  \
	field_regex_default_group_parse_and_return.sh \
	field_regex_invalid_args.sh \
	field_regex_with_consume_group.sh \
	field_regex_with_consume_group_and_return_group.sh \
	field_regex_with_negation.sh \
	field_tokenized_with_regex.sh \
	field_regex_while_regex_support_is_disabled.sh
endif

