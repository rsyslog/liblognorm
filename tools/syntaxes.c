/* Syntax "detectors"
 *
 * Copyright 2015 Rainer Gerhards
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *       http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "config.h"
#include <stdio.h>
#include <stdint.h>

#include "syntaxes.h"

/* returns -1 if no integer found, else integer */
static int64_t
getPosInt(const char *const __restrict__ buf,
	const size_t buflen,
	size_t *const __restrict__ nprocessed)
{
	int64_t val = 0;
	size_t i;

	for(i = 0 ; i < buflen ; ++i) {
		if('0' <= buf[i] && buf[i] <= '9')
			val = val*10 + buf[i]-'0';
		else
			break;
	}
	*nprocessed = i;
	if(i == 0)
		val = -1;
	return val;
}

/* 1 - is IPv4, 0 not */
int
syntax_ipv4(const char *const __restrict__ buf,
	const size_t buflen,
	const char *extracted,
	size_t *const __restrict__ nprocessed)
{
	int64_t val;
	size_t nproc;
	size_t i;
	int r = 0;

	val = getPosInt(buf, buflen, &i);
	if(val < 1 || val > 255)
		goto done;

	if(buf[i] != '.') goto done;
	i++;
	val = getPosInt(buf+i, buflen-i, &nproc);
	if(val < 1 || val > 255)
		goto done;
	i += nproc;

	if(buf[i] != '.') goto done;
	i++;
	val = getPosInt(buf+i, buflen-i, &nproc);
	if(val < 1 || val > 255)
		goto done;
	i += nproc;

	if(buf[i] != '.') goto done;
	i++;
	val = getPosInt(buf+i, buflen-i, &nproc);
	if(val < 1 || val > 255)
		goto done;
	i += nproc;

	//printf("IP Addr[%zd]: '%s'\n", i, buf);
	*nprocessed = i;
	r = 1;

done:
	return r;
}

/* 1 - is positive integer, 0 not */
int
syntax_posint(const char *const __restrict__ buf,
	const size_t buflen,
	const char *extracted,
	size_t *const __restrict__ nprocessed)
{
	int64_t val;
	size_t nproc;
	size_t i;
	int r = 0;

	val = getPosInt(buf, buflen, &i);
	if(val == -1)
		goto done;
	*nprocessed = i;
	r = 1;

done:
	return r;
}
