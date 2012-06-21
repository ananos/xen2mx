/*
 * Copyright (C) Anastassios Nanos 2012 (See AUTHORS file)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details. You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#define _GNU_SOURCE

#ifndef __omx_xen_timers_h__
#define __omx_xen_timers_h__

typedef struct timers {
	unsigned long long total;
	unsigned long long val;
	unsigned long cnt;
} timers_t;


#ifdef TIMERS_ENABLED

#include <asm/msr.h>
#include <linux/timex.h>

#define TIMER_START(tp)	do {( tp)->val = get_cycles(); } while (0)
#define TIMER_STOP(tp)	do { (tp)->total += get_cycles() - (tp)->val; ++(tp)->cnt; } while (0)
#define TIMER_RESET(tp)	do { (tp)->total = (tp)->val = 0; (tp)->cnt = 0; } while (0)
#define TIMER_TOTAL(tp)	((tp)->total)
#define TIMER_COUNT(tp)	((tp)->cnt)
#define TIMER_AVG(tp)	((tp)->cnt ? ((tp)->total / (tp)->cnt) : -1)

#define TICKS_TO_USEC(t)	(1000 * t/CYCLES_PER_SEC)

#else

#define TIMER_START(a)
#define TIMER_STOP(a)
#define TIMER_TOTAL(a) 0ULL
#define TIMER_COUNT(a) 0UL
#define TIMER_RESET(a)
#define TICKS_TO_USEC(a) 0ULL

#endif	/* TIMERS_ENABLED */

#define var_name(x) #x
#define omx_xen_timer_reset(x) TIMER_RESET(x);
#endif	/* __omx_xen_timers_h__ */
