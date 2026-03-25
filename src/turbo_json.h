/**
 * @file turbo_json.h
 * @brief JSON serialization for turbo results
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_JSON_H_INCLUDED
#define	LIBLOGNORM_TURBO_JSON_H_INCLUDED

#include "config.h"

#ifdef ENABLE_TURBO

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
 * @brief Serialize result to JSON string.
 *
 * @param r       Result to serialize
 * @param buf     Output buffer
 * @param buflen  Buffer size
 * @param outlen  Receives actual length
 * @return 0 on success, -1 on error
 */
int ln_fast_to_json(const ln_fast_result_t *r,
					char *buf, size_t buflen, size_t *outlen);

/**
 * @brief Serialize result to allocated JSON string.
 *
 * @param r        Result to serialize
 * @param json_str Receives allocated string (caller must free)
 * @param json_len Receives string length
 * @return 0 on success, -1 on error
 */
int ln_fast_to_json_alloc(const ln_fast_result_t *r,
						  char **json_str, size_t *json_len);

#ifdef __cplusplus
}
#endif

#endif /* ENABLE_TURBO */

#endif /* LIBLOGNORM_TURBO_JSON_H_INCLUDED */
