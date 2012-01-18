/*
 * Open-MX
 * Copyright © inria 2010
 * (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

/* This file contains an implementation of a list manipulation interface.
 * This implementation was written from scratch by Ludovic Stordeur who
 * did not have any knowledge of any other list interface implementation.
 * Thus this work may not be considered as a derivative work. */


#ifndef __omx_list_h__
#define __omx_list_h__

#include <stddef.h>

#include "omx_debug.h"


#define containerof(elt, type, field)	\
	(type *)((char *)(elt) - offsetof(type, field))


static inline void
list_head_init(struct list_head *node)
{
	omx__debug_assert(node);

	node->prv = node;
	node->nxt = node;
}

static inline void
list_add_after(struct list_head *new, struct list_head *node)
{
	omx__debug_assert(node);
	omx__debug_assert(new);

	new->prv      = node;
	new->nxt      = node->nxt;
	node->nxt     = new;
	new->nxt->prv = new;
}

static inline void
list_add_tail(struct list_head *new, struct list_head *node)
{
	omx__debug_assert(node);
	omx__debug_assert(new);

	new->prv      = node->prv;
	new->nxt      = node;
	node->prv     = new;
	new->prv->nxt = new;
}

static inline void
list_del(const struct list_head *node)
{
	omx__debug_assert(node);

	node->prv->nxt = node->nxt;
	node->nxt->prv = node->prv;
}

static inline int
list_empty(const struct list_head *list)
{
	omx__debug_assert(list);

	return (list->nxt == list);
}

#define list_first_entry(list, type, field) \
	containerof((list)->nxt, type, field)

#define list_last_entry(list, type, field) \
	containerof((list)->prv, type, field)

static inline void
list_move(struct list_head *node, struct list_head *node_to)
{
	omx__debug_assert(node);
	omx__debug_assert(node_to);

	list_del(node);
	list_add_after(node, node_to);
}

static inline void
list_spliceall_tail(struct list_head *src, struct list_head *dst)
{
	if (! list_empty(src)) {
		dst->prv->nxt = src->nxt;
		src->nxt->prv = dst->prv;

		src->prv->nxt = dst;
		dst->prv      = src->prv;
	}
}

#define __list_for_each(iter, list)	\
	for (iter = (list)->nxt; iter != (list); iter = iter->nxt)

static inline int
__list_check_elt(const struct list_head *list, const struct list_head *elt)
{
	struct list_head *iter;

	omx__debug_assert(list);
	omx__debug_assert(elt);

	__list_for_each(iter, list)
		if (iter == elt)
			return 1;

	return 0;
}

#ifdef OMX_LIB_DEBUG
#  define list_check_elt(list, elt, ep_msg, rest...)	\
	do {						\
		if (! __list_check_elt(list, elt))	\
			omx__abort(ep_msg, ## rest);	\
	} while (0)
#else
#  define list_check_elt(list, elt, rest...) (void)0
#endif

static inline unsigned
list_count(const struct list_head *list)
{
	struct list_head *iter;
	unsigned acc = 0;

	omx__debug_assert(list);

	__list_for_each(iter, list)
		acc++;

	return acc;
}


#define list_for_each_entry(e, list, field)				\
	for (e = list_first_entry((list), typeof (*e), field);		\
	     &e->field != (list);					\
	     e = list_first_entry(&e->field, typeof (*e), field))

#define list_for_each_entry_reverse(e, list, field)			\
	for (e = list_last_entry((list), typeof (*e), field);		\
	     &e->field != (list);					\
	     e = list_last_entry(&e->field, typeof (*e), field))

#define list_for_each_entry_safe(e,next,list,field)			\
	for (e = list_first_entry((list), typeof (*e), field),		\
		     next = list_first_entry(&e->field, typeof (*e), field); \
	     &e->field != (list);					\
	     e = next, next = list_first_entry(&next->field, typeof (*e), field))


#endif /* __omx_list_h__ */

