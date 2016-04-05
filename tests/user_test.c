#include "config.h"
#include <json.h>
#include <liblognorm.h>
#include <string.h>

int main() {
	const char* str = "foo says hello!";
	json_object *obj, *from, *msg;
	obj = from = msg = NULL;
	ln_ctx ctx =  ln_initCtx();
	int ret = 1;

	ln_loadSample(ctx, "rule=:%from:word% says %msg:word%");
	if (ln_normalize(ctx, str, strlen(str), &obj) == 0) {

		from = json_object_object_get(obj, "from");
		msg = json_object_object_get(obj, "msg");

		ret = strcmp(json_object_get_string(from), "foo") ||
			strcmp(json_object_get_string(msg), "hello!");
	}

	if (obj != NULL) json_object_put(obj);
	ln_exitCtx(ctx);

	return ret;
}
