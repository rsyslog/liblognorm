/* clustering log records
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
#include <string.h>
#include "syntaxes.h"

#define MAXLINE 32*1024

void
processLine(const char *const __restrict__ buf,
	const size_t buflen)
{
	static int lnCnt = 1;
	size_t nproc;
	char *tocopy;
	size_t tocopylen;
	size_t iout;
	char bufout[MAXLINE];

	printf("line %d: %s\n", lnCnt, buf);
	iout = 0;
	for(size_t i = 0 ; i < buflen ; ) {
//printf("i %zd, iout %zd\n", i, iout);
		if(syntax_ipv4(buf+i, buflen-i, NULL, &nproc)) {
			tocopy = "%ipv4%";
		} else if(syntax_posint(buf+i, buflen-i, NULL, &nproc)) {
			tocopy = "%posint%";
		} else {
			tocopy = NULL;
			nproc = 1;
		}

		/* copy to output buffer */
		if(tocopy == NULL) {
			bufout[iout++] = buf[i];
		} else {
			tocopylen = strlen(tocopy); // do this in lower lever
			memcpy(bufout+iout, tocopy, tocopylen);
			iout += tocopylen;
		}
		i += nproc;
	}
	bufout[iout] = '\0';
	printf("outline %d: %s\n", lnCnt, bufout);
	++lnCnt;
}

int
processFile(FILE *fp)
{
	char lnbuf[MAXLINE];

	while(!feof(fp)) {
		size_t i;
		for(i = 0 ; i < sizeof(lnbuf)-1 ; ++i) {
			const int c = fgetc(fp);
			if(c == EOF || c == '\n')
				break;
			lnbuf[i] = c;
		}
		lnbuf[i] = '\0';
		if(i > 0) {
			processLine(lnbuf, i);
		}
	}

	return 0;
}


int
main(int argc, char *argv[])
{
	int r;
	r = processFile(stdin);
	return r;
}
