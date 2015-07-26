#include "config.h"
#include "json_compatibility.h"
#include <string.h>

#ifndef HAVE_JSON_OBJECT_OBJECT_GET_EX
int json_object_object_get_ex(struct json_object* obj,
							  const char *key,
							  struct json_object **value) {
	json_object* tmp = NULL;
	tmp = json_object_object_get(obj, key);
	if (value != NULL) {
		*value = tmp;
	}
	return tmp != NULL;
}
#endif

#ifndef HAVE_JSON_OBJECT_GET_STRING_LEN
int json_object_get_string_len(struct json_object *obj) {
	if (obj == NULL) return 0;
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

#ifndef HAVE_JSON_TYPE_TO_NAME
const char* json_type_to_name(int t) {
	switch(t) {
	case json_type_null:
		return "null";
		break;
	case json_type_boolean:
		return "boolean";
		break;
	case json_type_double:
		return "double";
		break;
	case json_type_int:
		return "int";
		break;
	case json_type_object:
		return "object";
		break;
	case json_type_array:
		return "array";
		break;
	case json_type_string:
		return "string";
		break;
	default:
		return "UNKNOWN TYPE";
		break;
	}
}
#endif
