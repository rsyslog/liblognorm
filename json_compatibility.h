#include <json.h>

#ifndef HAVE_JSON_OBJECT_OBJECT_GET_EX
int json_object_object_get_ex(struct json_object* obj,
							  const char *key,
							  struct json_object **value);
#endif

#ifndef HAVE_JSON_OBJECT_GET_STRING_LEN
int json_object_get_string_len(struct json_object *obj);
#endif
