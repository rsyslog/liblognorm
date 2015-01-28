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
/* 1 - is IPv4, 0 not */
int
syntax_ipv4(const char *const __restrict__ buf,
	const size_t buflen,
	const char *extracted,
	size_t *const __restrict__ nprocessed);

int
syntax_posint(const char *const __restrict__ buf,
	const size_t buflen,
	const char *extracted,
	size_t *const __restrict__ nprocessed);
