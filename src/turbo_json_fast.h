/**
 * @file turbo_json_fast.h
 * @brief Fast JSON serialization declarations
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_JSON_FAST_H_INCLUDED
#define	LIBLOGNORM_TURBO_JSON_FAST_H_INCLUDED

#include "turbo_result_fast.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Estimate JSON output size.
 */
size_t ln_fast_json_estimate(const ln_fast_result_t *r);

/**
 * @brief Serialize to JSON string.
 *
 * Creates nested objects from dotted field names:
 * "timestamp_netscaler.day" -> {"timestamp_netscaler": {"day": ...}}
 *
 * @param r      Result to serialize
 * @param buf    Output buffer
 * @param buflen Buffer size
 * @param outlen Receives actual length (may be NULL)
 * @return 0 on success, -1 if buffer too small
 */
int ln_fast_to_json(const ln_fast_result_t *r,
					char *buf, size_t buflen, size_t *outlen);

/**
 * @brief Allocating version.
 */
int ln_fast_to_json_alloc(const ln_fast_result_t *r,
						  char **json_str, size_t *json_len);

#ifdef __cplusplus
}
#endif

#endif /* LIBLOGNORM_TURBO_JSON_FAST_H_INCLUDED */
