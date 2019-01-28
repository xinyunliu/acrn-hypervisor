/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "vmmapi.h"
#include "inout.h"
#include "lpc.h"
#include "pmtimer.h"

static struct vpmtmr vpmtimer;

void set_pmtmr_val(struct vpmtmr *vpmtmr, uint32_t val)
{
	struct itimerspec its;
	uint64_t tv_nsecs;
	uint32_t cnt2carry;

	if (val & PMTMR_CARRY_MASK)
		vpmtmr->msb_is_set = true;
	else
		vpmtmr->msb_is_set = false;

	/* how many counts left in TMR_VAL to its carry bit (i.e. msb) flip */
	cnt2carry = PMTMR_NOCARRY_CNTS - (val & PMTMR_NOCARRY_MASK);

	/* calculate it_value */
	its.it_value.tv_sec = cnt2carry / PMTMR_TICK_RATE;
	tv_nsecs = (uint64_t)cnt2carry * NANOSEC_TICK_RATE / PMTMR_TICK_RATE;
	its.it_value.tv_nsec = tv_nsecs
			- (uint64_t)its.it_value.tv_sec * NANOSEC_TICK_RATE;

	/* we will reload the value in pmtmr event handler when timer expires,
	 * so don't need a period timer here.
	 */
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;

	acrn_timer_settime(&vpmtmr->timer, &its);
}

uint32_t get_pmtmr_val(struct vpmtmr *vpmtmr)
{
	struct itimerspec its;
	uint64_t val_nsecs;
	uint32_t tmr_val, cnt2carry;

	pthread_mutex_lock(&vpmtmr->mtx);

	/* 1. get how many time left (in nano seconds)
	 * that carry bit (i.e. msb) of TMR_VAL flips.
	 */
	acrn_timer_gettime(&vpmtmr->timer, &its);
	val_nsecs = its.it_value.tv_sec * NANOSEC_TICK_RATE
					+ its.it_value.tv_nsec;

	/* 2. calculate how many counts left
	 * that carry bit (i.e. msb) of TMR_VAL flips.
	 * TMR_VAL rate is PMTMR_TICK_RATE
	 * so 1 unit in TMR_VAL means
	 *        (NANOSEC_TICK_RATE / PMTMR_TICK_RATE) nano seconds
	 * then left counts to carry should be:
	 *  cnt2carry = val_nsecs / (NANOSEC_TICK_RATE / PMTMR_TICK_RATE)
	 * to reduce calculate error, the formula would be
	 *  cnt2carry = (val_nsecs * PMTMR_TICK_RATE) / NANOSEC_TICK_RATE;
	 */
	cnt2carry = (val_nsecs * PMTMR_TICK_RATE) / NANOSEC_TICK_RATE;

	tmr_val = PMTMR_NOCARRY_CNTS -
			((uint32_t)cnt2carry & PMTMR_NOCARRY_MASK);

	if (vpmtmr->msb_is_set)
		tmr_val |= PMTMR_CARRY_MASK;
	else
		tmr_val &= ~PMTMR_CARRY_MASK;

	pthread_mutex_unlock(&vpmtmr->mtx);

	return tmr_val;
}

static int32_t
vpmtmr_io_handler(struct vmctx *ctx, int vcpu, int in, int port,
		  int bytes, uint32_t *eax, void *arg)
{
	struct vpmtmr *vpmtmr = ctx->vpmtmr;

	if (in) {
		*eax = get_pmtmr_val(vpmtmr);
	} else {
		printf("Invalid IO write! PMTimer port is read only!\n");
	}

	return 0;
}

void vpmtmr_init(struct vmctx *ctx)
{
	struct vpmtmr *vpmtmr = &vpmtimer;

	vpmtmr->vm = ctx;
	ctx->vpmtmr = vpmtmr;

	vpmtmr->timer.clockid = CLOCK_MONOTONIC;
	if (acrn_timer_init(&vpmtmr->timer, vpmtmr_event_handler,
						vpmtmr) != 0) {
		vpmtmr->io_addr = 0;
		return;
	}

	vpmtmr->io_addr = IO_PMTMR;
	vpmtmr->msb_is_set = false;

	pthread_mutex_init(&vpmtmr->mtx, NULL);

	/* Per ACPI spec, pmtmr val could be any value from boot,
	 *  set the initial value to 0.
	 */
	set_pmtmr_val(vpmtmr, 0);

}

void vpmtmr_deinit(struct vmctx *ctx)
{
	struct vpmtmr *vpmtmr = ctx->vpmtmr;
	struct inout_port iop;

	if (vpmtmr->io_addr != 0) {
		memset(&iop, 0, sizeof(struct inout_port));
		iop.name = "pmtimer";
		iop.port = vpmtmr->io_addr;
		iop.size = 4;
		unregister_inout(&iop);

		acrn_timer_deinit(&vpmtmr->timer);
	}

	ctx->vpmtmr = NULL;
}

INOUT_PORT(pmtimer, IO_PMTMR, IOPORT_F_INOUT, vpmtmr_io_handler);
SYSRES_IO(IO_PMTMR, 4);
