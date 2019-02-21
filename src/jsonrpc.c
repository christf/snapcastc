#include "jsonrpc.h"
#include "alloc.h"
#include "util.h"

#include <json-c/json.h>
#include <string.h>

void jsonrpc_free_members(jsonrpc_request *req) {
	free(req->method);

	for (int i = VECTOR_LEN(req->parameters) - 1; i > 0; --i) {
		parameter *p = &VECTOR_INDEX(req->parameters, i);

		free(p->name);
		if (p->type == json_type_string)
			free(p->value.string);
		if (p->type == json_type_object)
			free(p->value.json_string);
	}

	VECTOR_FREE(req->parameters);
}

void strcopy_from_json(char **dest, json_object *jobj) {
	// this will remove quotes from strings, make sure to free the result after use
	*dest = snap_alloc(strlen(json_object_to_json_string_ext(jobj, 0)) - 2);
	strncpy(*dest, &json_object_to_json_string_ext(jobj, 0)[1], strlen(json_object_to_json_string_ext(jobj, 0)) - 2);
}

bool jsonrpc_parse_string(jsonrpc_request *result, const char *line) {
	json_object *jobj = NULL;
	json_object *jsonrpc = NULL;
	json_object *method = NULL;
	json_object *params = NULL;
	json_object *id = NULL;

	if (!strlen(line))
		return false;

	log_error("parsing line: %s length %d\n", line, strnlen(line, 1024));
	jobj = json_tokener_parse(line);
	if (!jobj) {
		log_verbose("error parsing json %s\n", line);
		return false;
	}

	if (!json_object_object_get_ex(jobj, "jsonrpc", &jsonrpc) || !json_object_object_get_ex(jobj, "method", &method)) {
		log_verbose("invalid json-rpc structure: jsonrpc or method not found\n");
		json_object_put(jobj);
		json_object_put(jsonrpc);
		json_object_put(method);
		return false;
	}

	const char *version = json_object_to_json_string_ext(jsonrpc, 0);
	if (strncmp("\"2.0\"", version, 5)) {
		log_verbose("expecting json-rpc Version 2.0\n");
		json_object_put(jobj);
		return false;
	}

	strcopy_from_json(&result->method, method);

	log_debug("method: %s\n", result->method);
	if (json_object_object_get_ex(jobj, "id", &id)) {
		result->id = json_object_get_int(id);
	}
	log_debug("id: %d\n", result->id);
	VECTOR_INIT(result->parameters);

	if (json_object_object_get_ex(jobj, "params", &params)) {
		log_debug("params: %s\n", json_object_to_json_string_ext(params, 0));

		if (json_object_get_type(params) == json_type_object) {
			json_object_object_foreach(params, key, val) {
				parameter p;

				p.type = json_object_get_type(val);
				p.name = strdup(key);
				log_debug("key: %s ", key);
				switch (p.type) {
					case json_type_null:
						log_debug("found json_type_null - nothing to do.\n");
						break;
					case json_type_boolean:
						log_debug("found json_type_boolean\n");
						p.value.bvalue = json_object_get_boolean(val);
						break;
					case json_type_double:
						log_debug("found json_type_double\n");
						p.value.dnumber = json_object_get_double(val);
						break;
					case json_type_int:
						log_debug("found json_type_int\n");
						p.value.number = json_object_get_int(val);
						break;
					case json_type_object:
						log_error("found json_type_object %s\n", key);
						json_object *tmp = NULL;
						if (!json_object_object_get_ex(params, key, &tmp))
							log_error("could not get json object %s\n", key);
						p.value.json_string = strdup(json_object_to_json_string(tmp));
						break;
					case json_type_array:
						log_error("found json_type_array - NOT IMPLEMENTED. THIS SHOULD NOT HAPPEN.\n");
						break;
					case json_type_string:
						log_debug("found json_type_string\n");
						p.value.string = strdup(json_object_get_string(val));
						break;
				}
				VECTOR_ADD(result->parameters, p);
			}
		} else {
			log_error("Passing parameters as array is not implemented. Ignoring input.\n");
			jsonrpc_free_members(result);
			json_object_put(jobj);
			json_object_put(params);
			return false;
		}
	}

	log_debug("parsed json from input socket: \n---\n%s\n---\n",
		  json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY));

	json_object_put(jobj);

	log_debug("found %d parameters\n", VECTOR_LEN(result->parameters));

	return true;
}
