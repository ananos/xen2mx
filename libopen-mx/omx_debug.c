/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
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

#include <signal.h>

#include "omx_lib.h"
#include "omx_request.h"

static void
omx__dump_request(const char *prefix, union omx_request *req)
{
  struct omx__partner *partner = req->generic.partner;
  enum omx__request_type type = req->generic.type;
  uint16_t state = req->generic.state;
  char strstate[128];
  omx__sprintf_reqstate(state, strstate);

  printf("%stype %s state %s\n",
	 prefix,
	 omx__strreqtype(type),
	 strstate);

  if (type == OMX_REQUEST_TYPE_SEND_TINY
      || type == OMX_REQUEST_TYPE_SEND_SMALL
      || type == OMX_REQUEST_TYPE_SEND_MEDIUM
      || type == OMX_REQUEST_TYPE_SEND_LARGE) {
    printf("%s  matchinfo %llx to addr %llx ep %d peer %d session %d seqnum %d resends %d\n",
	   prefix,
	   (unsigned long long) req->generic.status.match_info,
	   (unsigned long long) partner->board_addr,
	   (unsigned) partner->endpoint_index,
	   (unsigned) partner->peer_index,
	   (unsigned) OMX__SESNUM(req->generic.send_seqnum),
	   (unsigned) OMX__SEQNUM(req->generic.send_seqnum),
	   (unsigned) req->generic.resends);

  } else {
    printf("%s  match info %llx mask %llx\n",
	   prefix,
	   (unsigned long long) req->recv.match_info,
	   (unsigned long long) req->recv.match_mask);
    if (type == OMX_REQUEST_TYPE_RECV_LARGE && !(state & OMX_REQUEST_STATE_RECV_PARTIAL))
      printf("%s  to addr %llx ep %d peer %d session %d seqnum %d resends %d\n",
	     prefix,
	     (unsigned long long) partner->board_addr,
	     (unsigned) partner->endpoint_index,
	     (unsigned) partner->peer_index,
	     (unsigned) OMX__SESNUM(req->generic.send_seqnum),
	     (unsigned) OMX__SEQNUM(req->generic.send_seqnum),
	     (unsigned) req->generic.resends);
  }
}

static void
omx__dump_req_q(const char * name, struct list_head *head)
{
  union omx_request *req;
  int count;

  printf("  %s:\n", name);
  count = 0;
  omx__foreach_request(head, req) {
    omx__dump_request("    ", req);
    count++;
  }
  printf("   (%d requests)\n", count);
}

static void
omx__dump_req_ctxidq(const char * name, struct list_head *head, int max, int offset)
{
  union omx_request *req;
  int i, count;

  printf("  %s:\n", name);
  count = 0;
  for(i=0; i<max; i++) {
    omx__foreach_request(head+i*offset, req) {
      omx__dump_request("    ", req);
      count++;
    }
  }
  printf("   (%d requests)\n", count);
}

static void
omx__dump_endpoint(struct omx_endpoint *ep)
{
  union omx_request *req;
  struct omx__early_packet *early;
  int i, count, count2;

  OMX__ENDPOINT_LOCK(ep);

  printf("Endpoint %d on Board %d:\n",
	 ep->endpoint_index, ep->board_index);

  printf("  Partners:\n");
  count = 0;
  for(i=0; i<omx__driver_desc->peer_max * omx__driver_desc->endpoint_max; i++) {
    struct omx__partner *partner = ep->partners[i];
    if (partner && partner != ep->myself) {
      printf("    partner addr %llx endpoint %d index %d:\n",
	     (unsigned long long) partner->board_addr,
	     (unsigned) partner->endpoint_index,
	     (unsigned) partner->peer_index);
      printf("      send session %x next %d ack next %d\n",
	     (unsigned) OMX__SESNUM(partner->next_send_seq),
	     (unsigned) OMX__SEQNUM(partner->next_send_seq),
	     (unsigned) OMX__SEQNUM(partner->next_acked_send_seq));
      printf("      recv session %x next match %d next frag %d last acked %d\n",
	     (unsigned) OMX__SESNUM(partner->next_match_recv_seq),
	     (unsigned) OMX__SEQNUM(partner->next_match_recv_seq),
	     (unsigned) OMX__SEQNUM(partner->next_frag_recv_seq),
	     (unsigned) OMX__SEQNUM(partner->last_acked_recv_seq));
      count++;

      printf("    Throttling send requests:\n");
      count2 = 0;
      omx__foreach_partner_request(&partner->throttling_send_req_q, req) {
	omx__dump_request("      ", req);
	count2++;
      }
      printf("     (%d throttling send requests)\n", count2);

      printf("    Non-acked requests:\n");
      count2 = 0;
      omx__foreach_partner_request(&partner->non_acked_req_q, req) {
	omx__dump_request("      ", req);
	count2++;
      }
      printf("     (%d non-acked requests)\n", count2);

      count2 = 0;
      omx__foreach_partner_early_packet(partner, early)
	count2++;
      printf("    Early packets:\n");
      printf("     (%d early packets)\n", count2);
   }
  }
  printf("   (%d partners excluding myself)\n", count);

  omx__dump_req_ctxidq("Recv", &ep->ctxid[0].recv_req_q, ep->ctxid_max, sizeof(ep->ctxid[0]));
  omx__dump_req_ctxidq("Unexpected", &ep->ctxid[0].unexp_req_q, ep->ctxid_max, sizeof(ep->ctxid[0]));
  omx__dump_req_ctxidq("Done", &ep->ctxid[0].done_req_q, ep->ctxid_max, sizeof(ep->ctxid[0]));
  omx__dump_req_q("Delayed for missing resources", &ep->need_resources_send_req_q);
  omx__dump_req_q("Driver medium sending", &ep->driver_medium_sending_req_q);
  omx__dump_req_q("Multifrag medium recv", &ep->multifrag_medium_recv_req_q);
  omx__dump_req_q("Large send", &ep->large_send_need_reply_req_q);
  omx__dump_req_q("Driver pulling", &ep->driver_pulling_req_q);
  omx__dump_req_q("Connect", &ep->connect_req_q);
  omx__dump_req_q("Unexpected self send", &ep->unexp_self_send_req_q);

  printf("\n");
  OMX__ENDPOINT_UNLOCK(ep);
}

static void
omx__debug_signal_handler(int signum)
{
  omx__foreach_endpoint(omx__dump_endpoint);
}

void
omx__debug_init(int signum)
{
  struct sigaction action;
  action.sa_handler = omx__debug_signal_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  if (sigaction(signum, &action, NULL) < 0) {
    fprintf(stderr, "Failed to setup debug signal handler, %m\n");
  }
}
