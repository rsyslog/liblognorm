/**
 * @file turbo_result.h
 * @brief Compatibility wrapper for result types
 *//*
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * Released under ASL 2.0.
 */
#ifndef LIBLOGNORM_TURBO_RESULT_H_INCLUDED
#define	LIBLOGNORM_TURBO_RESULT_H_INCLUDED

#include "turbo_result_fast.h"

/* For backward compatibility, alias the types */
typedef ln_fast_result_t ln_result_t;
typedef ln_fast_field_t  ln_field_t;

/* Alias functions */
#define ln_result_init(r, a)          ln_fast_result_init(r, a)
#define ln_result_clear(r)            ln_fast_result_clear(r)
#define ln_result_add_string(r,n,v,l) ln_fast_add_string_static(r, n, strlen(n), v, l)
#define ln_result_add_int(r,n,v)      ln_fast_add_int_static(r, n, strlen(n), v)
#define ln_result_add_tag(r,t)        ln_fast_add_tag(r, t)

#endif /* LIBLOGNORM_TURBO_RESULT_H_INCLUDED */
