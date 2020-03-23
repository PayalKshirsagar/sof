// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Bartosz Kokoszko	<bartoszx.kokoszko@linux.intel.com>
//	   Artur Kloniecki	<arturx.kloniecki@linux.intel.com>

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <termios.h>
#include "convert.h"

#define APP_NAME "sof-logger"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#endif

static const char *debugfs[] = {
	"dmac0", "dmac1", "ssp0", "ssp1",
	"ssp2", "iram", "dram", "shim",
	"mbox", "etrace",
	"hda", "pp", "dsp",
};

static void usage(void)
{
	fprintf(stdout, "Usage %s <option(s)> <file(s)>\n", APP_NAME);
	fprintf(stdout, "%s:\t \t\t\tDisplay mailbox contents\n", APP_NAME);
	fprintf(stdout, "%s:\t -i infile -o outfile\tDump infile contents "
		"to outfile\n", APP_NAME);
	fprintf(stdout, "%s:\t -l *.ldc_file\t\t*.ldc files generated "
		"by rimage\n", APP_NAME);
	fprintf(stdout, "%s:\t -p \t\t\tInput from stdin\n", APP_NAME);
	fprintf(stdout, "%s:\t -e \t\t\tEnable checking firmware version with "
		"default verification file\n", APP_NAME);
	fprintf(stdout, "%s:\t -v ver_file\t\tEnable checking firmware version "
		"with ver_file file\n", APP_NAME);
	fprintf(stdout, "%s:\t -c clock\t\tSet timestamp clock in MHz\n",
		APP_NAME);
	fprintf(stdout, "%s:\t -s state_name\t\tTake a snapshot of state\n",
		APP_NAME);
	fprintf(stdout, "%s:\t -t\t\t\tDisplay trace data\n", APP_NAME);
	fprintf(stdout, "%s:\t -u baud\t\tInput data from a UART\n", APP_NAME);
	fprintf(stdout, "%s:\t -r\t\t\tLess formatted output for "
		"chained log processors\n",
		APP_NAME);
	fprintf(stdout, "%s:\t -d\t\t\tDump ldc information\n", APP_NAME);
	exit(0);
}

static int snapshot(const char *name)
{
	const char *path = "/sys/kernel/debug/sof";
	uint32_t val, addr;
	char pinname[64], poutname[64], buffer[128];
	FILE *in_fd, *out_fd;
	int i, count;

	if (!name) {
		fprintf(stderr, "error: need snapshot name\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(debugfs); i++) {

		sprintf(pinname, "%s/%s", path, debugfs[i]);
		sprintf(poutname, "%s.%s.txt", name, debugfs[i]);

		/* open debugfs for reading */
		in_fd = fopen(pinname, "rb");
		if (!in_fd) {
			fprintf(stderr, "error: unable to open %s for reading %d\n",
				pinname, errno);
			continue;
		}

		/* open outfile for writing */
		out_fd = fopen(poutname, "wb");
		if (!out_fd) {
			fprintf(stderr, "error: unable to open %s for writing %d\n",
				poutname, errno);
			fclose(in_fd);
			continue;
		}

		fprintf(stdout, "processing %s...\n", pinname);
		addr = 0;

		while (1) {
			count = fread(&val, 1, 4, in_fd);
			if (count != 4)
				break;

			sprintf(buffer, "0x%6.6x: 0x%8.8x\n", addr, val);

			count = fwrite(buffer, 1, strlen(buffer), out_fd);

			addr += 4;
		}

		fclose(in_fd);
		fclose(out_fd);
	}

	return 0;
}

static int configure_uart(const char *file, unsigned int baud)
{
	struct termios tio = {};
	int ret, fd = open(file, O_RDWR | O_NOCTTY);
	if (fd < 0)
		return -errno;

	cfsetspeed(&tio, 115200);
	cfmakeraw(&tio);

	tio.c_iflag |= IGNBRK;

	tio.c_cflag |= CLOCAL | CREAD | HUPCL;

	tio.c_cc[VTIME] = 1;
	tio.c_cc[VMIN] = 1;

	ret = tcsetattr(fd, TCSANOW, &tio);
	return ret < 0 ? -errno : fd;
}

int main(int argc, char *argv[])
{
	struct convert_config config;
	unsigned int baud = 0;
	const char *snapshot_file = 0;
	int opt, ret = 0;

	config.trace = 0;
	config.clock = 19.2;
	config.in_file = NULL;
	config.out_file = NULL;
	config.out_fd = NULL;
	config.in_fd = NULL;
	config.ldc_file = NULL;
	config.ldc_fd = NULL;
	config.input_std = 0;
	/* checking fw version is disabled by default */
	config.version_file = NULL;
	config.version_fd = NULL;
	config.version_fw = 0;
	config.use_colors = 1;
	config.serial_fd = -EINVAL;
	config.raw_output = 0;
	config.dump_ldc = 0;

	while ((opt = getopt(argc, argv, "ho:i:l:ps:c:u:tev:rd")) != -1) {
		switch (opt) {
		case 'o':
			config.out_file = optarg;
			break;
		case 'i':
			config.in_file = optarg;
			break;
		case 't':
			config.trace = 1;
			break;
		case 'c':
			config.clock = atof(optarg);
			break;
		case 's':
			snapshot_file = optarg;
			break;
		case 'l':
			config.ldc_file = optarg;
			break;
		case 'p':
			config.input_std = 1;
			break;
		case 'e':
			/* enabling checking fw version with default verification 
			 * file
			 */
			config.version_fw = 1;
			config.version_file = "/sys/kernel/debug/sof/fw_version";
			break;
		case 'u':
			baud = atoi(optarg);
			break;
		case 'r':
			config.raw_output = 1;
			break;
		case 'v':
			/* enabling checking fw version with ver_file file */
			config.version_fw = 1;
			config.version_file = optarg;
			break;
		case 'd':
			config.dump_ldc = 1;
			break;
		case 'h':
		default: /* '?' */
			usage();
		}
	}

	if (snapshot_file)
		return baud ? EINVAL : -snapshot(snapshot_file);

	if (!config.ldc_file) {
		fprintf(stderr, "error: Missing ldc file\n");
		usage();
	}

	config.ldc_fd = fopen(config.ldc_file, "rb");
	if (!config.ldc_fd) {
		fprintf(stderr, "error: Unable to open ldc file %s\n",
			config.ldc_file);
		ret = errno;
		goto out;
	}

	if (config.version_fw) {
		config.version_fd = fopen(config.version_file, "rb");
		if (!config.version_fd) {
			fprintf(stderr, "error: Unable to open version file %s\n",
				config.version_file);
			ret = errno;
			goto out;
		}
	}

	if (config.out_file) {
		config.out_fd = fopen(config.out_file, "w");
		if (!config.out_fd) {
			fprintf(stderr, "error: Unable to open out file %s\n",
				config.out_file);
			ret = errno;
			goto out;
		}
	} else {
		config.out_fd = stdout;
	}

	/* trace requested ? */
	if (config.trace)
		config.in_file = "/sys/kernel/debug/sof/trace";

	/* default option with no infile is to dump errors/debug data */
	if (!config.in_file)
		config.in_file = "/sys/kernel/debug/sof/etrace";

	if (config.input_std) {
		config.in_fd = stdin;
	} else if (baud) {
		config.serial_fd = configure_uart(config.in_file, baud);
		if (config.serial_fd < 0) {
			ret = -config.serial_fd;
			goto out;
		}
	} else {
		config.in_fd = fopen(config.in_file, "rb");
		if (!config.in_fd) {
			fprintf(stderr, "error: Unable to open in file %s\n",
				config.in_file);
			ret = errno;
			goto out;
		}
	}
	if (isatty(fileno(config.out_fd)) != 1)
		config.use_colors = 0;

	ret = -convert(&config);

out:
	/* close files */
	if (config.out_fd)
		fclose(config.out_fd);

	if (config.in_fd)
		fclose(config.in_fd);

	if (config.ldc_fd)
		fclose(config.ldc_fd);

	if (config.version_fd)
		fclose(config.version_fd);

	return ret;
}
