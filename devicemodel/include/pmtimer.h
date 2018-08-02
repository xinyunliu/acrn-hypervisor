/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef _PMTIMER_H_
#define _PMTIMER_H_

#include "timer.h"

#define IO_PMTMR		0x408	/* 4-byte i/o port for the timer */
#define PMTMR_32BIT		true

#define PMTMR_TICK_RATE		3579545
#define NANOSEC_TICK_RATE	1000000000
#define PMTMR_NOCARRY_MASK	(PMTMR_32BIT ? 0x7fffffff : 0x7fffff)
#define PMTMR_NOCARRY_CNTS	PMTMR_NOCARRY_MASK
#define PMTMR_CARRY_MASK	(PMTMR_32BIT ? 0x80000000 : 0x800000)

struct vpmtmr {
	struct vmctx *vm;
	struct acrn_timer timer;
	pthread_mutex_t mtx;
	uint16_t io_addr;
	bool msb_is_set;
};

void vpmtmr_init(struct vmctx *ctx);
void vpmtmr_deinit(struct vmctx *ctx);
void vpmtmr_event_handler(void *arg);
void set_pmtmr_val(struct vpmtmr *vpmtmr, uint32_t val);
uint32_t get_pmtmr_val(struct vpmtmr *vpmtmr);

#endif /* _PMTIMER_H_ */
