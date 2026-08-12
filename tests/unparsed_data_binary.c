#include "config.h"
#include <string.h>

#include "liblognorm.h"

static int
has_string_field(struct json_object *json, const char *name, const char *expected,
		const size_t expected_len)
{
	struct json_object *value;

	if(!json_object_object_get_ex(json, name, &value))
		return 0;
	if(json_object_get_type(value) != json_type_string)
		return 0;
	if((size_t) json_object_get_string_len(value) != expected_len)
		return 0;
	return memcmp(json_object_get_string(value), expected, expected_len) == 0;
}

static int
run_case(const char *rulebase, const unsigned opts, const char *message,
		const size_t message_len, const char *expected_unparsed,
		const size_t expected_unparsed_len, const char *expected_binary,
		const size_t expected_binary_len)
{
	ln_ctx ctx = NULL;
	struct json_object *json = NULL;
	int ret = 1;

	if((ctx = ln_initCtx()) == NULL)
		goto done;
	if(opts != 0)
		ln_setCtxOpts(ctx, opts);
	if(ln_loadSamplesFromString(ctx, rulebase) != 0)
		goto done;
	ln_normalize(ctx, message, message_len, &json);
	if(json == NULL)
		goto done;
	if(!has_string_field(json, "unparsed-data", expected_unparsed,
			expected_unparsed_len))
		goto done;
	if(expected_binary == NULL) {
		struct json_object *value;
		if(json_object_object_get_ex(json, "unparsed-data-binary", &value))
			goto done;
	} else if(!has_string_field(json, "unparsed-data-binary", expected_binary,
			expected_binary_len)) {
		goto done;
	}

	ret = 0;
done:
	if(json != NULL)
		json_object_put(json);
	if(ctx != NULL)
		ln_exitCtx(ctx);
	return ret;
}

int
main(void)
{
	static const char v2_rulebase[] =
		"version=2\n"
		"rule=:prefix%value:number%\n";
	static const char v1_rulebase[] =
		"rule=:prefix%value:number%\n";
	static const char control_message[] = "prefix\x01tail";
	static const char printable_message[] = "prefix tail";
	static const char utf8_message[] = "prefix \xc3\xa4";
	int ret = 1;

	if(run_case(v2_rulebase, 0, control_message, sizeof(control_message) - 1,
			control_message + 6, sizeof(control_message) - 7, "017461696c", 10) != 0)
		goto done;
	if(run_case(v1_rulebase, 0, control_message, sizeof(control_message) - 1,
			control_message + 6, sizeof(control_message) - 7, "017461696c", 10) != 0)
		goto done;
	if(run_case(v2_rulebase, 0, printable_message, sizeof(printable_message) - 1,
			printable_message + 6, sizeof(printable_message) - 7, NULL, 0) != 0)
		goto done;
	if(run_case(v2_rulebase, 0, utf8_message, sizeof(utf8_message) - 1,
			utf8_message + 6, sizeof(utf8_message) - 7, NULL, 0) != 0)
		goto done;
	if(run_case(v2_rulebase, LN_CTXOPT_NO_UNPARSED_DATA_BINARY, control_message,
			sizeof(control_message) - 1, control_message + 6,
			sizeof(control_message) - 7, NULL, 0) != 0)
		goto done;

	ret = 0;
done:
	return ret;
}
