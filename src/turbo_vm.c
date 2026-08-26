/*
 * turbo_vm.c: Virtual machine for executing TurboVM bytecode
 *
 * Part of the TurboVM bytecode engine for high-performance log parsing.
 *
 * Copyright 2024-2026 by Advens and Jeremie Jourdin.
 * Copyright 2015-2026 by Rainer Gerhards and Adiscon GmbH.
 *
 * This file is part of liblognorm.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * A copy of the LGPL v2.1 can be found in the file "COPYING" in this distribution.
 */
/* config.h MUST precede the system headers: it defines _GNU_SOURCE, which is what
 * makes glibc's <stdlib.h> declare strtod_l (modern glibc has no <xlocale.h>).
 * FreeBSD/macOS declare strtod_l in <xlocale.h>, included below. */
#include "config.h"
#include "turbo_vm.h"
#include "turbo_vm_opt.h"
#include "turbo_simd.h"
/* enum FMT_MODE value FMT_AS_TIMESTAMP_UX_MS; kept in lockstep with parser.c. */
#define FMT_MODE_TIMESTAMP_UX_MS 3
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <locale.h>
#if defined(__has_include)
# if __has_include(<xlocale.h>)
#  include <xlocale.h>
# endif
#endif

/*============================================================================
 * Debug Tracing
 *============================================================================*/

#ifdef LN_VM_TRACE
static bool g_trace_enabled = true;
#define TRACE(...) do { if (g_trace_enabled) fprintf(stderr, __VA_ARGS__); } while(0)
#else
#define TRACE(...) ((void)0)
#endif

/*============================================================================
 * Helper macros
 *============================================================================*/

#define myisdigit(c) ((c) >= '0' && (c) <= '9')

/*============================================================================
 * Opcode Names
 *============================================================================*/

const char *
ln_opcode_name(ln_opcode_t op)
{
	switch (op) {
	case OP_HALT:           return "HALT";
	case OP_MATCH:          return "MATCH";
	case OP_JUMP:           return "JUMP";
	case OP_FORK:           return "FORK";
	case OP_FAIL:           return "FAIL";
	case OP_CALL:           return "CALL";
	case OP_RET:            return "RET";
	case OP_LITERAL:        return "LITERAL";
	case OP_LITERAL_EXT:    return "LITERAL_EXT";
	case OP_LITERAL_CI:     return "LITERAL_CI";
	case OP_CHAR:           return "CHAR";
	case OP_ANY:            return "ANY";
	case OP_CHARSET:        return "CHARSET";
	case OP_FIELD_WORD:     return "FIELD_WORD";
	case OP_FIELD_INT:      return "FIELD_INT";
	case OP_FIELD_UINT:     return "FIELD_UINT";
	case OP_FIELD_FLOAT:    return "FIELD_FLOAT";
	case OP_FIELD_IPV4:     return "FIELD_IPV4";
	case OP_FIELD_IPV6:     return "FIELD_IPV6";
	case OP_FIELD_HEX:      return "FIELD_HEX";
	case OP_FIELD_QUOTED:   return "FIELD_QUOTED";
	case OP_FIELD_CHAR_TO:  return "FIELD_CHAR_TO";
	case OP_FIELD_STR_TO:   return "FIELD_STR_TO";
	case OP_FIELD_REST:     return "FIELD_REST";
	case OP_FIELD_JSON:     return "FIELD_JSON";
	case OP_FIELD_MAC:      return "FIELD_MAC";
	case OP_FIELD_DATE:     return "FIELD_DATE";
	case OP_FIELD_REGEX:    return "FIELD_REGEX";
	case OP_FIELD_NAME_VALUE: return "FIELD_NAME_VALUE";
	case OP_SKIP_SPACE:     return "SKIP_SPACE";
	case OP_SKIP_SPACE1:    return "SKIP_SPACE1";
	case OP_SKIP_N:         return "SKIP_N";
	case OP_SKIP_TO:        return "SKIP_TO";
	case OP_SKIP_PAST:      return "SKIP_PAST";
	case OP_SKIP_LINE:      return "SKIP_LINE";
	case OP_TAG:            return "TAG";
	case OP_RULE_ID:        return "RULE_ID";
	case OP_STATIC_FIELD:   return "STATIC_FIELD";
	case OP_CTX_PUSH:       return "CTX_PUSH";
	case OP_CTX_POP:        return "CTX_POP";
	case OP_CTX_NEST:       return "CTX_NEST";
	case OP_CTX_UNNEST:     return "CTX_UNNEST";
	case OP_ASSERT_CHAR:    return "ASSERT_CHAR";
	case OP_ASSERT_END:     return "ASSERT_END";
	case OP_ASSERT_START:   return "ASSERT_START";
	case OP_SYSLOG_PRI:     return "SYSLOG_PRI";
	case OP_SYSLOG_TS:      return "SYSLOG_TS";
	case OP_CEF_HDR:        return "CEF_HDR";
	case OP_V2_IPTABLES:    return "V2_IPTABLES";
	case OP_CEE_SYSLOG:     return "CEE_SYSLOG";
	case OP_CHECKPOINT_LEA: return "CHECKPOINT_LEA";
	case OP_NOP:            return "NOP";
	case OP_DEBUG:          return "DEBUG";
	case OP_INVALID:        return "INVALID";
	default:                return "UNKNOWN";
	}
}

/**
 * @brief string-to: find the delimiter STRING, standard-parser semantics.
 *
 * Mirrors PARSER_Parse(StringTo) in parser.c exactly:
 *   - the scan starts one byte past the field start, so a delimiter sitting
 *     at offset 0 is skipped and the value is never empty;
 *   - the first occurrence wins and the delimiter itself is not consumed;
 *   - a delimiter shorter than two bytes never matches (the standard parser's
 *     inner loop cannot run for len < 2; replicated here so both engines
 *     agree, see doc/turbo.rst "Known differences").
 *
 * @return 0 and *out_len = value length on success, -1 on no match.
 */
static inline int
parse_string_to(const char *ip, size_t rem, const char *delim, size_t dlen,
				size_t *out_len)
{
	size_t i;

	if (dlen < 2 || rem <= dlen) return -1;

	for (i = 1; i + dlen <= rem; i++) {
		if (ip[i] == delim[0] && memcmp(ip + i, delim, dlen) == 0) {
			*out_len = i;
			return 0;
		}
	}
	return -1;
}

/*============================================================================
 * Disassembly
 *============================================================================*/

int
ln_instr_disasm(const ln_instr_t *inst, char *buf, size_t len)
{
	const char *name;
	int n;

	if (!inst || !buf || len == 0) return 0;

	name = ln_opcode_name(inst->op);
	n = 0;

	switch (inst->op) {
	case OP_LITERAL:
	case OP_LITERAL_CI:
		n = snprintf(buf, len, "%s \"%.*s\"", name, (int)inst->aux, inst->data.str);
		break;
	case OP_CHAR:
		if (isprint((unsigned char)inst->data.str[0])) {
			n = snprintf(buf, len, "%s '%c'", name, inst->data.str[0]);
		} else {
			n = snprintf(buf, len, "%s 0x%02x", name, (unsigned char)inst->data.str[0]);
		}
		break;
	case OP_JUMP:
	case OP_FORK:
	case OP_CALL:
		n = snprintf(buf, len, "%s %+d", name, inst->data.jump.offset);
		break;
	case OP_MATCH:
	case OP_TAG:
	case OP_RULE_ID:
	case OP_CTX_PUSH:
	case OP_CTX_NEST:
	case OP_FIELD_WORD:
	case OP_FIELD_INT:
	case OP_FIELD_UINT:
	case OP_FIELD_FLOAT:
	case OP_FIELD_IPV4:
	case OP_FIELD_IPV6:
	case OP_FIELD_HEX:
	case OP_FIELD_QUOTED:
	case OP_FIELD_REST:
	case OP_FIELD_JSON:
	case OP_FIELD_MAC:
	case OP_FIELD_DATE:
	case OP_V2_IPTABLES:
	case OP_CEE_SYSLOG:
		n = snprintf(buf, len, "%s \"%s\"", name, inst->data.str);
		break;
	case OP_STATIC_FIELD:
		if (inst->flags & LN_INSTR_F_KV_POOL) {
			n = snprintf(buf, len, "%s pool+%u=pool+%u", name,
						(unsigned)inst->data.kv_pool.key_off,
						(unsigned)inst->data.kv_pool.val_off);
		} else {
			n = snprintf(buf, len, "%s \"%s\"=\"%s\"", name,
						inst->data.kv.key, inst->data.kv.val);
		}
		break;
	case OP_FIELD_STR_TO:
		n = snprintf(buf, len, "%s \"%s\" delim=+%u/%u", name,
					inst->data.str_to.name,
					(unsigned)inst->data.str_to.delim_off, (unsigned)inst->aux);
		break;
	case OP_FIELD_CHAR_TO:
		if (isprint(inst->data.char_to.delim)) {
			n = snprintf(buf, len, "%s \"%s\" delim='%c'", name,
						inst->data.char_to.name, inst->data.char_to.delim);
		} else {
			n = snprintf(buf, len, "%s \"%s\" delim=0x%02x", name,
						inst->data.char_to.name, inst->data.char_to.delim);
		}
		break;
	case OP_CHECKPOINT_LEA:
		if (inst->data.char_to.delim) {
			n = snprintf(buf, len, "%s \"%s\" term='%c'", name,
						inst->data.char_to.name, inst->data.char_to.delim);
		} else {
			n = snprintf(buf, len, "%s \"%s\"", name, inst->data.char_to.name);
		}
		break;
	case OP_FIELD_NAME_VALUE:
		n = snprintf(buf, len, "%s \"%s\" sep=%s ass=%s", name,
					inst->data.char_to.name,
					inst->data.char_to.delim ? (char[]){inst->data.char_to.delim, 0} : "ws",
					inst->data.char_to.ass ? (char[]){inst->data.char_to.ass, 0} : "'='");
		break;
	case OP_SKIP_N:
		n = snprintf(buf, len, "%s %u", name, inst->aux);
		break;
	default:
		n = snprintf(buf, len, "%s", name);
		break;
	}

	return n;
}

void
ln_program_disasm(const ln_program_t *prog, FILE *fp)
{
	char buf[128];
	uint32_t i;

	if (!prog || !fp) return;

	fprintf(fp, "=== Program: %s ===\n", prog->name ? prog->name : "(unnamed)");
	fprintf(fp, "Instructions: %u\n\n", prog->code_len);

	for (i = 0; i < prog->code_len; i++) {
		ln_instr_disasm(&prog->code[i], buf, sizeof(buf));
		fprintf(fp, "[%4u] %s\n", i, buf);
	}
	fprintf(fp, "\n");
}

/*============================================================================
 * VM Lifecycle
 *============================================================================*/

int
ln_vm_init(ln_vm_t *vm, ln_arena_t *arena)
{
	if (!vm || !arena) return LN_VM_ERROR;

	memset(vm, 0, sizeof(*vm));
	vm->arena = arena;

	/* Private C locale for locale-independent number parsing (strtod_l). JSON
	 * numbers always use '.' as the decimal point, but plain strtod() honours
	 * LC_NUMERIC and would read "1.5" as 1.0 under a comma-decimal locale. "C"
	 * needs no locale files, so newlocale only fails on OOM; on failure we fail
	 * init, so the caller drops turbo and uses the locale-independent standard path
	 * rather than ever parsing a number under the wrong locale. Freed by
	 * ln_vm_destroy. */
	vm->c_locale = (void *)newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
	if (vm->c_locale == NULL) return LN_VM_ERROR;

	return LN_VM_OK;
}

void
ln_vm_destroy(ln_vm_t *vm)
{
	if (!vm) return;
	if (vm->c_locale != NULL) {
		freelocale((locale_t)vm->c_locale);
		vm->c_locale = NULL;
	}
}

void
ln_vm_reset(ln_vm_t *vm)
{
	if (!vm) return;

	vm->prog = NULL;
	vm->input = NULL;
	vm->input_end = NULL;
	vm->pc = 0;
	vm->ip = NULL;
	vm->fork_sp = 0;
	vm->call_sp = 0;
	vm->field_ctx_sp = 0;
	vm->result = NULL;
	vm->instr_count = 0;
	vm->backtrack_count = 0;
	vm->matched_rule = NULL;
	vm->error = NULL;
}


/*============================================================================
 * Date conversion for OP_FIELD_DATE with format="timestamp-unix"[-ms]
 *
 * Self-contained on purpose: the VM does not call into the standard parser and
 * does not build a json_object per message. tests/turbo_test_date.c compares
 * this against the standard parser over every accepted spelling of both
 * formats, so the duplication is held to exact parity by test rather than by
 * hope.
 *==========================================================================*/

typedef struct {
	int year, month, day;
	int hour, minute, second;
	int secfrac, secfrac_digits;
	int off_hour, off_minute;
	char off_mode;
} turbo_date_t;

static inline int
turbo_date_int(const unsigned char **buf, size_t *len)
{
	const unsigned char *p = *buf;
	size_t n = *len;
	int v = 0;

	while (n > 0 && *p >= '0' && *p <= '9') {
		v = v * 10 + (*p - '0');
		++p; --n;
	}
	*buf = p; *len = n;
	return v;
}

/* Days-from-civil; the standard parser clamps the same range and yields 0
 * outside it. */
static int64_t
turbo_date_to_unix(const turbo_date_t *d, int ms)
{
	int64_t y, era, yoe, doy, doe, days, ts;

	if (d->year < 1970 || d->year > 2100)
		return 0;

	y = d->year - (d->month <= 2);
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (d->month + (d->month > 2 ? -3 : 9)) + 2) / 5 + d->day - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	days = era * 146097 + doe - 719468;

	ts = days * 86400 + d->hour * 3600 + d->minute * 60 + d->second;
	if (d->off_mode == '-')
		ts += d->off_hour * 3600 + d->off_minute * 60;
	else
		ts -= d->off_hour * 3600 + d->off_minute * 60;

	if (ms) {
		int frac = d->secfrac;
		int div = 1;

		ts *= 1000;
		if (d->secfrac_digits == 1) frac *= 100;
		else if (d->secfrac_digits == 2) frac *= 10;
		else if (d->secfrac_digits > 3) {
			int i;
			for (i = 0; i < d->secfrac_digits - 3; ++i) div *= 10;
		}
		ts += frac / div;
	}
	return ts;
}

static int
turbo_scan_rfc3164(const unsigned char *p, size_t orglen, size_t *parsed,
		   turbo_date_t *d)
{
	size_t len = orglen;
	int v;

	memset(d, 0, sizeof(*d));
	d->off_mode = '+';
	if (len < 3) return -1;

	switch (*p++) {
	case 'j': case 'J':
		if (*p == 'a' || *p == 'A') { d->month = 1; p++; if(*p!='n'&&*p!='N') return -1; p++; }
		else if (*p == 'u' || *p == 'U') {
			p++;
			if (*p == 'n' || *p == 'N') d->month = 6;
			else if (*p == 'l' || *p == 'L') d->month = 7;
			else return -1;
			p++;
		} else return -1;
		break;
	case 'f': case 'F':
		d->month = 2; if((*p!='e'&&*p!='E')) return -1; p++; if((*p!='b'&&*p!='B')) return -1; p++; break;
	case 'm': case 'M':
		if (*p != 'a' && *p != 'A') return -1;
		p++;
		if (*p == 'r' || *p == 'R') d->month = 3;
		else if (*p == 'y' || *p == 'Y') d->month = 5;
		else return -1;
		p++;
		break;
	case 'a': case 'A':
		if (*p == 'p' || *p == 'P') { d->month = 4; p++; if(*p!='r'&&*p!='R') return -1; p++; }
		else if (*p == 'u' || *p == 'U') { d->month = 8; p++; if(*p!='g'&&*p!='G') return -1; p++; }
		else return -1;
		break;
	case 's': case 'S':
		d->month = 9; if(*p!='e'&&*p!='E') return -1; p++; if(*p!='p'&&*p!='P') return -1; p++; break;
	case 'o': case 'O':
		d->month = 10; if(*p!='c'&&*p!='C') return -1; p++; if(*p!='t'&&*p!='T') return -1; p++; break;
	case 'n': case 'N':
		d->month = 11; if(*p!='o'&&*p!='O') return -1; p++; if(*p!='v'&&*p!='V') return -1; p++; break;
	case 'd': case 'D':
		d->month = 12; if(*p!='e'&&*p!='E') return -1; p++; if(*p!='c'&&*p!='C') return -1; p++; break;
	default: return -1;
	}
	len -= 3;

	if (len == 0 || *p++ != ' ') return -1;
	--len;
	if (len > 0 && *p == ' ') { --len; ++p; }        /* one-digit day */

	d->day = turbo_date_int(&p, &len);
	if (d->day < 1 || d->day > 31) return -1;
	if (len == 0 || *p++ != ' ') return -1;
	--len;

	v = turbo_date_int(&p, &len);
	if (v > 1970 && v < 2100) {                      /* Cisco year variant */
		if (len == 0 || *p++ != ' ') return -1;
		--len;
		v = turbo_date_int(&p, &len);
	}
	d->hour = v;
	if (d->hour < 0 || d->hour > 23) return -1;

	if (len == 0 || *p++ != ':') return -1;
	--len;
	d->minute = turbo_date_int(&p, &len);
	if (d->minute < 0 || d->minute > 59) return -1;

	if (len == 0 || *p++ != ':') return -1;
	--len;
	d->second = turbo_date_int(&p, &len);
	if (d->second < 0 || d->second > 60) return -1;

	if (len > 0 && *p == ':') { ++p; --len; }        /* tolerated extra colon */

	{
		struct tm tm;
		const time_t now = time(NULL);
		gmtime_r(&now, &tm);
		d->year = tm.tm_year + 1900;                 /* RFC3164 carries no year */
	}
	*parsed = orglen - len;
	return 0;
}

static int
turbo_scan_rfc5424(const unsigned char *p, size_t orglen, size_t *parsed,
		   turbo_date_t *d)
{
	size_t len = orglen;

	memset(d, 0, sizeof(*d));
	d->year = turbo_date_int(&p, &len);
	if (len == 0 || *p++ != '-') return -1;
	--len;
	d->month = turbo_date_int(&p, &len);
	if (d->month < 1 || d->month > 12) return -1;
	if (len == 0 || *p++ != '-') return -1;
	--len;
	d->day = turbo_date_int(&p, &len);
	if (d->day < 1 || d->day > 31) return -1;
	if (len == 0 || *p++ != 'T') return -1;
	--len;
	d->hour = turbo_date_int(&p, &len);
	if (d->hour < 0 || d->hour > 23) return -1;
	if (len == 0 || *p++ != ':') return -1;
	--len;
	d->minute = turbo_date_int(&p, &len);
	if (d->minute < 0 || d->minute > 59) return -1;
	if (len == 0 || *p++ != ':') return -1;
	--len;
	d->second = turbo_date_int(&p, &len);
	if (d->second < 0 || d->second > 60) return -1;

	if (len > 0 && *p == '.') {
		const unsigned char *start;
		--len; ++p;
		start = p;
		d->secfrac = turbo_date_int(&p, &len);
		d->secfrac_digits = (int)(p - start);
	}

	if (len == 0) return -1;
	if (*p == 'Z') {
		d->off_mode = '+'; --len; ++p;
	} else if (*p == '+' || *p == '-') {
		d->off_mode = (char)*p;
		--len; ++p;
		d->off_hour = turbo_date_int(&p, &len);
		if (d->off_hour < 0 || d->off_hour > 23) return -1;
		if (len == 0 || *p++ != ':') return -1;
		--len;
		d->off_minute = turbo_date_int(&p, &len);
		if (d->off_minute < 0 || d->off_minute > 59) return -1;
	} else {
		return -1;                                   /* TZ is mandatory */
	}

	*parsed = orglen - len;
	return 0;
}

int
ln_turbo_date2unix(const int kind, const char *const str, const size_t strLen,
	const int ms, size_t *const parsed, int64_t *const ts)
{
	turbo_date_t d;
	int r;

	*parsed = 0;
	r = (kind == 1)
	  ? turbo_scan_rfc5424((const unsigned char *)str, strLen, parsed, &d)
	  : turbo_scan_rfc3164((const unsigned char *)str, strLen, parsed, &d);
	if (r != 0) return -1;
	*ts = turbo_date_to_unix(&d, ms);
	return 0;
}

/*============================================================================
 * Field Context Stack Operations (for ".." substitution)
 *============================================================================*/

static inline bool
vm_push_field_ctx(ln_vm_t *vm, const char *name, bool is_nested)
{
	ln_field_ctx_t *ctx;
	bool discard;

	if (vm->field_ctx_sp >= LN_VM_MAX_FIELD_CTX) {
		vm->error = "field context stack overflow";
		return false;
	}

	/*
	 * A context named ".." takes the name of the level above it: that is what
	 * ".." means, and it is how the standard parser collapses a single-member
	 * subtree into its parent's name. Pushing the literal instead put ".." (or
	 * nothing) into every field path produced underneath.
	 */
	if (name != NULL && name[0] == '.' && name[1] == '.' && name[2] == '\0') {
		/* Already a full path: the level above carries one. */
		name = (vm->field_ctx_sp > 0)
		     ? vm->field_ctx[vm->field_ctx_sp - 1].name
		     : NULL;
	} else if (name != NULL && name[0] != '\0'
		   && !(name[0] == '.' && name[1] == '\0')
		   && !(name[0] == '-' && name[1] == '\0')
		   && vm->field_ctx_sp > 0) {
		/*
		 * Contexts nest: %log:@outer% containing %mid:@inner% must yield
		 * log.mid.<field>. Only the top of the stack is consulted when a
		 * field name is built, so each context stores the whole path rather
		 * than its own segment; otherwise every level but the innermost was
		 * dropped.
		 */
		const ln_field_ctx_t *parent = &vm->field_ctx[vm->field_ctx_sp - 1];

		if (parent->name != NULL && parent->name[0] != '\0'
		    && !(parent->name[0] == '.' && parent->name[1] == '\0')
		    && !parent->discard) {
			char buf[512];   /* same ceiling as the JSON key scratch */
			size_t nl = strlen(name);

			if ((size_t)parent->name_len + 1 + nl < sizeof(buf)) {
				memcpy(buf, parent->name, parent->name_len);
				buf[parent->name_len] = '.';
				memcpy(buf + parent->name_len + 1, name, nl + 1);
				{
					const char *dup = ln_arena_strndup(vm->arena, buf,
							parent->name_len + 1 + nl);
					if (dup != NULL) name = dup;
				}
			}
		}
	}

	/*
	 * A field named "-" is matched but not stored, and that applies to a whole
	 * subtree, not just a scalar: the standard parser discards the parser's
	 * entire result. Mark the context so nothing emitted below it is kept, and
	 * inherit the mark so an inner context cannot escape it.
	 */
	discard = (name != NULL && name[0] == '-' && name[1] == '\0');
	if (vm->field_ctx_sp > 0 && vm->field_ctx[vm->field_ctx_sp - 1].discard)
		discard = true;

	ctx = &vm->field_ctx[vm->field_ctx_sp++];
	ctx->name = name;
	ctx->name_len = name ? (uint16_t)strlen(name) : 0;
	ctx->is_nested = is_nested ? 1 : 0;
	ctx->discard = discard ? 1 : 0;
	ctx->n_fields_at_push = vm->result ? vm->result->n_fields : 0;

	TRACE("[CTX] push \"%s\" (sp=%u, nested=%d)\n",
		  name ? name : "(null)", vm->field_ctx_sp, is_nested);

	return true;
}

static const char *vm_build_field_name(ln_vm_t *vm, const char *name, uint16_t *out_len);

static inline bool
vm_pop_field_ctx(ln_vm_t *vm)
{
	const ln_field_ctx_t *closed;

	if (vm->field_ctx_sp == 0) {
		vm->error = "field context stack underflow";
		return false;
	}
	closed = &vm->field_ctx[vm->field_ctx_sp - 1];
	vm->field_ctx_sp--;

	/*
	 * A named parser that stored nothing still yields an (empty) object in the
	 * standard parser: a custom type whose members are all "-", or one whose
	 * matching alternative is an empty literal. Emitting nothing here would
	 * make the field absent instead of empty. Popped first, so the name is
	 * built against the enclosing context.
	 */
	if (!closed->discard && closed->name != NULL && closed->name[0] != '\0'
	    && !(closed->name[0] == '.' && closed->name[1] == '\0')
	    && vm->result != NULL
	    && vm->result->n_fields == closed->n_fields_at_push) {
		uint16_t nlen;
		const char *full = vm_build_field_name(vm, closed->name, &nlen);

		if (full != NULL && full[0] != '\0')
			ln_fast_add_rawjson_static(vm->result, full, nlen, "{}", 2);
	}

	TRACE("[CTX] pop (sp=%u)\n", vm->field_ctx_sp);

	return true;
}

static inline const char *
vm_get_context_name(ln_vm_t *vm)
{
	if (vm->field_ctx_sp == 0) return NULL;
	return vm->field_ctx[vm->field_ctx_sp - 1].name;
}

static inline uint16_t
vm_get_context_name_len(ln_vm_t *vm)
{
	if (vm->field_ctx_sp == 0) return 0;
	return vm->field_ctx[vm->field_ctx_sp - 1].name_len;
}

/**
 * @brief Resolve ".." field name to parent context name.
 */
static inline const char * __attribute__((unused))
vm_resolve_field_name(ln_vm_t *vm, const char *name, uint16_t *out_len)
{
	if (name && name[0] == '.' && name[1] == '.' &&
		(name[2] == '\0' || name[2] == '.')) {
		const char *ctx_name = vm_get_context_name(vm);
		if (ctx_name && ctx_name[0]) {
			*out_len = vm_get_context_name_len(vm);
			TRACE("[RESOLVE] \"%s\" -> \"%s\"\n", name, ctx_name);
			return ctx_name;
		}
		TRACE("[RESOLVE] \"%s\" -> NULL (no context)\n", name);
		*out_len = 0;
		return NULL;
	}
	*out_len = (uint16_t)strlen(name);
	return name;
}

/*============================================================================
 * Fork Stack Operations
 *============================================================================*/

static inline bool
vm_push_fork(ln_vm_t *vm, uint32_t alt_pc)
{
	ln_fork_t *f;

	if (vm->fork_sp >= LN_VM_MAX_FORKS) {
		vm->error = "fork stack overflow";
		return false;
	}

	f = &vm->forks[vm->fork_sp++];
	f->pc = alt_pc;
	f->ip = vm->ip;
	f->n_fields = vm->result ? vm->result->n_fields : 0;
	f->n_tags = vm->result ? vm->result->n_tags : 0;
	f->call_sp = vm->call_sp;
	f->field_ctx_sp = vm->field_ctx_sp;

	return true;
}

static inline bool
vm_pop_fork(ln_vm_t *vm)
{
	ln_fork_t *f;

	if (vm->fork_sp == 0)
		return false;

	vm->backtrack_count++;
	f = &vm->forks[--vm->fork_sp];

	vm->pc = f->pc;
	vm->ip = f->ip;
	vm->call_sp = f->call_sp;
	vm->field_ctx_sp = f->field_ctx_sp;

	if (vm->result) {
		vm->result->n_fields = f->n_fields;
		vm->result->n_tags = f->n_tags;
	}

	return true;
}

/*============================================================================
 * Optimized Field Extraction Helpers
 *============================================================================*/

/**
 * @brief Build prefixed field name for nested context.
 *
 * When inside custom type context "foo" and field is "bar",
 * produces "foo.bar". For ".." produces just "foo".
 * Uses arena for allocation.
 */
/* Resolve a field/context name for an instruction: the inline opcode buffer, or
 * the program string pool when the name was too long to inline
 * (LN_INSTR_F_NAME_POOL). inline_buf is the opcode's inline name buffer, which
 * holds a uint32 pool offset in the pooled case. Names in the pool are
 * NUL-terminated, so callers use the returned pointer as a plain C string. */
static inline const char *
turbo_iname(const ln_vm_t *vm, const ln_instr_t *inst, const char *inline_buf)
{
	/* Pooling is the rare case (names that fit inline dominate), so keep the
	 * common path a straight fall-through. */
	if (UNLIKELY(inst->flags & LN_INSTR_F_NAME_POOL)) {
		uint32_t off;
		memcpy(&off, inline_buf, sizeof(off));
		return vm->prog->strpool + off;
	}
	return inline_buf;
}

static inline const char *
vm_build_field_name(ln_vm_t *vm, const char *name, uint16_t *out_len)
{
	const char *ctx_name;
	uint16_t ctx_len;
	size_t name_slen;
	uint16_t name_len;
	bool all_dots;
	uint16_t i;
	uint16_t total_len;
	char *prefixed;

	/* "-" is matched but not stored, and neither is anything under a context
	 * that carries the same name, however deep. */
	if (name != NULL && name[0] == '-' && name[1] == '\0') {
		*out_len = 0;
		return NULL;
	}
	if (vm->field_ctx_sp > 0 && vm->field_ctx[vm->field_ctx_sp - 1].discard) {
		*out_len = 0;
		return NULL;
	}

	/* Handle ".." - resolve to context name only */
	if (name && name[0] == '.' && name[1] == '.' &&
		(name[2] == '\0' || name[2] == '.')) {
		ctx_name = vm_get_context_name(vm);
		/* Skip root context "." */
		if (ctx_name && ctx_name[0] && !(ctx_name[0] == '.' && ctx_name[1] == '\0')) {
			*out_len = vm_get_context_name_len(vm);
			return ctx_name;
		}
		*out_len = 0;
		return NULL;
	}

	/* If no context, use field name as-is */
	if (vm->field_ctx_sp == 0 || !name || !name[0]) {
		if (name) {
			size_t slen = strlen(name);
			if (slen > UINT16_MAX) { *out_len = 0; return NULL; }
			*out_len = (uint16_t)slen;
		} else {
			*out_len = 0;
		}
		return name;
	}

	/* Get context name */
	ctx_name = vm_get_context_name(vm);
	ctx_len = vm_get_context_name_len(vm);

	/* Skip root context "." or empty - don't prefix */
	if (!ctx_name || ctx_len == 0 ||
		(ctx_len == 1 && ctx_name[0] == '.') ||
		(ctx_len == 0 && ctx_name && ctx_name[0] == '\0')) {
		size_t slen = strlen(name);
		if (slen > UINT16_MAX) { *out_len = 0; return NULL; }
		*out_len = (uint16_t)slen;
		return name;
	}

	name_slen = strlen(name);
	if (name_slen > UINT16_MAX) { *out_len = 0; return NULL; }
	name_len = (uint16_t)name_slen;

	/* SAFETY: Skip if context name contains only dots */
	all_dots = true;
	for (i = 0; i < ctx_len; i++) {
		if (ctx_name[i] != '.') { all_dots = false; break; }
	}
	if (all_dots) {
		*out_len = name_len;
		return name;
	}

	/* Guard against uint16_t overflow on total length */
	if ((size_t)ctx_len + 1 + name_len > UINT16_MAX) {
		*out_len = name_len;
		return name;  /* Fallback to unprefixed */
	}
	total_len = ctx_len + 1 + name_len;  /* "ctx.name" */

	/* Allocate from arena */
	prefixed = ln_arena_alloc(vm->arena, total_len + 1);
	if (!prefixed) {
		*out_len = name_len;
		return name;  /* Fallback to unprefixed */
	}

	memcpy(prefixed, ctx_name, ctx_len);
	prefixed[ctx_len] = '.';
	memcpy(prefixed + ctx_len + 1, name, name_len);
	prefixed[total_len] = '\0';

	*out_len = total_len;
	return prefixed;
}

/*
 * CEF writes a delimiter that belongs to a value as "\|" or "\=", and a
 * literal backslash as "\\". A value therefore ends at the first delimiter
 * that is not itself escaped.
 */
static const char *
cef_find_unescaped(const char *const base, const char *s,
				   const char *const end, const char c)
{
	while (s < end) {
		const size_t off = ln_simd_find_char(s, (size_t)(end - s), c);
		const char *hit;
		const char *b;
		size_t nbs = 0;

		if (off >= (size_t)(end - s))
			return NULL;
		hit = s + off;
		b = hit;
		while (b > base && *(b - 1) == '\\') {
			++nbs;
			--b;
		}
		if ((nbs & 1) == 0)
			return hit;
		s = hit + 1;
	}
	return NULL;
}

/*
 * Remove CEF escape characters. A header field escapes only the delimiter and
 * the backslash itself; an extension value additionally spells a newline as
 * "\n", a carriage return as "\r" and a solidus as "\/". Any other escape is
 * not valid CEF and rejects the message, which is what the standard parser
 * does, so an input both engines see must be accepted or refused by both.
 *
 * A value carrying no backslash is the common case and is returned as a span
 * into the input, so nothing is copied. Only an escaped value is rewritten,
 * into the arena, which keeps the per-message allocation count at zero for
 * ordinary input.
 *
 * Returns 0 on success, -1 if the value carries an escape CEF does not define.
 */
static int
cef_unescape(ln_vm_t *vm, const char *const s, const size_t len,
			 const int is_ext, const char **const out,
			 size_t *const out_len)
{
	char *dst;
	size_t si, di = 0;

	if (len == 0 || ln_simd_find_char(s, len, '\\') >= len) {
		*out = s;
		*out_len = len;
		return 0;
	}
	dst = (char *)ln_arena_alloc(vm->arena, len);
	if (dst == NULL) {
		*out = s;
		*out_len = len;
		return 0;
	}
	for (si = 0; si < len; ++si) {
		char c = s[si];

		if (c != '\\') {
			dst[di++] = c;
			continue;
		}
		if (si + 1 >= len)
			return -1;
		c = s[++si];
		if (is_ext) {
			switch (c) {
			case '=':  dst[di++] = '=';  break;
			case '\\': dst[di++] = '\\'; break;
			case 'n':  dst[di++] = '\n'; break;
			case 'r':  dst[di++] = '\r'; break;
			case '/':  dst[di++] = '/';  break;
			default:   return -1;
			}
		} else {
			if (c != '\\' && c != '|')
				return -1;
			dst[di++] = c;
		}
	}
	*out = dst;
	*out_len = di;
	return 0;
}

/**
 * @brief Add string field using fast result.
 *
 * The name pointer comes from instruction data which is stable
 * for the lifetime of the program. No copying needed.
 */
static inline bool
vm_add_string_field(ln_vm_t *vm, const char *name, const char *val, size_t len)
{
	uint16_t name_len;
	const char *full_name;

	if (!vm->result) return true;

	/* Build prefixed name if inside context */
	full_name = vm_build_field_name(vm, name, &name_len);
	if (!full_name || !full_name[0]) return true;

	/* Direct add - name pointer is stable (from instruction or arena) */
	return ln_fast_add_string_static(vm->result, full_name, name_len, val, len) == 0;
}

/**
 * @brief Add integer field using fast result.
 */
static inline bool
vm_add_int_field(ln_vm_t *vm, const char *name, int64_t val)
{
	uint16_t name_len;
	const char *full_name;

	if (!vm->result) return true;

	full_name = vm_build_field_name(vm, name, &name_len);
	if (!full_name || !full_name[0]) return true;

	return ln_fast_add_int_static(vm->result, full_name, name_len, val) == 0;
}

/*
 * Add a JSON null field. A valueless iptables flag is a key with a null value
 * in the standard parser, not a key with an empty string.
 */
static bool
vm_add_null_field(ln_vm_t *vm, const char *name)
{
	uint16_t name_len;
	const char *full_name;

	if (!vm->result) return true;

	full_name = vm_build_field_name(vm, name, &name_len);
	if (!full_name || !full_name[0]) return true;

	return ln_fast_add_null_static(vm->result, full_name, name_len) == 0;
}

/**
 * @brief Add a raw JSON value (emitted verbatim, without quotes) using fast
 * result. Used for float format="number", where the matched text is already a
 * valid JSON number and is emitted as-is to preserve its serialization.
 */
static inline bool
vm_add_rawjson_field(ln_vm_t *vm, const char *name, const char *val, size_t len)
{
	uint16_t name_len;
	const char *full_name;

	if (!vm->result) return true;

	full_name = vm_build_field_name(vm, name, &name_len);
	if (!full_name || !full_name[0]) return true;

	return ln_fast_add_rawjson_static(vm->result, full_name, name_len, val, len) == 0;
}

/*============================================================================
 * Inline Parser Implementations
 *============================================================================*/

static inline int
parse_word(const char *buf, size_t len, size_t *out_len)
{
	size_t i = 0;
	while (i < len && buf[i] != ' ')
		i++;
	if (i == 0) return -1;
	*out_len = i;
	return 0;
}

static inline int
parse_number(const char *buf, size_t len, int64_t *value, size_t *out_len)
{
	size_t i = 0;
	uint64_t val = 0;
	int neg = 0;
	size_t start;

	if (len == 0) return -1;

	if (buf[0] == '-') { neg = 1; i++; }
	else if (buf[0] == '+') { i++; }

	start = i;
	while (i < len && myisdigit(buf[i])) {
		int digit = buf[i] - '0';
		if (val > (UINT64_MAX - (uint64_t)digit) / 10)
			return -1;  /* overflow */
		val = val * 10 + (uint64_t)digit;
		i++;
	}

	if (i == start) return -1;

	/* Range-check before converting to signed */
	if (neg) {
		if (val > (uint64_t)INT64_MAX + 1)
			return -1;  /* underflow: value more negative than INT64_MIN */
		*value = -(int64_t)val;
	} else {
		if (val > (uint64_t)INT64_MAX)
			return -1;  /* overflow: value exceeds INT64_MAX */
		*value = (int64_t)val;
	}
	*out_len = i;
	return 0;
}

static inline int
parse_float(const char *buf, size_t len, size_t *out_len)
{
	size_t i = 0;
	int seen_point = 0;
	int digits = 0;

	if (len == 0) return -1;
	if (buf[0] == '-') i++;

	for (; i < len; i++) {
		if (buf[i] == '.') {
			if (seen_point) break;
			seen_point = 1;
		} else if (myisdigit(buf[i])) {
			digits++;
		} else {
			break;
		}
	}

	if (digits == 0) return -1;
	*out_len = i;
	return 0;
}

static inline int
parse_hex(const char *buf, size_t len, int64_t *value, size_t *out_len)
{
	size_t i;
	uint64_t val;
	int ndigits;

	if (len < 3) return -1;
	if (buf[0] != '0' || (buf[1] != 'x' && buf[1] != 'X')) return -1;

	i = 2;
	val = 0;
	ndigits = 0;

	while (i < len && isxdigit((unsigned char)buf[i])) {
		char c;
		int digit;
		ndigits++;
		if (ndigits > 16) return -1;  /* overflow: >16 hex digits exceeds uint64_t */
		c = buf[i];
		if (c >= '0' && c <= '9') digit = c - '0';
		else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
		else digit = c - 'A' + 10;
		val = val * 16 + digit;
		i++;
	}

	if (i == 2) return -1;
	*value = (int64_t)val;
	*out_len = i;
	return 0;
}

static inline int
check_ipv4_byte(const char *buf, size_t len, size_t *pos)
{
	int val = 0;
	size_t i = *pos;

	if (i >= len || !myisdigit(buf[i])) return -1;

	val = buf[i++] - '0';
	if (i < len && myisdigit(buf[i])) {
		val = val * 10 + buf[i++] - '0';
		if (i < len && myisdigit(buf[i]))
			val = val * 10 + buf[i++] - '0';
	}

	if (val > 255) return -1;
	*pos = i;
	return 0;
}

static inline int
parse_ipv4(const char *buf, size_t len, size_t *out_len)
{
	size_t i = 0;
	if (len < 7) return -1;

	if (check_ipv4_byte(buf, len, &i) != 0) return -1;
	if (i >= len || buf[i++] != '.') return -1;
	if (check_ipv4_byte(buf, len, &i) != 0) return -1;
	if (i >= len || buf[i++] != '.') return -1;
	if (check_ipv4_byte(buf, len, &i) != 0) return -1;
	if (i >= len || buf[i++] != '.') return -1;
	if (check_ipv4_byte(buf, len, &i) != 0) return -1;

	*out_len = i;
	return 0;
}

static inline int
parse_ipv6(const char *buf, size_t len, size_t *out_len)
{
	size_t i = 0;
	while (i < len) {
		if (isxdigit((unsigned char)buf[i])) {
			i++;
		} else if (buf[i] == ':' && i > 0) {
			i++;
		} else if (buf[i] == '.' && i >= 2) {
			while (i < len && (myisdigit(buf[i]) || buf[i] == '.'))
				i++;
			break;
		} else {
			break;
		}
	}
	if (i < 2) return -1;
	*out_len = i;
	return 0;
}

static inline int
parse_op_quoted(const char *buf, size_t len, size_t *start,
				size_t *out_len, size_t *consumed)
{
	char quote;

	if (len == 0) return -1;

	quote = buf[0];
	if (quote == '"' || quote == '\'') {
		size_t i = 1;
		while (i < len) {
			if (buf[i] == '\\' && i + 1 < len) { i += 2; }
			else if (buf[i] == quote) {
				*start = 1;
				*out_len = i - 1;
				*consumed = i + 1;
				return 0;
			} else { i++; }
		}
		return -1;
	} else {
		size_t wlen;
		if (parse_word(buf, len, &wlen) != 0) return -1;
		*start = 0;
		*out_len = wlen;
		*consumed = wlen;
		return 0;
	}
}

static inline int
parse_json(const char *buf, size_t len, size_t *out_len)
{
	char open;
	char close;
	int depth = 0;
	bool in_string = false;
	size_t i;

	if (len == 0) return -1;

	open = buf[0];

	if (open == '{') close = '}';
	else if (open == '[') close = ']';
	else return -1;

	for (i = 0; i < len; i++) {
		char c = buf[i];
		if (in_string) {
			if (c == '\\' && i + 1 < len) { i++; }
			else if (c == '"') { in_string = false; }
		} else {
			if (c == '"') { in_string = true; }
			else if (c == open) { depth++; }
			else if (c == close) {
				depth--;
				if (depth == 0) {
					*out_len = i + 1;
					return 0;
				}
			}
		}
	}
	return -1;
}

/*============================================================================
 * SIMD-accelerated JSON flattening
 *============================================================================
 *
 * OP_FIELD_JSON flattens a nested JSON object into the flat result store using
 * dotted keys (e.g. {"alert":{"severity":3}} -> "alert.severity"=3). The
 * dotted keys integrate with the existing flat result-tree key convention
 * (the JSON serializer re-nests them on output) and reuse the same separator
 * the other nested turbo fields use (vm_build_field_name '.').
 *
 * Design: a length-gated hybrid scan that REUSES the VM's existing SIMD
 * primitives (ln_simd_find_char / ln_simd_skip_space, SSE4.2 + NEON + scalar
 * backends) rather than inventing new vector machinery, so OP_FIELD_JSON
 * inherits the dual-arch + scalar fallback contract for free.
 *
 *   - Structural walk (scalar recursive descent): typical log JSON is
 *     structurally DENSE (structural bytes sit only a few bytes apart) so a
 *     16-wide vector structural-set scan loses its per-chunk setup cost against
 *     a tight scalar dispatch on the next byte. A pure-SIMD structural-set scan
 *     measured slower than the scalar walk on representative log corpora, so we
 *     walk structural bytes scalarly.
 *   - String-body leap (length-gated SIMD): the one place long byte runs occur
 *     is inside long string values (URLs, long identifiers). json_scan_string()
 *     uses ln_simd_find_char() to jump to the closing quote, but ONLY when the
 *     remaining span is >= 2 vector widths; short bodies take the scalar path.
 *     This yields the SIMD win where it exists and zero regression where it
 *     doesn't.
 *   - Whitespace is skipped via the vectorized ln_simd_skip_space().
 *
 * Value lifetime (correctness landmine): BOTH the dotted key and any string
 * value are ln_arena_strndup'd into the VM arena before we return. The flat
 * store keeps STATIC (non-owning) pointers; only arena pointers survive for the
 * lifetime of the result. A pointer into the transient input line would dangle.
 * Keys are built in a small stack buffer then arena-duped.
 */

/* LN_JSON_MAX_DEPTH is defined in turbo_result_fast.h so the flattener here and
 * the JSON serializer in turbo_json_impl.c share one ceiling. */
#define LN_JSON_KEYBUF     512          /* dotted-path scratch (stack) */

/* Maximum object/array nesting accepted in a JSON field value. Matches json-c's
 * JSON_TOKENER_DEFAULT_DEPTH so a %field:json% value is accepted or rejected at
 * the same nesting as the standard parser. */
#define LN_JSON_INPUT_MAX_DEPTH  32

typedef struct {
	ln_vm_t      *vm;
	const char   *buf;          /* whole JSON span being parsed */
	size_t        len;
	size_t        pos;          /* scalar cursor (offset into buf) */
	char          key[LN_JSON_KEYBUF];  /* current dotted prefix */
	size_t        key_len;      /* bytes used in key[] */
	int           n_emitted;    /* leaves added to the flat store */
	bool          full;         /* hit LN_FAST_MAX_FIELDS: stop adding */
	bool          validate_only;/* when set, the parser only checks that the JSON
	                             * is well-formed and measures its byte span; it
	                             * stores no fields */
} ln_json_ctx_t;

/*
 * Stage-1 SIMD skip of insignificant whitespace at ctx->pos. Reuses the VM's
 * vectorized whitespace skip.
 */
static inline void
json_skip_ws(ln_json_ctx_t *c)
{
	if (c->pos < c->len)
		c->pos += ln_simd_skip_space(c->buf + c->pos, c->len - c->pos);
}

/*
 * Validate that every backslash escape in a JSON string body is well-formed:
 * one of \" \\ \/ \b \f \n \r \t, or \u followed by four hex digits. Returns 0
 * if all escapes are valid, -1 otherwise. The caller skips this pass for bodies
 * with no backslash.
 */
static int
json_validate_escapes(const char *s, size_t n)
{
	size_t i = 0;

	while (i < n) {
		if (s[i] != '\\') { i++; continue; }
		if (n - i < 2) return -1;           /* trailing backslash */
		switch (s[i + 1]) {
		case '"': case '\\': case '/': case 'b':
		case 'f': case 'n': case 'r': case 't':
			i += 2;
			break;
		case 'u': {
			size_t k;
			if (n - i < 6) return -1;       /* need \uXXXX */
			for (k = 2; k < 6; k++) {
				char h = s[i + k];
				if (!((h >= '0' && h <= '9') ||
				      (h >= 'a' && h <= 'f') ||
				      (h >= 'A' && h <= 'F')))
					return -1;
			}
			i += 6;
			break;
		}
		default:
			return -1;                      /* unknown escape */
		}
	}
	return 0;
}

/*
 * Stage-1 SIMD scan of a JSON string body starting just AFTER the opening
 * quote. Uses ln_simd_find_char() to leap to the next '"' and corrects for
 * backslash escapes scalarly (escapes are rare in typical log JSON, so the SIMD
 * leap dominates). On return str/slen describe the raw (still-escaped) body
 * and ctx->pos points just past the closing quote. Returns -1 if unterminated
 * or if the body contains a malformed escape.
 */
static int
json_scan_string(ln_json_ctx_t *c, const char **str, size_t *slen)
{
	size_t start = c->pos;          /* first byte of body */
	size_t p = c->pos;

	for (;;) {
		size_t rem = c->len - p;
		size_t off;
		size_t bs = 0;
		size_t q;
		/* Length-gated SIMD: typical log JSON is structurally dense (short
		 * string bodies, structural bytes a few bytes apart) so a 16-wide
		 * vector scan loses its per-chunk setup cost against a tight scalar
		 * loop on short bodies. We only pay for SIMD once a long body
		 * (>= 2 vector widths) is plausible: that captures the long values
		 * (URLs, long identifiers) where the leap actually pays off. Net:
		 * scalar-or-better on dense JSON, SIMD win on long values, zero
		 * regression elsewhere. */
		if (rem >= 2 * LN_SIMD_WIDTH) {
			off = ln_simd_find_char(c->buf + p, rem, '"');
		} else {
			off = 0;
			while (off < rem && c->buf[p + off] != '"') off++;
		}
		if (off >= rem) return -1;  /* no closing quote */
		p += off;
		/* count preceding backslashes to know if this quote is escaped */
		q = p;
		while (q > start && c->buf[q - 1] == '\\') { bs++; q--; }
		if ((bs & 1) == 0) {        /* even => real closing quote */
			*str = c->buf + start;
			*slen = p - start;
			c->pos = p + 1;         /* consume closing quote */
			/* A captured JSON value is emitted verbatim, so any escape it
			 * contains must be well-formed for the output to stay valid. */
			if (memchr(*str, '\\', *slen) &&
			    json_validate_escapes(*str, *slen) != 0)
				return -1;
			return 0;
		}
		p++;                        /* escaped quote, keep scanning */
		if (p >= c->len) return -1;
	}
}

/*
 * Unescape a raw JSON string body into an arena buffer (the common JSON
 * sequences: \" \\ \/ \n \r \t \b \f and \uXXXX -> UTF-8).
 * If the body has no backslash, we arena-strndup directly (fast path). Returns
 * arena pointer + out length, or NULL on OOM.
 */
static const char *
json_unescape_arena(ln_vm_t *vm, const char *s, size_t n, size_t *out_len)
{
	const char *bs = memchr(s, '\\', n);
	char *dst;
	size_t o = 0;
	size_t i;
	if (bs == NULL) {
		char *dup = ln_arena_strndup(vm->arena, s, n);
		if (dup) *out_len = n;
		return dup;
	}
	/* n is an input-derived length; guard n+1 against size_t wrap: n ==
	 * SIZE_MAX would make ln_arena_alloc see 0 and round it up to a 1-byte
	 * buffer, which the unescape loop below would then overflow. */
	if (n == SIZE_MAX) return NULL;
	dst = ln_arena_alloc(vm->arena, n + 1);
	if (!dst) return NULL;
	for (i = 0; i < n; i++) {
		char ch = s[i];
		if (ch == '\\' && i + 1 < n) {
			char e = s[++i];
			switch (e) {
			case '"':  dst[o++] = '"';  break;
			case '\\': dst[o++] = '\\'; break;
			case '/':  dst[o++] = '/';  break;
			case 'n':  dst[o++] = '\n'; break;
			case 'r':  dst[o++] = '\r'; break;
			case 't':  dst[o++] = '\t'; break;
			case 'b':  dst[o++] = '\b'; break;
			case 'f':  dst[o++] = '\f'; break;
			case 'u': {
				/* \uXXXX -> UTF-8 (BMP only; surrogate pairs left as-is). */
				if (i + 4 < n) {
					unsigned cp = 0; int ok = 1, k;
					for (k = 1; k <= 4; k++) {
						char h = s[i + k]; unsigned d;
						if (h >= '0' && h <= '9') d = (unsigned)(h - '0');
						else if (h >= 'a' && h <= 'f') d = (unsigned)(h - 'a' + 10);
						else if (h >= 'A' && h <= 'F') d = (unsigned)(h - 'A' + 10);
						else { ok = 0; break; }
						cp = (cp << 4) | d;
					}
					if (ok) {
						i += 4;
						if (cp < 0x80) {
							dst[o++] = (char)cp;
						} else if (cp < 0x800) {
							dst[o++] = (char)(0xC0 | (cp >> 6));
							dst[o++] = (char)(0x80 | (cp & 0x3F));
						} else {
							dst[o++] = (char)(0xE0 | (cp >> 12));
							dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
							dst[o++] = (char)(0x80 | (cp & 0x3F));
						}
						break;
					}
				}
				/* malformed \u: emit the backslash + 'u' literally; the
				 * hex-ish bytes that follow are emitted as normal chars. */
				dst[o++] = '\\';
				dst[o++] = 'u';
				break;
			}
			default: dst[o++] = e; break;
			}
		} else {
			dst[o++] = ch;
		}
	}
	dst[o] = '\0';
	*out_len = o;
	return dst;
}

/* Emit a leaf into the flat store. key[]/key_len is the dotted path. */
static void
json_emit_string(ln_json_ctx_t *c, const char *raw, size_t raw_len)
{
	size_t vlen;
	const char *key;
	const char *val;
	if (c->validate_only) return;
	if (c->full || c->vm->result == NULL) return;
	if (c->vm->result->n_fields >= LN_FAST_MAX_FIELDS) {
		c->full = true;
		c->vm->result->flags |= LN_FRESULT_TRUNCATED;
		return;
	}
	key = ln_arena_strndup(c->vm->arena, c->key, c->key_len);
	val = json_unescape_arena(c->vm, raw, raw_len, &vlen);
	if (!key || !val) return;
	if (ln_fast_add_string_static(c->vm->result, key, (uint16_t)c->key_len,
				      val, (uint32_t)vlen) == 0)
		c->n_emitted++;
}

/*
 * Emit a nested array verbatim, tagged LN_FFIELD_RAW_JSON.
 *
 * Flattening an array to .0/.1/... keys loses the fact that it was an array:
 * both the JSON serializer and any consumer of the fast result can only rebuild
 * an object with numeric keys, and "0" is indistinguishable from a genuine
 * object key. Keeping the array as a raw JSON span is what %field:json% already
 * does for a whole object, so consumers that handle RAW_JSON need no change.
 */
static int
json_emit_rawjson(ln_json_ctx_t *c, const char *raw, size_t raw_len)
{
	const char *key;
	const char *val;

	if (c->validate_only) return 0;
	if (c->full || c->vm->result == NULL) return 0;
	if (c->vm->result->n_fields >= LN_FAST_MAX_FIELDS) {
		c->full = true;
		c->vm->result->flags |= LN_FRESULT_TRUNCATED;
		return 0;
	}
	key = ln_arena_strndup(c->vm->arena, c->key, c->key_len);
	val = ln_arena_strndup(c->vm->arena, raw, raw_len);
	if (!key || !val) return -1;   /* arena exhausted: caller flattens instead */
	if (ln_fast_add_rawjson_static(c->vm->result, key, (uint16_t)c->key_len,
				       val, (uint32_t)raw_len) != 0)
		return -1;
	c->n_emitted++;
	return 0;
}

static void
json_emit_int(ln_json_ctx_t *c, int64_t v)
{
	const char *key;
	if (c->validate_only) return;
	if (c->full || c->vm->result == NULL) return;
	if (c->vm->result->n_fields >= LN_FAST_MAX_FIELDS) {
		c->full = true;
		c->vm->result->flags |= LN_FRESULT_TRUNCATED;
		return;
	}
	key = ln_arena_strndup(c->vm->arena, c->key, c->key_len);
	if (!key) return;
	if (ln_fast_add_int_static(c->vm->result, key, (uint16_t)c->key_len, v) == 0)
		c->n_emitted++;
}

static void
json_emit_double(ln_json_ctx_t *c, double v)
{
	const char *key;
	if (c->validate_only) return;
	if (c->full || c->vm->result == NULL) return;
	if (c->vm->result->n_fields >= LN_FAST_MAX_FIELDS) {
		c->full = true;
		c->vm->result->flags |= LN_FRESULT_TRUNCATED;
		return;
	}
	key = ln_arena_strndup(c->vm->arena, c->key, c->key_len);
	if (!key) return;
	if (ln_fast_add_double_static(c->vm->result, key, (uint16_t)c->key_len, v) == 0)
		c->n_emitted++;
}

/*
 * Emit s[0..len) (an already grammar-validated JSON number span) as a double.
 * The common case fits the stack buffer; a pathologically long number is copied
 * in full via the VM arena, because truncating to the stack buffer could cut a
 * trailing 'eNN' exponent and silently corrupt the magnitude. Parsing goes
 * through the VM's private C locale (strtod_l), so the decimal point is always
 * '.' regardless of the process LC_NUMERIC, matching the locale-independent standard
 * path; out-of-range values saturate to +/-HUGE_VAL per C, acceptable here. The
 * locale is guaranteed valid here: ln_vm_init fails if it cannot be created, so
 * the turbo context is never built and the standard path is used instead.
 */
static void
json_emit_double_span(ln_json_ctx_t *c, const char *s, size_t len)
{
	char stackbuf[64];
	const char *num;

	/*
	 * libfastjson keeps the source spelling of a number: 1.50 stays 1.50,
	 * 100.0 stays 100.0, 1e3 stays 1e3. Storing the span verbatim is
	 * therefore what matches the standard parser byte for byte, and it
	 * skips strtod entirely. The parse below is only the fallback for a
	 * span the arena could not take.
	 */
	if (json_emit_rawjson(c, s, len) == 0)
		return;

	if (len < sizeof(stackbuf)) {
		memcpy(stackbuf, s, len);
		stackbuf[len] = '\0';
		num = stackbuf;
	} else {
		num = ln_arena_strndup(c->vm->arena, s, len);
		if (num == NULL) return;   /* OOM: drop this field, keep the line */
	}
	json_emit_double(c, strtod_l(num, NULL, (locale_t)c->vm->c_locale));
}

static void
json_emit_bool(ln_json_ctx_t *c, bool v)
{
	const char *key;

	if (c->validate_only) return;
	if (c->full || c->vm->result == NULL) return;
	if (c->vm->result->n_fields >= LN_FAST_MAX_FIELDS) {
		c->full = true;
		c->vm->result->flags |= LN_FRESULT_TRUNCATED;
		return;
	}
	key = ln_arena_strndup(c->vm->arena, c->key, c->key_len);
	if (!key) return;
	if (ln_fast_add_bool_static(c->vm->result, key, (uint16_t)c->key_len, v) == 0)
		c->n_emitted++;
}

/* A null member is part of the document; dropping it would make the field
 * absent, which is not what the standard parser produces. */
static void
json_emit_null(ln_json_ctx_t *c)
{
	const char *key;

	if (c->validate_only) return;
	if (c->full || c->vm->result == NULL) return;
	if (c->vm->result->n_fields >= LN_FAST_MAX_FIELDS) {
		c->full = true;
		c->vm->result->flags |= LN_FRESULT_TRUNCATED;
		return;
	}
	key = ln_arena_strndup(c->vm->arena, c->key, c->key_len);
	if (!key) return;
	if (ln_fast_add_null_static(c->vm->result, key, (uint16_t)c->key_len) == 0)
		c->n_emitted++;
}

static int json_parse_value(ln_json_ctx_t *c, int depth);

/*
 * Parse a JSON number at ctx->pos with a strict JSON-grammar validator:
 *   optional leading '-', required integer digits, optional '.'+frac digits,
 *   optional 'e'/'E' (+/-) exp digits. Returns -1 on any invalid form
 *   (lone '-'/'.', '+'-prefix, multiple '.'/'e', sign mid-number, ...) WITHOUT
 *   advancing the cursor, so the caller backtracks the whole line instead of
 *   mis-consuming garbage. On success advances pos by exactly the validated
 *   length. Decides int vs double by presence of '.', 'e' or 'E'.
 */
static int
json_parse_number(ln_json_ctx_t *c)
{
	const char *s = c->buf + c->pos;
	size_t rem = c->len - c->pos;
	size_t i = 0;
	bool is_double = false;

	if (i < rem && s[i] == '-') i++;        /* JSON allows leading '-' only */

	{
		size_t int_start = i;
		while (i < rem && s[i] >= '0' && s[i] <= '9') i++;
		if (i == int_start) return -1;      /* integer digits required */
	}

	if (i < rem && s[i] == '.') {
		size_t frac_start;
		is_double = true; i++;
		frac_start = i;
		while (i < rem && s[i] >= '0' && s[i] <= '9') i++;
		if (i == frac_start) return -1;     /* '.' must be followed by digits */
	}

	if (i < rem && (s[i] == 'e' || s[i] == 'E')) {
		size_t exp_start;
		is_double = true; i++;
		if (i < rem && (s[i] == '+' || s[i] == '-')) i++;
		exp_start = i;
		while (i < rem && s[i] >= '0' && s[i] <= '9') i++;
		if (i == exp_start) return -1;      /* exponent digits required */
	}

	if (is_double) {
		json_emit_double_span(c, s, i);
	} else {
		int64_t v;
		size_t consumed;
		if (parse_number(s, i, &v, &consumed) == 0)
			json_emit_int(c, v);
		else
			/* overflow / too-long int: keep full span as double for fidelity */
			json_emit_double_span(c, s, i);
	}
	c->pos += i;
	return 0;
}

/*
 * Parse a JSON object body: ctx->pos is just past '{'. Appends ".<key>" to the
 * dotted path for each member, recurses into the value, then truncates back.
 */
static int
json_parse_object(ln_json_ctx_t *c, int depth)
{
	size_t saved_len = c->key_len;

	json_skip_ws(c);
	if (c->pos < c->len && c->buf[c->pos] == '}') { c->pos++; return 0; }

	for (;;) {
		const char *kstr; size_t klen;

		json_skip_ws(c);
		if (c->pos >= c->len || c->buf[c->pos] != '"') return -1;
		c->pos++;                                   /* opening quote */
		if (json_scan_string(c, &kstr, &klen) != 0) return -1;

		/* append ".<key>" to dotted path; fail (backtrack the line) on
		 * overflow rather than silently dropping the segment, which would
		 * mislabel this leaf under the parent/root key = silent corruption. */
		c->key_len = saved_len;
		if (klen >= LN_JSON_KEYBUF || saved_len + 1 + klen >= LN_JSON_KEYBUF) {
			/* klen is input-derived: reject it alone first so the sum below
			 * cannot wrap past the bound. In validate-only mode the dotted key
			 * is unused, so an over-long path is not a reason to reject
			 * otherwise-valid JSON; the key is simply not built. */
			if (!c->validate_only) return -1;
		} else {
			if (saved_len > 0) c->key[c->key_len++] = '.';
			memcpy(c->key + c->key_len, kstr, klen);
			c->key_len += klen;
		}

		json_skip_ws(c);
		if (c->pos >= c->len || c->buf[c->pos] != ':') return -1;
		c->pos++;                                   /* ':' */

		if (json_parse_value(c, depth + 1) != 0) return -1;

		c->key_len = saved_len;                     /* pop member key */

		json_skip_ws(c);
		if (c->pos >= c->len) return -1;
		if (c->buf[c->pos] == ',') { c->pos++; continue; }
		if (c->buf[c->pos] == '}') { c->pos++; return 0; }
		return -1;
	}
}

/*
 * Parse a JSON array body (pos is just past '['). Each element is parsed in
 * turn to validate it and advance the cursor. In flatten mode scalar elements
 * are keyed by their index (.0, .1, ...).
 */
static int
json_parse_array(ln_json_ctx_t *c, int depth)
{
	size_t saved_len = c->key_len;
	int idx = 0;

	json_skip_ws(c);
	if (c->pos < c->len && c->buf[c->pos] == ']') { c->pos++; return 0; }

	for (;;) {
		char ib[24];
		int n;

		json_skip_ws(c);
		if (c->pos >= c->len) return -1;

		/* index-suffix key for scalar elements; fail (backtrack the line)
		 * on overflow rather than silently mislabelling the element. */
		c->key_len = saved_len;
		n = snprintf(ib, sizeof(ib), ".%d", idx);
		if (n <= 0 || saved_len + (size_t)n >= LN_JSON_KEYBUF) {
			/* validate-only: dotted key unused, over-long path is not fatal */
			if (!c->validate_only) return -1;
		} else {
			memcpy(c->key + c->key_len, ib, (size_t)n);
			c->key_len += (size_t)n;
		}

		if (json_parse_value(c, depth + 1) != 0) return -1;
		c->key_len = saved_len;

		json_skip_ws(c);
		if (c->pos >= c->len) return -1;
		if (c->buf[c->pos] == ',') { c->pos++; idx++; continue; }
		if (c->buf[c->pos] == ']') { c->pos++; return 0; }
		return -1;
	}
}

static int
json_parse_value(ln_json_ctx_t *c, int depth)
{
	if (depth >= LN_JSON_INPUT_MAX_DEPTH) return -1;
	json_skip_ws(c);
	if (c->pos >= c->len) return -1;

	switch (c->buf[c->pos]) {
	case '{':
		c->pos++;
		return json_parse_object(c, depth);
	case '[': {
		size_t start = c->pos;
		bool saved_validate_only = c->validate_only;
		int rc;

		/* A top-level array has no key to hang the value on; merging one
		 * into the parent object is not meaningful, so leave that case on
		 * the original path. */
		if (c->key_len == 0) {
			c->pos++;
			return json_parse_array(c, depth);
		}
		/* Walk the array to validate it and find its end without emitting
		 * per-element fields, then store the whole span. */
		c->validate_only = true;
		c->pos++;
		rc = json_parse_array(c, depth);
		c->validate_only = saved_validate_only;
		if (rc != 0) return -1;
		if (json_emit_rawjson(c, c->buf + start, c->pos - start) == 0)
			return 0;
		/* The span did not fit the per-message arena (it is sized for a
		 * whole message, and a large array can exceed what is left).
		 * Fall back to per-element flattening, which allocates in small
		 * pieces: the array shape is lost, but no data is. */
		c->pos = start + 1;
		return json_parse_array(c, depth);
	}
	case '"': {
		const char *s; size_t sl;
		c->pos++;
		if (json_scan_string(c, &s, &sl) != 0) return -1;
		json_emit_string(c, s, sl);
		return 0;
	}
	case 't':
		if (c->len - c->pos >= 4 && memcmp(c->buf + c->pos, "true", 4) == 0) {
			json_emit_bool(c, true); c->pos += 4; return 0;
		}
		return -1;
	case 'f':
		if (c->len - c->pos >= 5 && memcmp(c->buf + c->pos, "false", 5) == 0) {
			json_emit_bool(c, false); c->pos += 5; return 0;
		}
		return -1;
	case 'n':
		if (c->len - c->pos >= 4 && memcmp(c->buf + c->pos, "null", 4) == 0) {
			json_emit_null(c);
			c->pos += 4; return 0;
		}
		return -1;
	default:
		return json_parse_number(c);     /* number or invalid */
	}
}

/*
 * Handle a JSON field. Three name contracts, matching the standard parser
 * (doc/configuration.rst "Special field names"):
 *
 *   "-" / empty  matched, not stored (same as any other discarded field).
 *   "."          inline: object keys become leaves at the current context
 *                (root at top level). This is the v1 merge for parsers that
 *                return a set of fields. Turbo walks the object once into
 *                the arena so mmenrich/mmwake can ln_fast_result_get_string.
 *   other        store the value as one LN_FFIELD_RAW_JSON field so arrays,
 *                booleans, nulls and full-precision numbers round-trip like v1.
 *
 * Malformed JSON fails the rule. Returns 0 on success, -1 on parse error.
 * *out_consumed is the byte span of the JSON value.
 */
static int
vm_json_flatten(ln_vm_t *vm, const char *field_name,
		const char *buf, size_t len, size_t *out_consumed)
{
	ln_json_ctx_t c;
	uint16_t fn_len;
	const char *name;
	const char *val;
	size_t start, vlen;
	int discarded, inlined;

	if (len == 0) return -1;

	discarded = (field_name == NULL || field_name[0] == '\0'
	    || (field_name[0] == '-' && field_name[1] == '\0'));
	inlined = (field_name != NULL && field_name[0] == '.'
	    && field_name[1] == '\0');

	memset(&c, 0, sizeof(c));
	c.vm  = vm;
	c.buf = buf;
	c.len = len;
	c.pos = 0;

	json_skip_ws(&c);
	start = c.pos;
	if (start >= len || (buf[start] != '{' && buf[start] != '[')) return -1;

	if (discarded) {
		c.validate_only = true;
		if (json_parse_value(&c, 0) != 0) return -1;
		*out_consumed = c.pos;
		return 0;
	}

	if (inlined) {
		/* v1 "." : emit dotted leaves at the current context (root here). */
		c.validate_only = false;
		if (json_parse_value(&c, 0) != 0) return -1;
		*out_consumed = c.pos;
		return 0;
	}

	/* Named %field:json%: validate, then store as one RAW JSON field. */
	c.validate_only = true;
	if (json_parse_value(&c, 0) != 0) return -1;
	*out_consumed = c.pos;
	vlen = c.pos - start;

	if (vm->result == NULL) return 0;
	if (vm->result->n_fields >= LN_FAST_MAX_FIELDS) {
		vm->result->flags |= LN_FRESULT_TRUNCATED;
		return 0;
	}

	name = vm_build_field_name(vm, field_name, &fn_len);
	if (name == NULL || fn_len == 0) return 0;

	name = ln_arena_strndup(vm->arena, name, fn_len);
	val  = ln_arena_strndup(vm->arena, buf + start, vlen);
	if (name == NULL || val == NULL) return 0;

	ln_fast_add_rawjson_static(vm->result, name, fn_len, val, (uint32_t)vlen);
	return 0;
}

static inline int
parse_mac48(const char *buf, size_t len, size_t *out_len)
{
	char sep;
	size_t i;
	int octet;

	if (len < 17) return -1;
	sep = 0;
	i = 0;

	for (octet = 0; octet < 6; octet++) {
		if (!isxdigit((unsigned char)buf[i]) ||
			!isxdigit((unsigned char)buf[i + 1]))
			return -1;
		i += 2;
		if (octet < 5) {
			if (sep == 0) {
				if (buf[i] == ':' || buf[i] == '-') sep = buf[i];
				else return -1;
			} else if (buf[i] != sep) return -1;
			i++;
		}
	}
	*out_len = i;
	return 0;
}

/*============================================================================
 * Instruction Execution (legacy switch-based, kept for reference/fallback)
 *============================================================================
 *
 * NOTE: The primary execution path is now ln_vm_continue() below, which
 * uses computed goto dispatch (via turbo_vm_opt.h macros).
 * vm_exec_instr() is retained for:
 *   - Disassembly/debug tools that need per-instruction stepping
 *   - Compilers without computed goto support (MSVC fallback path)
 *
 * When LN_VM_COMPUTED_GOTO == 1, ln_vm_continue() bypasses this function
 * entirely and dispatches inline for maximum throughput.
 *============================================================================*/

static int __attribute__((unused))
vm_exec_instr(ln_vm_t *vm)
{
	const ln_instr_t *inst;
	size_t remaining;

	if (UNLIKELY(vm->pc >= vm->prog->code_len)) {
		vm->error = "PC out of bounds";
		return -1;
	}

	inst = &vm->prog->code[vm->pc];
	remaining = (size_t)(vm->input_end - vm->ip);

	vm->instr_count++;

	TRACE("[%u] %s ip=%zu rem=%zu ctx_sp=%u\n", vm->pc, ln_opcode_name(inst->op),
		  (size_t)(vm->ip - vm->input), remaining, vm->field_ctx_sp);

	switch (inst->op) {

	/*=== Control ===*/

	case OP_HALT:
		return -1;

	case OP_MATCH:
		vm->matched_rule = inst->data.str;
		if (vm->result) {
			ln_fast_set_rule_id(vm->result, inst->data.str);
		}
		return 0;

	case OP_JUMP:
		vm->pc += inst->data.jump.offset;
		return 1;

	case OP_FORK: {
		uint32_t alt_pc = vm->pc + inst->data.jump.offset;
		if (!vm_push_fork(vm, alt_pc)) return -1;
		vm->pc++;
		return 1;
	}

	case OP_FAIL:
		return -1;

	case OP_CALL: {
		if (vm->call_sp >= LN_VM_MAX_CALLS) {
			vm->error = "call stack overflow";
			return -1;
		}
		vm->calls[vm->call_sp++] = vm->pc + 1;
		vm->pc += inst->data.jump.offset;
		return 1;
	}

	case OP_RET: {
		if (vm->call_sp == 0) {
			vm->error = "call stack underflow";
			return -1;
		}
		vm->pc = vm->calls[--vm->call_sp];
		return 1;
	}

	/*=== Field Context ===*/

	case OP_CTX_PUSH: {
		if (!vm_push_field_ctx(vm, turbo_iname(vm, inst, inst->data.str), false)) return -1;
		vm->pc++;
		return 1;
	}

	case OP_CTX_POP: {
		if (!vm_pop_field_ctx(vm)) return -1;
		vm->pc++;
		return 1;
	}

	case OP_CTX_NEST: {
		if (!vm_push_field_ctx(vm, turbo_iname(vm, inst, inst->data.str), true)) return -1;
		vm->pc++;
		return 1;
	}

	case OP_CTX_UNNEST: {
		if (!vm_pop_field_ctx(vm)) return -1;
		vm->pc++;
		return 1;
	}

	/*=== Literals ===*/

	case OP_LITERAL: {
		uint16_t len = inst->aux;
		/* Bound inline compare to the 60-byte union (security audit #6b). */
		if (len > LN_INSTR_MAX_INLINE) return -1;
		if (remaining < len) return -1;
		if (memcmp(vm->ip, inst->data.str, len) != 0) return -1;
		vm->ip += len;
		vm->pc++;
		return 1;
	}

	case OP_LITERAL_CI: {
		uint16_t len = inst->aux;
		/* Bound inline compare to the 60-byte union (security audit #6b). */
		if (len > LN_INSTR_MAX_INLINE) return -1;
		if (remaining < len) return -1;
		for (uint16_t i = 0; i < len; i++) {
			if (tolower((unsigned char)vm->ip[i]) !=
				tolower((unsigned char)inst->data.str[i])) {
				return -1;
			}
		}
		vm->ip += len;
		vm->pc++;
		return 1;
	}

	case OP_CHAR: {
		if (remaining < 1) return -1;
		if (*vm->ip != inst->data.str[0]) return -1;
		vm->ip++;
		vm->pc++;
		return 1;
	}

	case OP_ANY: {
		if (remaining < 1) return -1;
		vm->ip++;
		vm->pc++;
		return 1;
	}

	/*=== Fields ===*/

	case OP_FIELD_WORD: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		ln_span_t span;

		if (ln_simd_word(vm->ip, remaining, &span) != LN_SIMD_OK) return -1;

		vm_add_string_field(vm, name, span.start, span.len);
		vm->ip += span.consumed;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_INT: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		int64_t value;
		size_t len;

		if (parse_number(vm->ip, remaining, &value, &len) != 0) return -1;

		/* Native JSON integer with format="number", the matched string otherwise. */
		if (inst->flags & LN_INSTR_F_NUMERIC)
			vm_add_int_field(vm, name, value);
		else
			vm_add_string_field(vm, name, vm->ip, len);
		vm->ip += len;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_UINT: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		int64_t value;
		size_t len;

		if (parse_number(vm->ip, remaining, &value, &len) != 0) return -1;
		if (vm->ip[0] == '-') return -1;

		vm_add_int_field(vm, name, value);
		vm->ip += len;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_FLOAT: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		size_t len;

		if (parse_float(vm->ip, remaining, &len) != 0) return -1;

		/* Native JSON number (verbatim) with format="number", string otherwise. */
		if (inst->flags & LN_INSTR_F_NUMERIC)
			vm_add_rawjson_field(vm, name, vm->ip, len);
		else
			vm_add_string_field(vm, name, vm->ip, len);
		vm->ip += len;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_IPV4: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		size_t len;

		if (parse_ipv4(vm->ip, remaining, &len) != 0) return -1;

		vm_add_string_field(vm, name, vm->ip, len);
		vm->ip += len;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_IPV6: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		size_t len;

		if (parse_ipv6(vm->ip, remaining, &len) != 0) return -1;

		vm_add_string_field(vm, name, vm->ip, len);
		vm->ip += len;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_HEX: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		int64_t value;
		size_t len;

		if (parse_hex(vm->ip, remaining, &value, &len) != 0) return -1;

		/* Native JSON integer (decimal) with format="number", the matched hex
		 * literal as a string otherwise. */
		if (inst->flags & LN_INSTR_F_NUMERIC)
			vm_add_int_field(vm, name, value);
		else
			vm_add_string_field(vm, name, vm->ip, len);
		vm->ip += len;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_QUOTED: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		size_t start, len, consumed;

		if (parse_op_quoted(vm->ip, remaining, &start, &len, &consumed) != 0) return -1;

		vm_add_string_field(vm, name, vm->ip + start, len);
		vm->ip += consumed;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_CHAR_TO: {
		const char *name = turbo_iname(vm, inst, inst->data.char_to.name);
		char delim = (char)inst->data.char_to.delim;
		ln_span_t span;

		if (ln_simd_char_to(vm->ip, remaining, delim, &span) != LN_SIMD_OK)
			return -1;

		vm_add_string_field(vm, name, span.start, span.len);
		vm->ip += span.consumed;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_STR_TO: {
		const char *name = turbo_iname(vm, inst, inst->data.str_to.name);
		const char *delim;
		size_t len;

		/* Test the pool before indexing it: forming strpool + off on a
		 * NULL pool is undefined even when the result is never read, and
		 * lets a compiler drop the check that follows. */
		if (!vm->prog->strpool) return -1;
		delim = vm->prog->strpool + inst->data.str_to.delim_off;
		if (parse_string_to(vm->ip, remaining, delim, inst->aux, &len) != 0)
			return -1;

		vm_add_string_field(vm, name, vm->ip, len);
		vm->ip += len;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_REST: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		vm_add_string_field(vm, name, vm->ip, remaining);
		vm->ip += remaining;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_DATE: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		ln_span_t span;
		int rc;

		/* format="timestamp-unix"[-ms] needs an epoch conversion the VM
		 * does not implement; decline so the walker serves this message
		 * rather than storing the raw text the walker would not store. */
		if (inst->flags & LN_INSTR_F_DATE_FMT) return -1;

		rc = ln_simd_timestamp(vm->ip, remaining, &span, NULL);
		if (rc != LN_SIMD_OK) return -1;

		vm_add_string_field(vm, name, span.start, span.len);
		vm->ip += span.consumed;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_JSON: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		size_t len;

		/* SIMD-flatten nested JSON into dotted flat keys. */
		if (vm_json_flatten(vm, name, vm->ip, remaining, &len) != 0)
			return -1;

		vm->ip += len;
		vm->pc++;
		return 1;
	}

	case OP_FIELD_MAC: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		size_t len;

		if (parse_mac48(vm->ip, remaining, &len) != 0) return -1;

		vm_add_string_field(vm, name, vm->ip, len);
		vm->ip += len;
		vm->pc++;
		return 1;
	}

	/*=== Name-Value-List ===*/

	case OP_FIELD_NAME_VALUE: {
		/*
		 * Parse name=value pairs from the remaining input.
		 * Supports: name=value, name="value", name='value', name= (empty)
		 * Separator between pairs: whitespace (default) or custom char.
		 * Assignator between name/value: '=' (default) or custom char.
		 *
		 * Each extracted field is added with the context prefix from the
		 * instruction's name field (e.g., "sns" → fields become "sns.key").
		 *
		 * Uses SIMD primitives for scanning where possible.
		 */
		const char *ctx_name = turbo_iname(vm, inst, inst->data.char_to.name);
		const char sep = (char)inst->data.char_to.delim;  /* 0 = whitespace */
		const char ass = (char)inst->data.char_to.ass;     /* 0 = '=' */
		const char ass_char = ass ? ass : '=';
		const int ignore_ws = inst->data.char_to.ignore_ws;
		const char *p;
		const char *end;
		int n_pairs;
		size_t consumed;

		/* Push the context so fields are nested under ctx_name */
		if (ctx_name[0]) {
			vm_push_field_ctx(vm, ctx_name, false);
		}

		p = vm->ip;
		end = vm->ip + remaining;
		n_pairs = 0;

		while (p < end) {
			/* --- Parse name --- */
			const char *name_start;
			size_t name_end_off; /* raw distance to assignator */
			size_t name_len;     /* stored name length (may be trimmed) */
			const char *val_start;
			size_t val_len;
			char *arena_name;

			/* ignore_whitespaces: skip leading whitespace before the name
			 * (SIMD-accelerated; same whitespace class as isspace). */
			if (ignore_ws) {
				p += ln_simd_skip_space(p, (size_t)(end - p));
				if (p >= end) break;
			}
			name_start = p;

			/* Scan for assignator char using SIMD find_char */
			name_end_off = ln_simd_find_char(p, (size_t)(end - p), ass_char);
			if (name_end_off >= (size_t)(end - p)) {
				/* No more assignator found: done */
				break;
			}

			/* Validate name: must be non-empty */
			name_len = name_end_off;
			if (name_len == 0) break;

			if (ass == 0) {
				/* Default mode: name must be a contiguous run of valid
				 * chars immediately followed by '=' (matches the
				 * standard parser; no trailing-whitespace trim here). */
				int valid = 1;
				size_t k;
				for (k = 0; k < name_len; k++) {
					unsigned char c = (unsigned char)name_start[k];
					if (!(isalnum(c) || c == '.' || c == '_' || c == '-')) {
						valid = 0;
						break;
					}
				}
				if (!valid) break;
			} else if (ignore_ws) {
				/* Custom assignator: name is anything before ass; trim its
				 * trailing whitespace to match the standard parser. */
				while (name_len > 0
					&& isspace((unsigned char)name_start[name_len - 1]))
					name_len--;
				if (name_len == 0) break;
			}

			p += name_end_off + 1; /* skip name span + assignator */

			/* ignore_whitespaces: skip whitespace between assignator and
			 * value (SIMD-accelerated). */
			if (ignore_ws) {
				p += ln_simd_skip_space(p, (size_t)(end - p));
			}

			/* --- Parse value --- */
			if (p < end && (*p == '"' || *p == '\'')) {
				/* Quoted value */
				char quote = *p;
				int backslashes = 0;
				p++; /* skip opening quote */
				val_start = p;
				/* Scan for closing quote, handling backslash escapes */
				while (p < end) {
					if (*p == quote && (backslashes % 2 == 0)) break;
					if (*p == '\\') backslashes++;
					else backslashes = 0;
					p++;
				}
				val_len = (size_t)(p - val_start);
				if (p < end && *p == quote) {
					p++; /* skip closing quote */
				} else {
					/* Unterminated quote: fail */
					break;
				}
			} else {
				/* Unquoted value: scan to separator */
				val_start = p;
				if (sep) {
					/* Custom separator */
					size_t off = ln_simd_find_char(p, (size_t)(end - p), sep);
					val_len = (off < (size_t)(end - p)) ? off : (size_t)(end - p);
				} else {
					/* Whitespace separator: scan to first whitespace */
					val_len = 0;
					while (p + val_len < end && !isspace((unsigned char)p[val_len]))
						val_len++;
				}
				p += val_len;
				/* ignore_whitespaces: trim trailing whitespace from an
				 * unquoted value (quoted values are kept verbatim). */
				if (ignore_ws) {
					while (val_len > 0
						&& isspace((unsigned char)val_start[val_len - 1]))
						val_len--;
				}
			}

			/* --- Store the field --- */
			/* Arena-allocate name so it outlives this stack frame */
			arena_name = ln_arena_strndup(vm->arena, name_start, name_len);
			if (!arena_name) break;

			/* Add field directly (context prefix handled by vm_add_string_field) */
			vm_add_string_field(vm, arena_name, val_start, val_len);
			n_pairs++;

			/* --- Advance to the next pair ---
			 * Matches the standard parser: with ignore_whitespaces and a
			 * custom separator, skip whitespace before the separator; then
			 * a separator (whitespace when sep == 0) must follow, otherwise
			 * stop. Finally consume the separator run. */
			if (ignore_ws && sep) {
				p += ln_simd_skip_space(p, (size_t)(end - p));
			}
			if (p < end
				&& !(sep ? (*p == sep) : isspace((unsigned char)*p)))
				break;
			if (sep) {
				while (p < end && *p == sep) p++;
			} else {
				while (p < end && isspace((unsigned char)*p)) p++;
			}
		}

		/* Pop context */
		if (ctx_name[0]) {
			vm_pop_field_ctx(vm);
		}

		/* Must have parsed at least one pair */
		if (n_pairs == 0) return -1;

		consumed = (size_t)(p - vm->ip);
		vm->ip += consumed;
		vm->pc++;
		return 1;
	}

	/*=== Skipping ===*/

	case OP_SKIP_SPACE: {
		size_t ws = ln_simd_skip_space(vm->ip, remaining);
		if (ws == 0) return -1;   /* whitespace field requires one or more */
		if (inst->flags & LN_INSTR_F_STORE)
			vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), vm->ip, ws);
		vm->ip += ws;
		vm->pc++;
		return 1;
	}

	case OP_SKIP_SPACE1: {
		size_t skipped;
		if (remaining == 0 || !isspace((unsigned char)vm->ip[0])) return -1;
		skipped = 1;
		while (skipped < remaining && isspace((unsigned char)vm->ip[skipped]))
			skipped++;
		vm->ip += skipped;
		vm->pc++;
		return 1;
	}

	case OP_SKIP_N: {
		uint16_t n = inst->aux;
		if (remaining < n) return -1;
		vm->ip += n;
		vm->pc++;
		return 1;
	}

	case OP_SKIP_TO: {
		char c = inst->data.str[0];
		size_t pos = 0;
		while (pos < remaining && vm->ip[pos] != c) pos++;
		if (pos >= remaining) return -1;
		vm->ip += pos;
		vm->pc++;
		return 1;
	}

	case OP_SKIP_PAST: {
		char c = inst->data.str[0];
		size_t pos = 0;
		while (pos < remaining && vm->ip[pos] != c) pos++;
		if (pos >= remaining) return -1;
		vm->ip += pos + 1;
		vm->pc++;
		return 1;
	}

	case OP_SKIP_LINE: {
		size_t pos = 0;
		while (pos < remaining && vm->ip[pos] != '\n') pos++;
		vm->ip += (pos < remaining) ? pos + 1 : remaining;
		vm->pc++;
		return 1;
	}

	/*=== Tags ===*/

	case OP_TAG: {
		if (vm->result && inst->data.str[0]) {
			ln_fast_add_tag(vm->result, inst->data.str);
		}
		vm->pc++;
		return 1;
	}

	case OP_RULE_ID: {
		if (vm->result && inst->data.str[0]) {
			ln_fast_set_rule_id(vm->result, inst->data.str);
		}
		vm->pc++;
		return 1;
	}

	case OP_STATIC_FIELD: {
		/* Add a pre-baked key=value field from compile-time annotation resolution.
		 * key is in data.kv.key (null-terminated), key length in aux.
		 * value is in data.kv.val (null-terminated).
		 * Zero per-message cost beyond the ln_fast_add_string_static call. */
		if (vm->result) {
			const int annot = (inst->flags & LN_INSTR_F_ANNOT) != 0;

			if (inst->flags & LN_INSTR_F_KV_POOL) {
				if (vm->prog->strpool) {
					const char *const k = vm->prog->strpool
							    + inst->data.kv_pool.key_off;
					const char *const v = vm->prog->strpool
							    + inst->data.kv_pool.val_off;

					if (annot)
						ln_fast_set_string_static(vm->result, k,
								inst->aux, v,
								inst->data.kv_pool.val_len);
					else
						ln_fast_add_string_static(vm->result, k,
								inst->aux, v,
								inst->data.kv_pool.val_len);
				}
			} else if (inst->data.kv.key[0]) {
				const uint32_t vlen =
					(uint32_t)strlen(inst->data.kv.val);

				if (annot)
					ln_fast_set_string_static(vm->result,
							inst->data.kv.key, inst->aux,
							inst->data.kv.val, vlen);
				else
					ln_fast_add_string_static(vm->result,
							inst->data.kv.key, inst->aux,
							inst->data.kv.val, vlen);
			}
		}
		vm->pc++;
		return 1;
	}

	/*=== Assertions ===*/

	case OP_ASSERT_CHAR: {
		if (remaining < 1) return -1;
		if (*vm->ip != inst->data.str[0]) return -1;
		vm->pc++;
		return 1;
	}

	case OP_ASSERT_END: {
		if (remaining > 0) return -1;
		vm->pc++;
		return 1;
	}

	case OP_ASSERT_START: {
		if (vm->ip != vm->input) return -1;
		vm->pc++;
		return 1;
	}

	/*=== Special ===*/

	case OP_SYSLOG_PRI: {
		const char *p;
		const char *end;
		uint32_t pri;
		int digits;
		uint8_t facility;
		uint8_t severity;

		if (remaining < 3) return -1;
		if (vm->ip[0] != '<') return -1;

		p = vm->ip + 1;
		end = vm->input_end;
		pri = 0;
		digits = 0;

		while (p < end && *p >= '0' && *p <= '9' && digits < 3) {
			pri = pri * 10 + (*p - '0');
			p++;
			digits++;
		}

		if (p >= end || *p != '>' || digits == 0) return -1;
		if (pri > 191) return -1;
		p++;

		facility = pri / 8;
		severity = pri % 8;

		vm_add_int_field(vm, "facility", facility);
		vm_add_int_field(vm, "severity", severity);

		vm->ip = p;
		vm->pc++;
		return 1;
	}

	/*=== iptables name=value ===*/

	case OP_V2_IPTABLES: {
		const char *ctx_name = turbo_iname(vm, inst, inst->data.str);
		const char *p = vm->ip;
		const char *end = vm->input_end;
		int n_pairs = 0;

		if (ctx_name[0]) {
			vm_push_field_ctx(vm, ctx_name, false);
		}

		while (p < end) {
			const char *name_start;
			size_t name_len;
			size_t eq_off;
			const char *val_start;
			size_t val_len;
			char *arena_name;

			/* Skip leading spaces */
			while (p < end && *p == ' ') p++;
			if (p >= end) break;

			name_start = p;

			/* Scan for '=' or space (flag without value) */
			eq_off = ln_simd_find_char(p, (size_t)(end - p), '=');

			/* Check if a space comes before '=' (flag) */
			{
				size_t sp_off;
				sp_off = ln_simd_find_char(p, (size_t)(end - p), ' ');
				if (sp_off < eq_off) {
					/* Flag: name without '=', store as null */
					name_len = sp_off;
					if (name_len == 0) break;
					arena_name = ln_arena_strndup(vm->arena,
								name_start, name_len);
					if (!arena_name) break;
					vm_add_null_field(vm, arena_name);
					n_pairs++;
					p += sp_off;
					continue;
				}
			}

			if (eq_off >= (size_t)(end - p)) {
				/* No '=' found: check if rest is a flag */
				name_len = (size_t)(end - p);
				if (name_len == 0) break;
				arena_name = ln_arena_strndup(vm->arena,
							name_start, name_len);
				if (!arena_name) break;
				vm_add_null_field(vm, arena_name);
				n_pairs++;
				p = end;
				break;
			}

			name_len = eq_off;
			if (name_len == 0) break;

			/* Validate name: alphanumeric + underscore */
			{
				int valid = 1;
				size_t k;
				for (k = 0; k < name_len; k++) {
					unsigned char c = (unsigned char)p[k];
					if (!(isalnum(c) || c == '_')) {
						valid = 0;
						break;
					}
				}
				if (!valid) break;
			}

			p += name_len + 1; /* skip name + '=' */

			/* Parse value: everything until space or end */
			val_start = p;
			{
				size_t sp_off;
				sp_off = ln_simd_find_char(p, (size_t)(end - p), ' ');
				val_len = (sp_off < (size_t)(end - p))
						  ? sp_off : (size_t)(end - p);
			}
			p += val_len;

			/* Store field */
			arena_name = ln_arena_strndup(vm->arena, name_start, name_len);
			if (!arena_name) break;
			vm_add_string_field(vm, arena_name, val_start, val_len);
			n_pairs++;
		}

		if (ctx_name[0]) {
			vm_pop_field_ctx(vm);
		}

		/* Require minimum 2 pairs */
		if (n_pairs < 2) return -1;

		vm->ip = p;
		vm->pc++;
		return 1;
	}

	/*=== CEE-syslog ===*/

	case OP_CEE_SYSLOG: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		const char *p = vm->ip;
		size_t rem = remaining;
		size_t json_len;

		/* Must start with "@cee:" (5 chars) */
		if (rem < 6) return -1;
		if (memcmp(p, "@cee:", 5) != 0) return -1;
		p += 5;
		rem -= 5;

		/* Skip optional whitespace after ':' */
		while (rem > 0 && (*p == ' ' || *p == '\t')) {
			p++;
			rem--;
		}

		/* Must have '{' for JSON object */
		if (rem == 0 || *p != '{') return -1;

		/* Parse JSON body */
		if (parse_json(p, rem, &json_len) != 0) return -1;

		/* JSON must consume rest of input */
		if (json_len != rem) return -1;

		vm_add_string_field(vm, name, p, json_len);
		vm->ip = p + json_len;
		vm->pc++;
		return 1;
	}

	/*=== Checkpoint LEA ===*/

	case OP_CHECKPOINT_LEA: {
		const char *ctx_name = turbo_iname(vm, inst, inst->data.char_to.name);
		const char term = (char)inst->data.char_to.delim;
		const char *p = vm->ip;
		const char *end = vm->input_end;
		int n_pairs = 0;

		if (ctx_name[0]) {
			vm_push_field_ctx(vm, ctx_name, false);
		}

		while (p < end) {
			const char *name_start;
			size_t name_len;
			size_t colon_off;
			const char *val_start;
			size_t val_len;
			size_t semi_off;
			char *arena_name;

			/* Skip leading spaces */
			while (p < end && *p == ' ') p++;
			if (p >= end) break;

			/* Check for early terminator */
			if (term && *p == term) break;

			name_start = p;

			/* Scan for ':' */
			colon_off = ln_simd_find_char(p, (size_t)(end - p), ':');
			if (colon_off >= (size_t)(end - p)) break;

			name_len = colon_off;
			if (name_len == 0) break;

			p += name_len + 1; /* skip name + ':' */

			/* Skip spaces after ':' */
			while (p < end && *p == ' ') p++;

			/* Scan for ';' */
			val_start = p;
			semi_off = ln_simd_find_char(p, (size_t)(end - p), ';');
			if (semi_off >= (size_t)(end - p)) {
				/* No semicolon: take rest as value */
				val_len = (size_t)(end - p);
				p = end;
			} else {
				val_len = semi_off;
				p += val_len + 1; /* skip value + ';' */
			}

			/* Store field */
			arena_name = ln_arena_strndup(vm->arena, name_start, name_len);
			if (!arena_name) break;
			vm_add_string_field(vm, arena_name, val_start, val_len);
			n_pairs++;
		}

		if (ctx_name[0]) {
			vm_pop_field_ctx(vm);
		}

		/* Require minimum 1 pair */
		if (n_pairs < 1) return -1;

		vm->ip = p;
		vm->pc++;
		return 1;
	}

	/*=== CEF header ===*/

	case OP_CEF_HDR: {
		const char *name = turbo_iname(vm, inst, inst->data.str);
		const char *p = vm->ip;
		const char *end = vm->input_end;
		size_t rem = remaining;
		int hdr_idx;

		/* Fixed header field names */
		static const char *hdr_names[6] = {
			"DeviceVendor", "DeviceProduct", "DeviceVersion",
			"SignatureID", "Name", "Severity"
		};

		/* Must start with "CEF:0|" (6 chars) */
		if (rem < 6) return -1;
		if (memcmp(p, "CEF:0|", 6) != 0) return -1;
		p += 6;

		if (name[0]) {
			vm_push_field_ctx(vm, name, false);
		}

		/* Parse 6 pipe-delimited header fields with escape handling */
		for (hdr_idx = 0; hdr_idx < 6; hdr_idx++) {
			const char *field_start = p;
			const char *field_val;
			size_t field_len;

			/* Scan for unescaped '|' or end of input */
			while (p < end) {
				if (*p == '\\' && p + 1 < end) {
					p += 2; /* skip escaped char */
					continue;
				}
				if (*p == '|') break;
				p++;
			}

			field_len = (size_t)(p - field_start);
			if (cef_unescape(vm, field_start, field_len, 0,
							 &field_val, &field_len) != 0) {
				if (name[0]) vm_pop_field_ctx(vm);
				return -1;
			}
			vm_add_string_field(vm, hdr_names[hdr_idx],
								field_val, field_len);

			if (hdr_idx < 5) {
				/* Expect pipe separator between fields */
				if (p >= end || *p != '|') {
					if (name[0]) vm_pop_field_ctx(vm);
					return -1;
				}
				p++; /* skip '|' */
			} else {
				/* After last header field, skip '|' if present */
				if (p < end && *p == '|') p++;
			}
		}

		/* Parse extensions: key=value pairs separated by spaces.
		 * Push "Extensions" sub-context. */
		if (p < end) {
			/* Extensions nest under the field, so compose "<field>.Extensions"
			 * (the context stack tracks only the top level). */
			const char *ext_ctx = "Extensions";
			int n_ext;

			/* Push the plain segment: the context stack composes the
			 * full path itself. */
			vm_push_field_ctx(vm, ext_ctx, false);
			n_ext = 0;

			while (p < end) {
				const char *key_start;
				size_t eq_off;
				size_t key_len;
				const char *val_start;
				size_t val_len;
				const char *next_eq;
				const char *key_eq;
				const char *val_span;
				const char *scan;
				char *arena_key;

				/* Skip spaces */
				while (p < end && *p == ' ') p++;
				if (p >= end) break;

				key_start = p;

				/* Find '=' */
				eq_off = ln_simd_find_char(p, (size_t)(end - p), '=');
				if (eq_off >= (size_t)(end - p)) break;

				key_len = eq_off;
				if (key_len == 0) break;

				p += key_len + 1; /* skip key + '=' */

				/* Value: greedy, take everything until next key=
				 * Scan backwards from next '=' to find the space
				 * before the next key. */
				val_start = p;
				next_eq = cef_find_unescaped(val_start, val_start, end, '=');

				if (next_eq && next_eq > val_start) {
					/* Walk back from '=' to find space before key */
					const char *kstart = next_eq;
					while (kstart > val_start && *(kstart - 1) != ' ')
						kstart--;
					if (kstart > val_start) {
						val_len = (size_t)(kstart - val_start);
						/* Trim trailing space */
						while (val_len > 0 &&
							   val_start[val_len - 1] == ' ')
							val_len--;
						p = kstart;
					} else {
						val_len = (size_t)(end - val_start);
						p = end;
					}
				} else {
					val_len = (size_t)(end - val_start);
					p = end;
				}

				arena_key = ln_arena_strndup(vm->arena,
							key_start, key_len);
				if (!arena_key) break;
				if (cef_unescape(vm, val_start, val_len, 1,
								 &val_span, &val_len) != 0) {
					vm_pop_field_ctx(vm);
					if (name[0]) vm_pop_field_ctx(vm);
					return -1;
				}
				vm_add_string_field(vm, arena_key,
									val_span, val_len);
				n_ext++;
			}

			vm_pop_field_ctx(vm); /* pop Extensions */
			(void)n_ext;
		}

		if (name[0]) {
			vm_pop_field_ctx(vm);
		}

		vm->ip = p;
		vm->pc++;
		return 1;
	}

	/*=== Debug ===*/

	case OP_NOP:
		vm->pc++;
		return 1;

	case OP_DEBUG:
		fprintf(stderr, "[DEBUG] pc=%u ip=%zu ctx_sp=%u\n",
				vm->pc, (size_t)(vm->ip - vm->input), vm->field_ctx_sp);
		vm->pc++;
		return 1;

	default: {
		snprintf(vm->error_buf, sizeof(vm->error_buf), "unknown opcode 0x%02x at pc=%u", inst->op, vm->pc);
		vm->error = vm->error_buf;
		return -1;
	}
	}
}

/*============================================================================
 * Main Execution Loop
 *============================================================================
 *
 * Two implementations:
 *
 * 1. COMPUTED GOTO (GCC/Clang): Flat dispatch loop with direct jumps
 *    between opcode handlers. Each handler ends with NEXT() which
 *    increments pc and jumps directly to the next handler, no loop
 *    overhead, no switch bounds check. Failed handlers jump to
 *    backtrack: which pops the fork stack or returns NOMATCH.
 *
 * 2. SWITCH FALLBACK (MSVC): Traditional loop calling vm_exec_instr().
 *    Same semantics, ~20-30% slower dispatch.
 *============================================================================*/

int
ln_vm_exec(ln_vm_t *vm, const ln_program_t *prog,
		   const char *input, size_t len,
		   ln_fast_result_t *result)
{
	size_t leading_ws;

	if (UNLIKELY(!vm || !prog || !prog->code || !input)) {
		return LN_VM_ERROR;
	}

	vm->prog = prog;
	vm->input = input;
	vm->input_end = input + len;
	vm->pc = 0;
	vm->ip = input;
	vm->fork_sp = 0;
	vm->call_sp = 0;
	vm->field_ctx_sp = 0;
	vm->result = result;
	vm->instr_count = 0;
	vm->backtrack_count = 0;
	vm->matched_rule = NULL;
	vm->error = NULL;

	/* Skip leading whitespace */
	leading_ws = ln_simd_skip_space(vm->ip, (size_t)(vm->input_end - vm->ip));
	vm->ip += leading_ws;

	return ln_vm_continue(vm);
}

#if LN_VM_COMPUTED_GOTO

/*
 * Computed goto dispatch loop.
 *
 * All opcode handlers are labels within this single function.
 * The dispatch table maps opcode bytes → label addresses.
 * Each handler ends with NEXT() (success → advance pc and dispatch)
 * or BACKTRACK() (failure → pop fork stack or return NOMATCH).
 *
 * Local variables used by handlers:
 *   inst       - current instruction pointer
 *   remaining  - bytes remaining in input
 *   pc         - program counter (local copy, written back to vm on exit)
 *   prog       - program pointer (const, avoids vm-> indirection)
 *
 * The hot path is: DISPATCH → handler → NEXT → DISPATCH → ...
 * Each DISPATCH does: prefetch(code[pc+1]), goto *table[code[pc].op]
 */
HOT_FUNC
int
ln_vm_continue(ln_vm_t *vm)
{
	const ln_program_t *prog;
	uint32_t pc;
	const char *ip;
	const char *input_end;
	const uint64_t MAX_INSTRUCTIONS = 100000000;
	DISPATCH_INIT();

	if (UNLIKELY(!vm || !vm->prog)) {
		return LN_VM_ERROR;
	}

	/* Hoist frequently accessed fields into local variables */
	prog = vm->prog;
	pc = vm->pc;
	ip = vm->ip;
	input_end = vm->input_end;

	/* Convenience macros for the handler bodies */
	#define REMAINING()  ((size_t)(input_end - ip))
	#define INST()       (&prog->code[pc])
	#define WRITEBACK()  do { vm->pc = pc; vm->ip = ip; } while(0)

	/*
	 * VALIDATE_TARGET (security audit #5): compute a control-flow target
	 * (jump/fork/call/ret) in int64_t and range-check it against
	 * [0, code_len) BEFORE it is used or pushed. This prevents a crafted
	 * relative offset from wrapping uint32_t arithmetic back into a
	 * valid-looking-but-wrong pc. The DISPATCH bounds guard is the
	 * backstop; this yields a clean error at the offending site.
	 * `_t64` is the int64_t target expression; on success `_dst` (a
	 * uint32_t lvalue) receives the validated value.
	 */
	#define VALIDATE_TARGET(_dst, _t64) \
		do { \
			int64_t _tt = (_t64); \
			if (UNLIKELY(_tt < 0 || (uint64_t)_tt >= prog->code_len)) { \
				vm->error = "control-flow target out of bounds"; \
				WRITEBACK(); \
				return LN_VM_ERROR; \
			} \
			(_dst) = (uint32_t)_tt; \
		} while (0)

	/* Start dispatch */
	DISPATCH();

	/*=================================================================
	 * Control Flow Opcodes
	 *=================================================================*/

	CASE(halt)
		WRITEBACK();
		BACKTRACK();

	CASE(match) {
		const ln_instr_t *inst = INST();
		vm->matched_rule = inst->data.str;
		if (vm->result) {
			ln_fast_set_rule_id(vm->result, inst->data.str);
		}
		WRITEBACK();
		return LN_VM_OK;
	}

	CASE(jump) {
		const ln_instr_t *inst = INST();
		VALIDATE_TARGET(pc, (int64_t)pc + inst->data.jump.offset);
		DISPATCH();
	}

	CASE(fork) {
		const ln_instr_t *inst = INST();
		uint32_t alt_pc;
		VALIDATE_TARGET(alt_pc, (int64_t)pc + inst->data.jump.offset);
		vm->pc = pc; vm->ip = ip; /* push_fork reads vm state */
		if (UNLIKELY(!vm_push_fork(vm, alt_pc))) {
			WRITEBACK();
			BACKTRACK();
		}
		pc++;
		DISPATCH();
	}

	CASE(fail)
		WRITEBACK();
		BACKTRACK();

	CASE(call) {
		const ln_instr_t *inst;
		uint32_t ret_pc, target_pc;
		if (UNLIKELY(vm->call_sp >= LN_VM_MAX_CALLS)) {
			vm->error = "call stack overflow";
			WRITEBACK();
			BACKTRACK();
		}
		inst = INST();
		/* Validate both the return addr (pc+1) and the call target
		 * before mutating the call stack (security audit #5). */
		VALIDATE_TARGET(ret_pc, (int64_t)pc + 1);
		VALIDATE_TARGET(target_pc, (int64_t)pc + inst->data.jump.offset);
		vm->calls[vm->call_sp++] = ret_pc;
		pc = target_pc;
		DISPATCH();
	}

	CASE(ret) {
		if (UNLIKELY(vm->call_sp == 0)) {
			vm->error = "call stack underflow";
			WRITEBACK();
			BACKTRACK();
		}
		/* Validate the popped return target (security audit #5). */
		VALIDATE_TARGET(pc, (int64_t)vm->calls[--vm->call_sp]);
		DISPATCH();
	}

	/*=================================================================
	 * Field Context Opcodes
	 *=================================================================*/

	CASE(ctx_push) {
		const ln_instr_t *inst = INST();
		if (UNLIKELY(!vm_push_field_ctx(vm, turbo_iname(vm, inst, inst->data.str), false))) {
			WRITEBACK();
			BACKTRACK();
		}
		NEXT();
	}

	CASE(ctx_pop) {
		if (UNLIKELY(!vm_pop_field_ctx(vm))) {
			WRITEBACK();
			BACKTRACK();
		}
		pc++;
		DISPATCH();
	}

	CASE(ctx_nest) {
		const ln_instr_t *inst = INST();
		if (UNLIKELY(!vm_push_field_ctx(vm, turbo_iname(vm, inst, inst->data.str), true))) {
			WRITEBACK();
			BACKTRACK();
		}
		pc++;
		DISPATCH();
	}

	CASE(ctx_unnest) {
		if (UNLIKELY(!vm_pop_field_ctx(vm))) {
			WRITEBACK();
			BACKTRACK();
		}
		pc++;
		DISPATCH();
	}

	/*=================================================================
	 * Literal Matching Opcodes
	 *=================================================================*/

	CASE(literal) {
		const ln_instr_t *inst = INST();
		uint16_t len;
		len = inst->aux;
		/* Inline literals live in the 60-byte union; a malformed aux must
		 * not let memcmp over-read past data.str (security audit #6b). */
		if (UNLIKELY(len > LN_INSTR_MAX_INLINE)) {
			WRITEBACK();
			BACKTRACK();
		}
		if (UNLIKELY(REMAINING() < len)) {
			WRITEBACK();
			BACKTRACK();
		}
		if (UNLIKELY(memcmp(ip, inst->data.str, len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(literal_ext) {
		/* External literal: same as LITERAL but data is a pointer.
		 * Currently unused in compiled programs, but reserved. */
		WRITEBACK();
		BACKTRACK();
	}

	CASE(literal_ci) {
		const ln_instr_t *inst = INST();
		uint16_t len;
		len = inst->aux;
		/* Bound the inline compare to the 60-byte union (security audit #6b). */
		if (UNLIKELY(len > LN_INSTR_MAX_INLINE)) {
			WRITEBACK();
			BACKTRACK();
		}
		if (UNLIKELY(REMAINING() < len)) {
			WRITEBACK();
			BACKTRACK();
		}
		for (uint16_t i = 0; i < len; i++) {
			if (tolower((unsigned char)ip[i]) !=
				tolower((unsigned char)inst->data.str[i])) {
				WRITEBACK();
				BACKTRACK();
			}
		}
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(char) {
		if (UNLIKELY(REMAINING() < 1)) {
			WRITEBACK();
			BACKTRACK();
		}
		if (UNLIKELY(*ip != INST()->data.str[0])) {
			WRITEBACK();
			BACKTRACK();
		}
		ip++;
		pc++;
		DISPATCH();
	}

	CASE(any) {
		if (UNLIKELY(REMAINING() < 1)) {
			WRITEBACK();
			BACKTRACK();
		}
		ip++;
		pc++;
		DISPATCH();
	}

	CASE(charset) {
		/* External charset bitmap: reserved but not yet used */
		WRITEBACK();
		BACKTRACK();
	}

	/*=================================================================
	 * Field Extraction Opcodes
	 *=================================================================*/

	CASE(field_word) {
		const ln_instr_t *inst = INST();
		ln_span_t span;
		if (UNLIKELY(ln_simd_word(ip, REMAINING(), &span) != LN_SIMD_OK)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip; /* vm_add reads vm->ip */
		vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), span.start, span.len);
		ip += span.consumed;
		pc++;
		DISPATCH();
	}

	CASE(field_int) {
		const ln_instr_t *inst = INST();
		int64_t value;
		size_t len;
		if (UNLIKELY(parse_number(ip, REMAINING(), &value, &len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		/* Native JSON integer with format="number", the matched string otherwise. */
		if (inst->flags & LN_INSTR_F_NUMERIC)
			vm_add_int_field(vm, turbo_iname(vm, inst, inst->data.str), value);
		else
			vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), ip, len);
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(field_uint) {
		const ln_instr_t *inst = INST();
		int64_t value;
		size_t len;
		if (UNLIKELY(parse_number(ip, REMAINING(), &value, &len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		if (UNLIKELY(ip[0] == '-')) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		vm_add_int_field(vm, turbo_iname(vm, inst, inst->data.str), value);
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(field_float) {
		const ln_instr_t *inst = INST();
		size_t len;
		if (UNLIKELY(parse_float(ip, REMAINING(), &len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		/* Native JSON number with format="number" (emitted verbatim to preserve
		 * the serialization), the matched string otherwise. */
		if (inst->flags & LN_INSTR_F_NUMERIC)
			vm_add_rawjson_field(vm, turbo_iname(vm, inst, inst->data.str), ip, len);
		else
			vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), ip, len);
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(field_ipv4) {
		const ln_instr_t *inst = INST();
		size_t len;
		if (UNLIKELY(parse_ipv4(ip, REMAINING(), &len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), ip, len);
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(field_ipv6) {
		const ln_instr_t *inst = INST();
		size_t len;
		if (UNLIKELY(parse_ipv6(ip, REMAINING(), &len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), ip, len);
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(field_hex) {
		const ln_instr_t *inst = INST();
		int64_t value;
		size_t len;
		if (UNLIKELY(parse_hex(ip, REMAINING(), &value, &len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		/* Native JSON integer (decimal) with format="number", the matched hex
		 * literal as a string otherwise. */
		if (inst->flags & LN_INSTR_F_NUMERIC)
			vm_add_int_field(vm, turbo_iname(vm, inst, inst->data.str), value);
		else
			vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), ip, len);
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(field_quoted) {
		const ln_instr_t *inst = INST();
		size_t start, len, consumed;
		if (UNLIKELY(parse_op_quoted(ip, REMAINING(), &start, &len, &consumed) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), ip + start, len);
		ip += consumed;
		pc++;
		DISPATCH();
	}

	CASE(field_char_to) {
		const ln_instr_t *inst = INST();
		char delim;
		ln_span_t span;
		delim = (char)inst->data.char_to.delim;
		if (UNLIKELY(ln_simd_char_to(ip, REMAINING(), delim, &span) != LN_SIMD_OK)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.char_to.name), span.start, span.len);
		ip += span.consumed;
		pc++;
		DISPATCH();
	}

	CASE(field_str_to) {
		const ln_instr_t *inst = INST();
		const char *delim;
		size_t len;
		if (UNLIKELY(prog->strpool == NULL)) {
			WRITEBACK();
			BACKTRACK();
		}
		delim = prog->strpool + inst->data.str_to.delim_off;
		if (UNLIKELY(parse_string_to(ip, REMAINING(), delim, inst->aux, &len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str_to.name), ip, len);
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(field_rest) {
		const ln_instr_t *inst = INST();
		size_t rem;
		rem = REMAINING();
		vm->ip = ip;
		vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), ip, rem);
		ip += rem;
		pc++;
		DISPATCH();
	}

	CASE(field_json) {
		const ln_instr_t *inst = INST();
		size_t len;
		vm->ip = ip;
		/* SIMD stage-1 structural scan + scalar stage-2 flatten.
		 * On parse failure BACKTRACK restores result->n_fields from the
		 * fork snapshot, rolling back any partially-emitted leaves. */
		if (UNLIKELY(vm_json_flatten(vm, turbo_iname(vm, inst, inst->data.str), ip,
					     REMAINING(), &len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(field_mac) {
		const ln_instr_t *inst = INST();
		size_t len;
		if (UNLIKELY(parse_mac48(ip, REMAINING(), &len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), ip, len);
		ip += len;
		pc++;
		DISPATCH();
	}

	CASE(field_date) {
		const ln_instr_t *inst = INST();
		ln_span_t span;
		/*
		 * format="timestamp-unix"[-ms] turns the date into an epoch integer.
		 * ln_turbo_date2unix() runs the standard parser's own scan and
		 * conversion and returns a plain int64_t, so the grammar and the
		 * leap-year/offset/sub-second arithmetic exist once while the fast
		 * path still allocates nothing per message.
		 */
		if (UNLIKELY(inst->flags & LN_INSTR_F_DATE_FMT)) {
			size_t dparsed = 0;
			int64_t ts = 0;

			if (UNLIKELY(ln_turbo_date2unix((int)(inst->aux >> 8), ip,
						REMAINING(),
						(inst->aux & 0xff) == FMT_MODE_TIMESTAMP_UX_MS,
						&dparsed, &ts) != 0 || dparsed == 0)) {
				WRITEBACK();
				BACKTRACK();
			}
			vm->ip = ip;
			vm_add_int_field(vm, turbo_iname(vm, inst, inst->data.str), ts);
			ip += dparsed;
			pc++;
			DISPATCH();
		}
		if (UNLIKELY(ln_simd_timestamp(ip, REMAINING(), &span, NULL) != LN_SIMD_OK)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), span.start, span.len);
		ip += span.consumed;
		pc++;
		DISPATCH();
	}

	CASE(field_regex) {
		/* OP_FIELD_REGEX: handled via external regex engine.
		 * For now, fall through to backtrack if encountered. */
		WRITEBACK();
		BACKTRACK();
	}

	CASE(field_name_value) {
		/*
		 * Parse name=value pairs.  This handler modifies vm->ip directly
		 * because it uses vm_add_string_field() and vm_push/pop_field_ctx()
		 * which read vm state.
		 */
		const ln_instr_t *inst = INST();
		const char *ctx_name;
		const char *p;
		const char *end;
		char sep;
		char ass;
		char ass_char;
		int ignore_ws;
		int n_pairs;

		vm->ip = ip;
		vm->pc = pc;

		ctx_name = turbo_iname(vm, inst, inst->data.char_to.name);
		sep = (char)inst->data.char_to.delim;
		ass = (char)inst->data.char_to.ass;
		ass_char = ass ? ass : '=';
		ignore_ws = inst->data.char_to.ignore_ws;

		if (ctx_name[0]) {
			vm_push_field_ctx(vm, ctx_name, false);
		}

		p = ip;
		end = input_end;
		n_pairs = 0;

		while (p < end) {
			const char *name_start;
			size_t name_end_off; /* raw distance to assignator */
			size_t name_len;     /* stored name length (may be trimmed) */
			const char *val_start;
			size_t val_len;
			char *arena_name;

			/* ignore_whitespaces: skip leading whitespace before the name
			 * (SIMD-accelerated; same whitespace class as isspace). */
			if (ignore_ws) {
				p += ln_simd_skip_space(p, (size_t)(end - p));
				if (p >= end) break;
			}
			name_start = p;

			name_end_off = ln_simd_find_char(p, (size_t)(end - p), ass_char);
			if (name_end_off >= (size_t)(end - p)) break;

			name_len = name_end_off;
			if (name_len == 0) break;

			if (ass == 0) {
				/* Default mode: contiguous valid-char name followed by '='
				 * (matches the standard parser; no trailing-ws trim). */
				int valid = 1;
				size_t k;
				for (k = 0; k < name_len; k++) {
					unsigned char c = (unsigned char)name_start[k];
					if (!(isalnum(c) || c == '.' || c == '_' || c == '-')) {
						valid = 0;
						break;
					}
				}
				if (!valid) break;
			} else if (ignore_ws) {
				/* Custom assignator: trim the name's trailing whitespace. */
				while (name_len > 0
					&& isspace((unsigned char)name_start[name_len - 1]))
					name_len--;
				if (name_len == 0) break;
			}

			p += name_end_off + 1; /* skip name span + assignator */

			/* ignore_whitespaces: skip whitespace between assignator and
			 * value (SIMD-accelerated). */
			if (ignore_ws) {
				p += ln_simd_skip_space(p, (size_t)(end - p));
			}

			if (p < end && (*p == '"' || *p == '\'')) {
				char quote = *p;
				int backslashes = 0;
				p++;
				val_start = p;
				while (p < end) {
					if (*p == quote && (backslashes % 2 == 0)) break;
					if (*p == '\\') backslashes++;
					else backslashes = 0;
					p++;
				}
				val_len = (size_t)(p - val_start);
				if (p < end && *p == quote) {
					p++;
				} else {
					break;
				}
			} else {
				val_start = p;
				if (sep) {
					size_t off = ln_simd_find_char(p, (size_t)(end - p), sep);
					val_len = (off < (size_t)(end - p)) ? off : (size_t)(end - p);
				} else {
					val_len = 0;
					while (p + val_len < end && !isspace((unsigned char)p[val_len]))
						val_len++;
				}
				p += val_len;
				/* ignore_whitespaces: trim trailing ws from unquoted value */
				if (ignore_ws) {
					while (val_len > 0
						&& isspace((unsigned char)val_start[val_len - 1]))
						val_len--;
				}
			}

			/* Arena-allocate name so it outlives this stack frame */
			arena_name = ln_arena_strndup(vm->arena, name_start, name_len);
			if (!arena_name) break;

			vm_add_string_field(vm, arena_name, val_start, val_len);
			n_pairs++;

			/* Advance to the next pair (see scalar handler for rationale). */
			if (ignore_ws && sep) {
				p += ln_simd_skip_space(p, (size_t)(end - p));
			}
			if (p < end
				&& !(sep ? (*p == sep) : isspace((unsigned char)*p)))
				break;
			if (sep) {
				while (p < end && *p == sep) p++;
			} else {
				while (p < end && isspace((unsigned char)*p)) p++;
			}
		}

		if (ctx_name[0]) {
			vm_pop_field_ctx(vm);
		}

		if (UNLIKELY(n_pairs == 0)) {
			WRITEBACK();
			BACKTRACK();
		}

		ip = p;
		pc++;
		DISPATCH();
	}

	/*=================================================================
	 * Skipping Opcodes
	 *=================================================================*/

	CASE(skip_space) {
		const ln_instr_t *inst = INST();
		size_t ws = ln_simd_skip_space(ip, REMAINING());
		if (UNLIKELY(ws == 0)) {   /* whitespace field requires one or more */
			WRITEBACK();
			BACKTRACK();
		}
		if (inst->flags & LN_INSTR_F_STORE) {
			vm->ip = ip;
			vm_add_string_field(vm, turbo_iname(vm, inst, inst->data.str), ip, ws);
		}
		ip += ws;
		pc++;
		DISPATCH();
	}

	CASE(skip_space1) {
		size_t rem;
		size_t skipped;
		rem = REMAINING();
		if (UNLIKELY(rem == 0 || !isspace((unsigned char)ip[0]))) {
			WRITEBACK();
			BACKTRACK();
		}
		skipped = 1;
		while (skipped < rem && isspace((unsigned char)ip[skipped]))
			skipped++;
		ip += skipped;
		pc++;
		DISPATCH();
	}

	CASE(skip_n) {
		const ln_instr_t *inst = INST();
		uint16_t n;
		n = inst->aux;
		if (UNLIKELY(REMAINING() < n)) {
			WRITEBACK();
			BACKTRACK();
		}
		ip += n;
		pc++;
		DISPATCH();
	}

	CASE(skip_to) {
		const ln_instr_t *inst = INST();
		char c;
		size_t rem;
		size_t pos;
		c = inst->data.str[0];
		rem = REMAINING();
		pos = ln_simd_find_char(ip, rem, c);
		if (UNLIKELY(pos >= rem)) {
			WRITEBACK();
			BACKTRACK();
		}
		ip += pos;
		pc++;
		DISPATCH();
	}

	CASE(skip_past) {
		const ln_instr_t *inst = INST();
		char c;
		size_t rem;
		size_t pos;
		c = inst->data.str[0];
		rem = REMAINING();
		pos = ln_simd_find_char(ip, rem, c);
		if (UNLIKELY(pos >= rem)) {
			WRITEBACK();
			BACKTRACK();
		}
		ip += pos + 1;
		pc++;
		DISPATCH();
	}

	CASE(skip_line) {
		size_t rem;
		size_t pos;
		rem = REMAINING();
		pos = ln_simd_find_char(ip, rem, '\n');
		ip += (pos < rem) ? pos + 1 : rem;
		pc++;
		DISPATCH();
	}

	/*=================================================================
	 * Tag / Rule ID Opcodes
	 *=================================================================*/

	CASE(tag) {
		const ln_instr_t *inst = INST();
		if (vm->result && inst->data.str[0]) {
			ln_fast_add_tag(vm->result, inst->data.str);
		}
		pc++;
		DISPATCH();
	}

	CASE(rule_id) {
		const ln_instr_t *inst = INST();
		if (vm->result && inst->data.str[0]) {
			ln_fast_set_rule_id(vm->result, inst->data.str);
		}
		pc++;
		DISPATCH();
	}

	CASE(static_field) {
		const ln_instr_t *inst = INST();
		if (vm->result) {
			const char *key = NULL;
			const char *val = NULL;
			uint32_t vlen = 0;

			if (inst->flags & LN_INSTR_F_KV_POOL) {
				if (prog->strpool) {
					key = prog->strpool + inst->data.kv_pool.key_off;
					val = prog->strpool + inst->data.kv_pool.val_off;
					vlen = inst->data.kv_pool.val_len;
				}
			} else if (inst->data.kv.key[0]) {
				key = inst->data.kv.key;
				val = inst->data.kv.val;
				vlen = (uint32_t)strlen(inst->data.kv.val);
			}
			/* Resolve like any other field: a static field inside a context
			 * takes its prefix, ".." takes the enclosing name, and "-" stores
			 * nothing. Adding it raw skipped all three. */
			if (key != NULL) {
				uint16_t nlen;
				const char *full = vm_build_field_name(vm, key, &nlen);

				if (full != NULL && full[0] != '\0') {
					if (inst->flags & LN_INSTR_F_ANNOT)
						ln_fast_set_string_static(vm->result, full,
									  nlen, val, vlen);
					else
						ln_fast_add_string_static(vm->result, full,
									  nlen, val, vlen);
				}
			}
		}
		pc++;
		DISPATCH();
	}

	/*=================================================================
	 * Assertion Opcodes
	 *=================================================================*/

	CASE(assert_char) {
		if (UNLIKELY(REMAINING() < 1 || *ip != INST()->data.str[0])) {
			WRITEBACK();
			BACKTRACK();
		}
		pc++;
		DISPATCH();
	}

	CASE(assert_end) {
		if (UNLIKELY(REMAINING() > 0)) {
			WRITEBACK();
			BACKTRACK();
		}
		pc++;
		DISPATCH();
	}

	CASE(assert_start) {
		if (UNLIKELY(ip != vm->input)) {
			WRITEBACK();
			BACKTRACK();
		}
		pc++;
		DISPATCH();
	}

	/*=================================================================
	 * Special Opcodes
	 *=================================================================*/

	CASE(syslog_pri) {
		size_t rem;
		const char *p;
		uint32_t pri;
		int digits;
		rem = REMAINING();
		if (UNLIKELY(rem < 3 || ip[0] != '<')) {
			WRITEBACK();
			BACKTRACK();
		}
		p = ip + 1;
		pri = 0;
		digits = 0;
		while (p < input_end && *p >= '0' && *p <= '9' && digits < 3) {
			pri = pri * 10 + (*p - '0');
			p++;
			digits++;
		}
		if (UNLIKELY(p >= input_end || *p != '>' || digits == 0 || pri > 191)) {
			WRITEBACK();
			BACKTRACK();
		}
		p++;
		vm->ip = ip;
		vm_add_int_field(vm, "facility", pri / 8);
		vm_add_int_field(vm, "severity", pri % 8);
		ip = p;
		pc++;
		DISPATCH();
	}

	CASE(syslog_ts) {
		/* OP_SYSLOG_TS: parse syslog timestamp; delegates to SIMD */
		const ln_instr_t *inst = INST();
		ln_span_t span;
		const char *name;
		if (UNLIKELY(ln_simd_timestamp(ip, REMAINING(), &span, NULL) != LN_SIMD_OK)) {
			WRITEBACK();
			BACKTRACK();
		}
		vm->ip = ip;
		/* Store as "timestamp" field if no name specified */
		name = inst->data.str[0] ? inst->data.str : "timestamp";
		vm_add_string_field(vm, name, span.start, span.len);
		ip += span.consumed;
		pc++;
		DISPATCH();
	}

	CASE(cef_hdr) {
		/*
		 * Parse CEF header: CEF:0|vendor|product|version|sigID|name|severity|ext
		 * 6 pipe-delimited fields with escape handling, then key=value extensions.
		 */
		const ln_instr_t *inst = INST();
		const char *name;
		const char *p;
		int hdr_idx;

		static const char *cef_hdr_names[6] = {
			"DeviceVendor", "DeviceProduct", "DeviceVersion",
			"SignatureID", "Name", "Severity"
		};

		vm->ip = ip;
		vm->pc = pc;

		/* Must start with "CEF:0|" */
		if (UNLIKELY(REMAINING() < 6 || memcmp(ip, "CEF:0|", 6) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}

		name = inst->data.str;
		p = ip + 6;

		if (name[0]) {
			vm_push_field_ctx(vm, name, false);
		}

		/* Parse 6 pipe-delimited header fields */
		for (hdr_idx = 0; hdr_idx < 6; hdr_idx++) {
			const char *field_start = p;
			const char *field_val;
			size_t field_len;

			while (p < input_end) {
				if (*p == '\\' && p + 1 < input_end) {
					p += 2;
					continue;
				}
				if (*p == '|') break;
				p++;
			}

			field_len = (size_t)(p - field_start);
			if (cef_unescape(vm, field_start, field_len, 0,
							 &field_val, &field_len) != 0) {
				if (name[0]) vm_pop_field_ctx(vm);
				WRITEBACK();
				BACKTRACK();
			}
			vm_add_string_field(vm, cef_hdr_names[hdr_idx],
								field_val, field_len);

			if (hdr_idx < 5) {
				if (UNLIKELY(p >= input_end || *p != '|')) {
					if (name[0]) vm_pop_field_ctx(vm);
					WRITEBACK();
					BACKTRACK();
				}
				p++;
			} else {
				if (p < input_end && *p == '|') p++;
			}
		}

		/* Parse extensions: key=value pairs */
		if (p < input_end) {
			/* Extensions nest under the field, so compose "<field>.Extensions"
			 * (the context stack tracks only the top level). */
			const char *ext_ctx = "Extensions";
			int n_ext = 0;

			/* Push the plain segment: the context stack composes the
			 * full path itself. */
			vm_push_field_ctx(vm, ext_ctx, false);

			while (p < input_end) {
				const char *key_start;
				size_t eq_off;
				size_t key_len;
				const char *val_start;
				size_t val_len;
				const char *next_eq;
				const char *key_eq;
				const char *val_span;
				const char *scan;
				char *arena_key;

				while (p < input_end && *p == ' ') p++;
				if (p >= input_end) break;

				key_start = p;
				key_eq = cef_find_unescaped(p, p, input_end, '=');
				if (key_eq == NULL) break;
				eq_off = (size_t)(key_eq - p);

				key_len = eq_off;
				if (key_len == 0) break;

				p += key_len + 1;

				val_start = p;
				next_eq = cef_find_unescaped(val_start, val_start, input_end, '=');

				if (next_eq && next_eq > val_start) {
					const char *kstart = next_eq;
					while (kstart > val_start && *(kstart - 1) != ' ')
						kstart--;
					if (kstart > val_start) {
						val_len = (size_t)(kstart - val_start);
						while (val_len > 0 &&
							   val_start[val_len - 1] == ' ')
							val_len--;
						p = kstart;
					} else {
						val_len = (size_t)(input_end - val_start);
						p = input_end;
					}
				} else {
					val_len = (size_t)(input_end - val_start);
					p = input_end;
				}

				arena_key = ln_arena_strndup(vm->arena,
							key_start, key_len);
				if (!arena_key) break;
				if (cef_unescape(vm, val_start, val_len, 1,
								 &val_span, &val_len) != 0) {
					vm_pop_field_ctx(vm);
					if (name[0]) vm_pop_field_ctx(vm);
					WRITEBACK();
					BACKTRACK();
				}
				vm_add_string_field(vm, arena_key,
									val_span, val_len);
				n_ext++;
			}

			vm_pop_field_ctx(vm);
			(void)n_ext;
		}

		if (name[0]) {
			vm_pop_field_ctx(vm);
		}

		ip = p;
		pc++;
		DISPATCH();
	}

	CASE(v2_iptables) {
		/*
		 * Parse iptables-format name=value pairs.
		 * Minimum 2 pairs required.  A flag (a name without '=') gets a
		 * null value, as the standard parser gives it.
		 */
		const ln_instr_t *inst = INST();
		const char *ctx_name;
		const char *p;
		int n_pairs;

		vm->ip = ip;
		vm->pc = pc;

		ctx_name = turbo_iname(vm, inst, inst->data.str);

		if (ctx_name[0]) {
			vm_push_field_ctx(vm, ctx_name, false);
		}

		p = ip;
		n_pairs = 0;

		while (p < input_end) {
			const char *name_start;
			size_t eq_off;
			size_t sp_off;
			size_t name_len;
			const char *val_start;
			size_t val_len;
			char *arena_name;

			/* Skip leading spaces */
			while (p < input_end && *p == ' ') p++;
			if (p >= input_end) break;

			name_start = p;

			/* Scan for '=' */
			eq_off = ln_simd_find_char(p, (size_t)(input_end - p), '=');

			/* Check if space comes before '=' (flag) */
			sp_off = ln_simd_find_char(p, (size_t)(input_end - p), ' ');
			if (sp_off < eq_off) {
				/* Flag: name without value */
				name_len = sp_off;
				if (name_len == 0) break;
				arena_name = ln_arena_strndup(vm->arena,
							name_start, name_len);
				if (!arena_name) break;
				vm_add_null_field(vm, arena_name);
				n_pairs++;
				p += sp_off;
				continue;
			}

			if (eq_off >= (size_t)(input_end - p)) {
				/* No '=': rest is a flag */
				name_len = (size_t)(input_end - p);
				if (name_len == 0) break;
				arena_name = ln_arena_strndup(vm->arena,
							name_start, name_len);
				if (!arena_name) break;
				vm_add_null_field(vm, arena_name);
				n_pairs++;
				p = input_end;
				break;
			}

			name_len = eq_off;
			if (name_len == 0) break;

			/* Validate name: alphanumeric + underscore */
			{
				int valid = 1;
				size_t k;
				for (k = 0; k < name_len; k++) {
					unsigned char c = (unsigned char)p[k];
					if (!(isalnum(c) || c == '_')) {
						valid = 0;
						break;
					}
				}
				if (!valid) break;
			}

			p += name_len + 1; /* skip name + '=' */

			/* Value: everything until space or end */
			val_start = p;
			{
				size_t off;
				off = ln_simd_find_char(p, (size_t)(input_end - p), ' ');
				val_len = (off < (size_t)(input_end - p))
						  ? off : (size_t)(input_end - p);
			}
			p += val_len;

			arena_name = ln_arena_strndup(vm->arena,
						name_start, name_len);
			if (!arena_name) break;
			vm_add_string_field(vm, arena_name, val_start, val_len);
			n_pairs++;
		}

		if (ctx_name[0]) {
			vm_pop_field_ctx(vm);
		}

		if (UNLIKELY(n_pairs < 2)) {
			WRITEBACK();
			BACKTRACK();
		}

		ip = p;
		pc++;
		DISPATCH();
	}

	CASE(cee_syslog) {
		/*
		 * Parse CEE-syslog format: "@cee:" prefix + JSON object.
		 * The JSON body is stored as the field value.
		 */
		const ln_instr_t *inst = INST();
		const char *fname;
		const char *p;
		size_t rem;
		size_t json_len;


		fname = turbo_iname(vm, inst, inst->data.str);
		rem = REMAINING();

		/* Must start with "@cee:" (5 chars) + at least '{' */
		if (UNLIKELY(rem < 6 || memcmp(ip, "@cee:", 5) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}

		p = ip + 5;
		rem -= 5;

		/* Skip optional whitespace after ':' */
		while (rem > 0 && (*p == ' ' || *p == '\t')) {
			p++;
			rem--;
		}

		/* Must have '{' */
		if (UNLIKELY(rem == 0 || *p != '{')) {
			WRITEBACK();
			BACKTRACK();
		}

		/* SIMD-flatten the CEE JSON body into dotted flat keys. */
		vm->ip = ip;
		if (UNLIKELY(vm_json_flatten(vm, fname, p, rem, &json_len) != 0)) {
			WRITEBACK();
			BACKTRACK();
		}

		/* JSON must consume rest of input (trailing whitespace allowed). */
		json_len += ln_simd_skip_space(p + json_len, rem - json_len);
		if (UNLIKELY(json_len != rem)) {
			WRITEBACK();
			BACKTRACK();
		}

		ip = p + json_len;
		pc++;
		DISPATCH();
	}

	CASE(checkpoint_lea) {
		/*
		 * Parse Checkpoint LEA format: name: value; name: value; ...
		 * Minimum 1 field required.
		 */
		const ln_instr_t *inst = INST();
		const char *ctx_name;
		char term;
		const char *p;
		int n_pairs;

		vm->ip = ip;
		vm->pc = pc;

		ctx_name = turbo_iname(vm, inst, inst->data.char_to.name);
		term = (char)inst->data.char_to.delim;

		if (ctx_name[0]) {
			vm_push_field_ctx(vm, ctx_name, false);
		}

		p = ip;
		n_pairs = 0;

		while (p < input_end) {
			const char *name_start;
			size_t colon_off;
			size_t name_len;
			const char *val_start;
			size_t val_len;
			size_t semi_off;
			char *arena_name;

			/* Skip leading spaces */
			while (p < input_end && *p == ' ') p++;
			if (p >= input_end) break;

			/* Check for early terminator */
			if (term && *p == term) break;

			name_start = p;

			/* Scan for ':' */
			colon_off = ln_simd_find_char(p,
						(size_t)(input_end - p), ':');
			if (colon_off >= (size_t)(input_end - p)) break;

			name_len = colon_off;
			if (name_len == 0) break;

			p += name_len + 1; /* skip name + ':' */

			/* Skip spaces after ':' */
			while (p < input_end && *p == ' ') p++;

			/* Scan for ';' */
			val_start = p;
			semi_off = ln_simd_find_char(p,
						(size_t)(input_end - p), ';');
			if (semi_off >= (size_t)(input_end - p)) {
				val_len = (size_t)(input_end - p);
				p = input_end;
			} else {
				val_len = semi_off;
				p += val_len + 1; /* skip value + ';' */
			}

			arena_name = ln_arena_strndup(vm->arena,
						name_start, name_len);
			if (!arena_name) break;
			vm_add_string_field(vm, arena_name,
								val_start, val_len);
			n_pairs++;
		}

		if (ctx_name[0]) {
			vm_pop_field_ctx(vm);
		}

		if (UNLIKELY(n_pairs < 1)) {
			WRITEBACK();
			BACKTRACK();
		}

		ip = p;
		pc++;
		DISPATCH();
	}

	/*=================================================================
	 * Debug / NOP Opcodes
	 *=================================================================*/

	CASE(nop) {
		pc++;
		DISPATCH();
	}

	CASE(debug) {
		WRITEBACK();
		fprintf(stderr, "[DEBUG] pc=%u ip=%zu ctx_sp=%u\n",
				pc, (size_t)(ip - vm->input), vm->field_ctx_sp);
		pc++;
		DISPATCH();
	}

	CASE(invalid) {
		snprintf(vm->error_buf, sizeof(vm->error_buf), "unknown opcode 0x%02x at pc=%u",
				 prog->code[pc].op, pc);
		vm->error = vm->error_buf;
		WRITEBACK();
		return LN_VM_ERROR;
	}

	/*=================================================================
	 * Backtrack Handler
	 *=================================================================*/

backtrack:
	vm->pc = pc;
	vm->ip = ip;
	if (UNLIKELY(vm->instr_count > MAX_INSTRUCTIONS)) {
		vm->error = "instruction limit exceeded";
		return LN_VM_LIMIT;
	}
	if (!vm_pop_fork(vm)) {
		return LN_VM_NOMATCH;
	}
	/* Restore local variables from vm state (pop_fork wrote them) */
	pc = vm->pc;
	ip = vm->ip;
	DISPATCH();

	#undef REMAINING
	#undef INST
	#undef WRITEBACK
	#undef VALIDATE_TARGET
}

#else /* !LN_VM_COMPUTED_GOTO: switch-based fallback */

int
ln_vm_continue(ln_vm_t *vm)
{
	if (!vm || !vm->prog) {
		return LN_VM_ERROR;
	}

	const uint64_t MAX_INSTRUCTIONS = 100000000;

	for (;;) {
		if (UNLIKELY(vm->instr_count > MAX_INSTRUCTIONS)) {
			vm->error = "instruction limit exceeded";
			return LN_VM_LIMIT;
		}

		int rc = vm_exec_instr(vm);

		if (rc == 0) {
			return LN_VM_OK;
		}

		if (rc < 0) {
			if (!vm_pop_fork(vm)) {
				return LN_VM_NOMATCH;
			}
		}
	}
}

#endif /* LN_VM_COMPUTED_GOTO */

/*============================================================================
 * Debug
 *============================================================================*/

void
ln_vm_dump(const ln_vm_t *vm, FILE *fp)
{
	if (!vm || !fp) return;

	fprintf(fp, "=== VM State ===\n");
	fprintf(fp, "Program:    %s\n", vm->prog ?
			(vm->prog->name ? vm->prog->name : "(unnamed)") : "(none)");
	fprintf(fp, "PC:         %u\n", vm->pc);
	fprintf(fp, "IP offset:  %zu\n", vm->ip ? (size_t)(vm->ip - vm->input) : 0);
	fprintf(fp, "Remaining:  %zu\n", ln_vm_remaining(vm));
	fprintf(fp, "Fork SP:    %u\n", vm->fork_sp);
	fprintf(fp, "Call SP:    %u\n", vm->call_sp);
	fprintf(fp, "Field Ctx:  %u\n", vm->field_ctx_sp);
	fprintf(fp, "Instr cnt:  %lu\n", (unsigned long)vm->instr_count);
	fprintf(fp, "Backtracks: %lu\n", (unsigned long)vm->backtrack_count);
	fprintf(fp, "Matched:    %s\n", vm->matched_rule ? vm->matched_rule : "(none)");
	fprintf(fp, "Error:      %s\n", vm->error ? vm->error : "(none)");

	if (vm->field_ctx_sp > 0) {
		fprintf(fp, "Field context stack:\n");
		for (uint32_t i = 0; i < vm->field_ctx_sp; i++) {
			fprintf(fp, "  [%u] \"%s\" (nested=%d)\n",
					i, vm->field_ctx[i].name ? vm->field_ctx[i].name : "(null)",
					vm->field_ctx[i].is_nested);
		}
	}

	if (vm->ip && vm->input_end > vm->ip) {
		size_t show = (size_t)(vm->input_end - vm->ip);
		if (show > 40) show = 40;
		fprintf(fp, "At input:   \"%.*s%s\"\n", (int)show, vm->ip,
				show < (size_t)(vm->input_end - vm->ip) ? "..." : "");
	}
	fprintf(fp, "\n");
}

void
ln_vm_set_trace(ln_vm_t *vm, bool enable)
{
	(void)vm;
	(void)enable;
#ifdef LN_VM_TRACE
	g_trace_enabled = enable;
#endif
}
