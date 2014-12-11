#include <json.h>
#include <inttypes.h>

#ifndef HAVE_JSON_OBJECT_OBJECT_GET_EX
int json_object_object_get_ex(struct json_object* obj,
							  const char *key,
							  struct json_object **value);
#endif

#ifndef HAVE_JSON_OBJECT_GET_STRING_LEN
int json_object_get_string_len(struct json_object *obj);
#endif

#ifndef HAVE_JSON_BOOL
typedef int json_bool;
#endif

#ifndef HAVE_JSON_OBJECT_GET_INT64
int64_t json_object_get_int64(struct json_object *jso);
#endif

#ifndef HAVE_JSON_OBJECT_NEW_INT64
struct json_object* json_object_new_int64(int64_t i);
#endif
