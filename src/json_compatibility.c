#include "config.h"
#include "json_compatibility.h"
#include <string.h>

#ifndef HAVE_JSON_OBJECT_OBJECT_GET_EX
int json_object_object_get_ex(struct json_object* obj,
							  const char *key,
							  struct json_object **value) {
	*value = json_object_object_get(obj, key);
	return *value != NULL;
}
#endif

#ifndef HAVE_JSON_OBJECT_GET_STRING_LEN
int json_object_get_string_len(struct json_object *obj) {
	const char* str = json_object_get_string(obj);
	return strlen(str);
}
#endif

#ifndef HAVE_JSON_OBJECT_GET_INT64
int64_t json_object_get_int64(struct json_object *jso) {
	int64_t val = json_object_get_int(jso);
	return val;
}
#endif

#ifndef HAVE_JSON_OBJECT_NEW_INT64
struct json_object* json_object_new_int64(int64_t i) {
	return json_object_new_int((int) i);
}
#endif
