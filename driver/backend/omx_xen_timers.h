/*
 * Copyright (C) 2012 (See AUTHORS file)
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

#ifndef __slurpoe_timers_h__
#define __slurpoe_timers_h__

typedef struct timers {
	unsigned long long total;
	unsigned long long val;
	unsigned long cnt;
} timers_t;

extern timers_t t_recv, t_rndv, t_notify, t_small, t_tiny, t_medium, t_connect, t_truc;
extern timers_t t_pull_request, t_pull_reply, t_pull, t_handle;
extern timers_t t_send_tiny, t_send_small, t_send_medium, t_send_connect, t_send_notify, t_send_connect_reply, t_send_rndv, t_send_liback;
#ifdef TIMERS_ENABLED

#include <asm/msr.h>
//#include <sys/time.h>
#include <linux/timex.h>	/* x86 rdtsc instruction support */

//typedef unsigned long long cycles_t;

#define barrier() __asm__ __volatile__("": : :"memory")

//#define rdtsc(low,high) __asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high))

//#define rdtscl(low) __asm__ __volatile__("rdtsc" : "=a" (low) : : "edx")

//#define rdtscll(val) __asm__ __volatile__("rdtsc" : "=A" (val))


#if 0
static inline cycles_t get_cycles(void)
{
	unsigned long long ret = 0;
	barrier();
	rdtscll(ret);
	barrier();
	return ret;
}
#endif

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


#endif	/* __slurpoe_timers_h__ */

