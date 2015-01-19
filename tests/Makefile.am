check_PROGRAMS = user_test
user_test_SOURCES = user_test.c
user_test_CPPFLAGS = $(LIBLOGNORM_CFLAGS) $(JSON_C_CFLAGS) $(LIBESTR_CFLAGS)
user_test_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_LIBS) $(LIBESTR_LIBS)
user_test_LDFLAGS = -no-install

TESTS = \
	user_test \
	field_tokenized.sh \
	field_tokenized_with_invalid_ruledef.sh

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

EXTRA_DIST = exec.sh

# tests
EXTRA_DIST += \
  field_tokenized.sh \
	field_tokenized_with_invalid_ruledef.sh \
	field_regex_default_group_parse_and_return.sh \
	field_regex_invalid_args.sh \
	field_regex_with_consume_group.sh \
	field_regex_with_consume_group_and_return_group.sh \
	field_regex_with_negation.sh \
	field_tokenized_with_regex.sh \
	field_regex_while_regex_support_is_disabled.sh
