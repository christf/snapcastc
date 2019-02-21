#pragma once
#include "vector.h"

#include <json-c/json.h>
#include <stdbool.h>

typedef struct {
	enum json_type type;

	char *name;
	union {
		char *string;
		int number;
		double dnumber;
		bool bvalue;
		char *json_string; // this should really be a json_object. However since son-c 0.12 does not support deep_copy() yet, serialize / deserialize for compatibility
	} value;
} parameter;

typedef struct {
	char *method;
	VECTOR(parameter) parameters;
} jsonrpc_notification;

typedef struct {
	int id;
	char *method;
	VECTOR(parameter) parameters;
} jsonrpc_request;

typedef struct {
	int code;
	char *message;
	VECTOR(parameter) parameters;
} jsonrpc_error;

typedef struct {
	int id;
	char *result;
	jsonrpc_error *error;
} jsonrpc_response;

void jsonrpc_free_members(jsonrpc_request *req);
bool jsonrpc_parse_string(jsonrpc_request *result, const char *line);
