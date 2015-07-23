check_PROGRAMS = json_eq user_test
json_eq_self_sources = json_eq.c
json_eq_SOURCES = $(json_eq_self_sources) $(top_srcdir)/src/json_compatibility.c
json_eq_CPPFLAGS =  $(JSON_C_CFLAGS) -I$(top_srcdir)/src
json_eq_LDADD = $(JSON_C_LIBS)
json_eq_LDFLAGS = -no-install

user_test_SOURCES = user_test.c
user_test_CPPFLAGS = $(LIBLOGNORM_CFLAGS) $(JSON_C_CFLAGS) $(LIBESTR_CFLAGS)
user_test_LDADD = $(JSON_C_LIBS) $(LIBLOGNORM_LIBS) $(LIBESTR_LIBS) ../compat/compat.la 
user_test_LDFLAGS = -no-install

TESTS_SHELLSCRIPTS = \
	usrdef_simple.sh \
	seq_simple.sh \
	alternative_simple.sh \
	repeat_simple.sh \
	parser_whitespace.sh \
	parser_whitespace_jsoncnf.sh \
	parser_LF.sh \
	parser_LF_jsoncnf.sh \
	field_hexnumber.sh \
	field_hexnumber_jsoncnf.sh \
	field_hexnumber_range.sh \
	field_hexnumber_range_jsoncnf.sh \
	field_mac48.sh \
	field_mac48_jsoncnf.sh \
	field_name_value.sh \
	field_name_value_jsoncnf.sh \
	field_kernel_timestamp.sh \
	field_kernel_timestamp_jsoncnf.sh \
	field_whitespace.sh \
	field_whitespace_jsoncnf.sh \
	field_rest.sh \
	field_rest_jsoncnf.sh \
	field_json.sh \
	field_json_jsoncnf.sh \
	field_cee-syslog.sh \
	field_cee-syslog_jsoncnf.sh \
	field_ipv6.sh \
	field_ipv6_jsoncnf.sh \
	field_v2-iptables.sh \
	field_v2-iptables_jsoncnf.sh \
	field_cef.sh \
	field_cef_jsoncnf.sh \
	field_checkpoint-lea.sh \
	field_checkpoint-lea_jsoncnf.sh \
	field_duration.sh \
	field_duration_jsoncnf.sh \
        field_float.sh \ 
        field_float_jsoncnf.sh 
# removed because test is not valid.
# more info: https://github.com/rsyslog/liblognorm/issues/98
# note that probably the other currently disable *_invalid_*.sh
# tests are also affected.
	field_float_with_invalid_ruledef.sh
# there seems to be a problem with some format in cisco-interface-spec
# Probably this was just not seen in v1, because of some impreciseness
# in the ptree normalizer. Pushing this back until v2 implementation is
# further.
	field_cisco-interface-spec.sh 
# the other tests are currently disabled because we do not
# yet support the required parsers
	field_tokenized.sh \
	field_tokenized_with_invalid_ruledef.sh \
	field_recursive.sh \
	field_tokenized_recursive.sh \
	field_interpret.sh \
	field_interpret_with_invalid_ruledef.sh \
	field_descent.sh \
	field_descent_with_invalid_ruledef.sh \
	field_suffixed.sh \
	field_suffixed_with_invalid_ruledef.sh

TESTS = \
	user_test \
	$(TESTS_SHELLSCRIPTS)

REGEXP_TESTS = \
	field_regex_default_group_parse_and_return.sh \
	field_regex_invalid_args.sh \
	field_regex_with_consume_group.sh \
	field_regex_with_consume_group_and_return_group.sh \
	field_regex_with_negation.sh \
	field_tokenized_with_regex.sh \
	field_regex_while_regex_support_is_disabled.sh

EXTRA_DIST = exec.sh \
	$(TESTS_SHELLSCRIPTS) \
	$(REGEXP_TESTS) \
	$(json_eq_self_sources) \
	$(user_test_SOURCES)

if ENABLE_REGEXP
TESTS += $(REGEXP_TESTS)
endif
