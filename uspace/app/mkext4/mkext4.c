/*
 * Copyright (c) 2018 Jiri Svoboda
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup fs
 * @{
 */

/**
 * @file
 * @brief Tool for creating new Ext4 file systems.
 */

#include <errno.h>
#include <ext4/filesystem.h>
#include <loc.h>
#include <stdio.h>
#include <str.h>

#define NAME	"mkext4"

static void syntax_print(void);

int main(int argc, char **argv)
{
	errno_t rc;
	char *dev_path;
	service_id_t service_id;
	char *endptr;
	aoff64_t nblocks;
	char *label;

	if (argc < 2) {
		printf(NAME ": Error, argument missing.\n");
		syntax_print();
		return 1;
	}

	--argc;
	++argv;

	while (*argv[0] == '-') {
		if (str_cmp(*argv, "--size") == 0) {
			--argc;
			++argv;
			if (*argv == NULL) {
				printf(NAME ": Error, argument missing.\n");
				syntax_print();
				return 1;
			}

			nblocks = strtol(*argv, &endptr, 10);
			if (*endptr != '\0') {
				printf(NAME ": Error, invalid argument.\n");
				syntax_print();
				return 1;
			}

			--argc;
			++argv;
		}

		if (str_cmp(*argv, "--label") == 0) {
			--argc;
			++argv;
			if (*argv == NULL) {
				printf(NAME ": Error, argument missing.\n");
				syntax_print();
				return 1;
			}

			label = *argv;

			--argc;
			++argv;
		}

		if (str_cmp(*argv, "-") == 0) {
			--argc;
			++argv;
			break;
		}
	}

	if (argc != 1) {
		printf(NAME ": Error, unexpected argument.\n");
		syntax_print();
		return 1;
	}

	dev_path = *argv;
	printf("Device: %s\n", dev_path);

	rc = loc_service_get_id(dev_path, &service_id, 0);
	if (rc != EOK) {
		printf(NAME ": Error resolving device `%s'.\n", dev_path);
		return 2;
	}

	(void) label;
	(void) nblocks;

	rc = ext4_filesystem_create(service_id);
	if (rc != EOK) {
		printf(NAME ": Error initializing file system.\n");
		return 3;
	}

	printf("Success.\n");

	return 0;
}

static void syntax_print(void)
{
	printf("syntax: mkext4 [<options>...] <device_name>\n");
	printf("options:\n"
	    "\t--size <sectors> Filesystem size, overrides device size\n"
	    "\t--label <label>  Volume label\n");
}

/**
 * @}
 */
