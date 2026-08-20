#include "config.h"
#include "liblognorm.h"

#include <stdio.h>

int
main(void)
{
	static const char rulebase[] =
		"version=2\n"
		"rule=:abc\n";
	static const char input[] = {'a', 'b', 'c', '\0'};
	struct json_object *json = NULL;
	ln_ctx ctx = ln_initCtx();
	int r;
	int ret = 1;

	if(ctx == NULL)
		return ret;

	r = ln_loadSamplesFromString(ctx, rulebase);
	if(r != 0)
		goto done;

	r = ln_normalize(ctx, input, sizeof(input), &json);
	if(r == 0) {
		fprintf(stderr, "literal parser matched input with trailing embedded NUL\n");
		goto done;
	}

	ret = 0;

done:
	if(json != NULL)
		json_object_put(json);
	ln_exitCtx(ctx);
	return ret;
}
