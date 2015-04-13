/* a small tool to squash multiline message - probably to be removed
 * later and integrated into the "mainstream" tools.
 * Copyright (C) 2015 by Rainer Gerhards
 * Released under ASL 2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

char *
getmsg(regex_t *const preg, char *const buf, size_t len)
{
	static size_t lenln = 0;
	static char lnbuf[1024*64];
	size_t iDst = 0;
	int nlines = 0;

	if(lenln) { /* have previous segment? */
		memcpy(buf+iDst, lnbuf, lenln);
		iDst += lenln;
		++nlines;
	}

	while(fgets(lnbuf, sizeof(lnbuf), stdin)) {
		lenln = strlen(lnbuf);
		if(lnbuf[lenln-1] == '\n') {
			lnbuf[lenln-1] = '\0';
			lenln--;
		}
		const int is_match =
			!regexec(preg, lnbuf, 0, NULL, 0);
		if(is_match) {
			break; /* previous message complete */
		} else {
			if(iDst != 0) {
				buf[iDst++] = '\\';
				buf[iDst++] = 'n';
			}
			memcpy(buf+iDst, lnbuf, lenln);
			iDst += lenln;
			++nlines;
		}
	}
	if(nlines == 0 && lenln > 0) { /* handle single lines */
		memcpy(buf+iDst, lnbuf, lenln);
		iDst += lenln;
		lenln = 0;
	}
	buf[iDst] = '\0';
}

int
main(int argc, char *argv[])
{
	if(argc != 2) {
		fprintf(stderr, "usage: squashml regex\n");
		exit(1);
	}

	regex_t preg;
	if(regcomp(&preg, argv[1], REG_EXTENDED)) {
		perror("regcomp");
		exit(1);
	}

	char msg[1024*256];
	while(!feof(stdin)) {
		getmsg(&preg, msg, sizeof(msg));
		printf("%s\n", msg);
	}
}
