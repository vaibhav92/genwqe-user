/*
 * Copyright 2015, International Business Machines
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Genwqe Capi Card Master Maintenence tool.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>

#include <libddcb.h>
#include <libcxl.h>
#include "afu_regs.h"

static const char *version = GIT_VERSION;
static int verbose = 0;
static FILE *fd_out;

#define VERBOSE0(fmt, ...) do {					\
                fprintf(fd_out, fmt, ## __VA_ARGS__);		\
        } while (0)

#define VERBOSE1(fmt, ...) do {					\
                if (verbose > 0)				\
                        fprintf(fd_out, fmt, ## __VA_ARGS__);	\
        } while (0)

#define VERBOSE2(fmt, ...) do {					\
                if (verbose > 1)				\
                        fprintf(fd_out, fmt, ## __VA_ARGS__);	\
        } while (0)

struct mdev_ctx {
	int			loop;		/* Loop Counter */
	int			card;		/* Card no (0,1,2,3 */
	struct cxl_afu_h	*afu_h;		/* The AFU handle */
	int			dt;		/* Delay time in sec (1 sec default) */
	int			count;		/* Number of loops to do, (-1) = forever */
	bool			deamon;		/* TRUE if forked */
	uint64_t		wed;		/* This is a dummy only for attach */
	bool			quiet;		/* False or true -q option */
	pid_t			pid;
	pid_t			my_sid;		/* for sid */
	int			mode;		/* See below */
	int			process_irqs;	/* Master IRQ Counter */
};

/* Mode Bits for Master Loop */
#define	CHECK_FIRS_MODE	0x0001	/* Mode 1 */
#define	CHECK_TIME_MODE	0x0002	/* Mode 2 */

static struct mdev_ctx	master_ctx;

#if 0
static int mmio_write(struct cxl_afu_h *afu_h, int ctx, uint32_t offset, uint64_t data)
{
	int rc = -1;
	uint32_t offs = (ctx * MMIO_CTX_OFFSET) + offset;

	VERBOSE2("[%s] Enter, Offset: 0x%x data: 0x%016llx\n",
		__func__, offs, (long long)data);
	rc = cxl_mmio_write64(afu_h, offs, data);
	VERBOSE2("[%s] Exit, rc = %d\n", __func__, rc);
	return rc;
}
#endif

static int mmio_read(struct cxl_afu_h *afu_h, int ctx, uint32_t offset, uint64_t *data)
{
	int rc = -1;
	uint32_t offs = (ctx * MMIO_CTX_OFFSET) + offset;

	VERBOSE2("[%s] Enter, CTX: %d Offset: 0x%x\n", __func__, ctx, offs);
	rc = cxl_mmio_read64(afu_h, offs, data);
	VERBOSE2("[%s] Exit, rc = %d data: 0x%016llx\n",
		__func__, rc, (long long)*data);
	return rc;
}

/*
 * Open AFU Master Device
 */
static int afu_m_open(struct mdev_ctx *mctx)
{
	int rc = 0;
	char    device[64];

	sprintf(device, "/dev/cxl/afu%d.0m", mctx->card);
	VERBOSE2("[%s] Enter, Open Device: %s\n", __func__, device);
	mctx->afu_h = cxl_afu_open_dev(device);
	if (mctx->afu_h) {
		rc = cxl_afu_attach(mctx->afu_h,
			(__u64)(unsigned long)(void *)mctx->wed);
		if (0 == rc) {
			rc = cxl_mmio_map(mctx->afu_h, CXL_MMIO_BIG_ENDIAN);
			if (0 == rc) {
				VERBOSE2("[%s] Exit\n", __func__);
				return 0;
			}
			cxl_afu_free(mctx->afu_h);
		}
	}
	mctx->afu_h = NULL;
	VERBOSE0("[%s] Exit, Card Open error rc: %d\n", __func__, rc);
	return -1;
}

static void afu_m_close(struct mdev_ctx *mctx)
{
	VERBOSE2("[%s] Enter\n", __func__);
	if (mctx->afu_h) {
		cxl_mmio_unmap(mctx->afu_h);
		cxl_afu_free(mctx->afu_h);
		mctx->afu_h = NULL;
	}
	VERBOSE2("[%s] Exit\n", __func__);
}

static int afu_check_stime(struct mdev_ctx *mctx)
{
	int	gsel, bsel = 0, ctx = 0;
	uint64_t gmask = 0, data = 0, stat_reg, err_reg, mstat_reg;

	mmio_read(mctx->afu_h, MMIO_MASTER_CTX_NUMBER,
		MMIO_AFU_STATUS_REG, &mstat_reg);
	for (gsel = 0; gsel < MMIO_CASV_REG_NUM; gsel++) {
		gmask = 0;
		mmio_read(mctx->afu_h, MMIO_MASTER_CTX_NUMBER,
			MMIO_CASV_REG + (gsel*8), &gmask);
		if (0 == gmask) continue;	/* No bit set, Skip */
		for (bsel = 0; bsel < 64; bsel++) {
			if (0 == (gmask & (1 << bsel))) continue;	/* Skip */
			ctx = (gsel * 64) + bsel + 1;	/* Active */

			mmio_read(mctx->afu_h, ctx, MMIO_DDCBQ_STATUS_REG, &stat_reg);
			if (0 == (stat_reg & 0xffffffff00000000ull)) {
				VERBOSE2("AFU[%d:%d] master skip\n",
					mctx->card, ctx);
				continue;	/* Skip Master */
			}
			mmio_read(mctx->afu_h, ctx, MMIO_DDCBQ_WT_REG, &data);
			data = data / 250;		/* makes time in usec */

			mmio_read(mctx->afu_h, ctx, MMIO_DDCBQ_DMAE_REG, &err_reg);

			VERBOSE0("AFU[%d:%d] Time: %lld usec Status: 0x%llx ",
				mctx->card, ctx,
				(long long)data,
				(long long) stat_reg);
			if (0 != err_reg)
				VERBOSE0("DMA Error: 0x%llx", (long long)err_reg);
			/* tainted if not 0 */
			if (0 != mstat_reg)
				VERBOSE0("MSTAT: 0x%llx", (long long)mstat_reg);
			VERBOSE0("\n");
			
		}
	}
	return mctx->dt;
}

static int afu_check_mfirs(struct mdev_ctx *mctx)
{
	int	i;
	uint64_t	data;
	uint32_t	offs;

	for (i = 0; i < MMIO_FIR_REGS_NUM; i++) {
		offs = MMIO_FIR_REGS_BASE + i * 8;
		mmio_read(mctx->afu_h, MMIO_MASTER_CTX_NUMBER, offs, &data);
		VERBOSE0("AFU[%d] FIR: %d : 0x%016llx\n",
			mctx->card, i, (long long)data);
	}
	return mctx->dt;
}

/* Return true if card Software Release is OK */
static bool check_app(struct mdev_ctx *mctx)
{
	int	rc;
	uint64_t data;

	/* Get MMIO_APP_VERSION_REG */
        rc = mmio_read(mctx->afu_h, MMIO_MASTER_CTX_NUMBER,
		MMIO_APP_VERSION_REG, &data);
	if (0 != rc)
		return false;

	/* Check Application Version for Version higher than 0403 */
	/* Register 8 does have following layout for the 64 bits */
	/* RRRRFFIINNNNNNNN */
	/* RRRR     == 16 bit Software Release (0404) */
	/* FF       ==  8 bit Software Fix Level on card (01) */
	/* II       ==  8 bit Software Interface ID (03) */
	/* NNNNNNNN == 32 Bit Function (475a4950) = (GZIP) */
	data = data >> 32;		/* RRRRFFII */
	if (0x03 == (data & 0xff)) {	/* Check II */
		data = data >> 16;	/* Check RRRR */
		if (data > 0x0500) 	/* need > 0500 */
			return true;
	}
	return false;
}

static int do_master(struct mdev_ctx *mctx)
{
	int dt = mctx->dt;

	mctx->loop++;
	VERBOSE1("[%s] AFU[%d] Loop: %d Delay: %d sec mode: 0x%x left: %d\n",
		__func__, mctx->card, mctx->loop,
		mctx->dt, mctx->mode, mctx->count);

	if (CHECK_FIRS_MODE == (CHECK_FIRS_MODE & mctx->mode))
		dt = afu_check_mfirs(mctx);

	if (CHECK_TIME_MODE == (CHECK_TIME_MODE & mctx->mode))
		dt = afu_check_stime(mctx);

	return dt;
}

static void sig_handler(int sig)
{
	struct mdev_ctx *mctx = &master_ctx;

	VERBOSE0("Sig Handler Signal: %d SID: %d\n", sig, mctx->my_sid);
	afu_m_close(mctx);
	fflush(fd_out);
	fclose(fd_out);
	exit(0);
}

static void help(char *prog)
{
	printf("Usage: %s [-CvhVd] [-f file] [-c count] [-i delay]\n"
		"\t-C, --card <num>	Card to use (default 0)\n"
		"\t-V, --version	\tPrint Version number\n"
		"\t-h, --help		This help message\n"
		"\t-q, --quiet		No output at all\n"
		"\t-v, --verbose	\tverbose mode, up to -vvv\n"
		"\t-c, --count <num>	Loops to run (-1 = forever)\n"
		"\t-i, --interval <num>	Interval time in sec (default 1 sec)\n"
		"\t-d, --deamon		Start in Deamon process (background)\n"
		"\t-m, --mode		Mode:\n"
		"\t	1 = Check Master Firs\n"
		"\t	2 = Watch Card worktimer\n"
		"\t-f, --log-file <file> Log File name when running in -d (deamon)\n",
		prog);
	return;
}

/**
 * Get command line parameters and create the output file.
 */
int main(int argc, char *argv[])
{
	int rc;
	int ch;
	char *log_file = NULL;
	struct mdev_ctx *mctx = &master_ctx;
	int	dt;
	int	mode;

	fd_out = stdout;	/* Default */
	mctx->loop = 0;		/* Counter */
	mctx->quiet = false;
	mctx->dt = 1;		/* Default, 1 sec delay time */
	mctx->count = 1;	/* Default, run once */
	mctx->card = 0;		/* Default, Card 0 */
	mctx->mode = 0;		/* Default, nothing to watch */
	mctx->process_irqs = 0;	/* No Master IRQ's received */
	mctx->deamon = false;	/* Not in Deamon mode */

	rc = EXIT_SUCCESS;
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{ "card",       required_argument, NULL, 'C' },
			{ "version",    no_argument,       NULL, 'V' },
			{ "quiet",      no_argument,       NULL, 'q' },
			{ "help",       no_argument,       NULL, 'h' },
			{ "verbose",    no_argument,       NULL, 'v' },
			{ "count",      required_argument, NULL, 'c' },
			{ "interval",   required_argument, NULL, 'i' },
			{ "deamon",     no_argument,       NULL, 'd' },
			{ "log-file",   required_argument, NULL, 'f' },
			{ "mode",   	required_argument, NULL, 'm' },
			{ 0,            0,       	   NULL,  0  }
		};
		ch = getopt_long(argc, argv, "C:f:c:i:m:Vqhvd",
			long_options, &option_index);
		if (-1 == ch)
			break;
		switch (ch) {
		case 'C':	/* --card */
			mctx->card = strtol(optarg, (char **)NULL, 0);
			break;
		case 'V':	/* --version */
			printf("%s\n", version);
			exit(EXIT_SUCCESS);
			break;
		case 'q':	/* --quiet */
			mctx->quiet = true;
			break;
		case 'h':	/* --help */
			help(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		case 'v':	/* --verbose */
			verbose++;
			break;
		case 'c':	/* --count */
			mctx->count = strtoul(optarg, NULL, 0);
			if (0 == mctx->count)
				mctx->count = 1;
			break;
		case 'i':	/* --interval */
			mctx->dt = strtoul(optarg, NULL, 0);
			break;
		case 'd':	/* --deamon */
			mctx->deamon = true;
			break;
		case 'm':	/* --mode */
			mode = strtoul(optarg, NULL, 0);
			switch (mode) {
			case 1: mctx->mode |= CHECK_FIRS_MODE; break;
			case 2: mctx->mode |= CHECK_TIME_MODE; break;
			default:
				printf("Please provide correct Mode Option (1..2)\n");
				exit(1);
				break;
			}
			break;
		case 'f':	/* --log-file */
			log_file = optarg;
			break;
		default:
			help(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if ((mctx->card < 0) || (mctx->card > 1)) {
		printf("%d for Option -C is invalid, please provide either 0 or 1 !\n",
			mctx->card);
		exit(EXIT_FAILURE);
	}

	if (mctx->deamon) {
		if (NULL == log_file) {
			printf("Please Provide log file name (-f) if running "
				"in deamon mode !\n");
			exit(1);
		}
	}
	if (log_file) {
		fd_out = fopen(log_file, "w+");
		if (NULL == fd_out) {
			printf("Can not create/append to file %s\n", log_file);
			exit(1);
		}
	}
	signal(SIGCHLD,SIG_IGN);	/* ignore child */
	signal(SIGTSTP,SIG_IGN);	/* ignore tty signals */
	signal(SIGTTOU,SIG_IGN);
	signal(SIGTTIN,SIG_IGN);
	signal(SIGHUP,sig_handler);	/* catch -1 hangup signal */
	signal(SIGINT, sig_handler);	/* Catch -2 */
	signal(SIGKILL,sig_handler);	/* catch -9 kill signal */
	signal(SIGTERM,sig_handler);	/* catch -15 kill signal */

	if (mctx->deamon) {
		mctx->pid = fork();
		if (mctx->pid < 0) {
			printf("Fork() failed\n");
			exit(1);
		}
		if (mctx->pid > 0) {
			printf("Child Pid is %d Parent exit here\n", mctx->pid);
			exit(0);
		}
		if (chdir("/")) {
			printf("Can not chdir to / !!!\n");
			exit(1);
		}
		umask(0);
		//set new session
		mctx->my_sid = setsid();
		printf("Child sid: %d from pid: %d\n", mctx->my_sid, mctx->pid);
		if(mctx->my_sid < 0)
			exit(1);
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	}

	if (0 != afu_m_open(mctx)) {
		VERBOSE0("Error: failed to open Master Context for CAPI Card: %u\n",
                        mctx->card);
		VERBOSE0("\tCheck Permissions in /dev/cxl/* or kernel log.\n");
                exit(EXIT_FAILURE);
	}

	if (false == check_app(mctx)) {
		VERBOSE0("Err: Wrong Card Appl ID. Need to have > 0500\n");
		afu_m_close(mctx);
                exit(EXIT_FAILURE);
	}

	while (1) {
		dt = do_master(mctx);		/* Process */
		if (dt)
			sleep(dt);		/* Sleep Remaining time */
		if (-1 == mctx->count)
			continue;		/* Run Forever */
		mctx->count--;		/* Decrement Runs */
		if (0 == mctx->count)
			break;			/* Exit */
	}

	if (!mctx->quiet && verbose)
		VERBOSE0("[%s] AFU[%d] after %d loops and %d Interrupts\n",
			__func__, mctx->card,
			mctx->loop, mctx->process_irqs);
	afu_m_close(mctx);
	fflush(fd_out);
	fclose(fd_out);
	exit(rc);
}
