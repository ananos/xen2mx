/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
 *
 * This file has been copied and modified from mxoed.c in Myricom's
 * Myrinet Express software. Copyright 2003 - 2008 by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License in COPYING.GPL for more details.
 */

#define _BSD_SOURCE 1 /* for random, srandom and setlinebuf */
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/types.h>

#include "omx_lib.h"
#include "omx_raw.h"

#define MXOED_LOGFILE "/var/log/omxoed.log"

#define MXOED_DEBUG 0

#define MAX_PEERS 8192
#define MAX_NICS 8
#define MXOE_PORT 2314
#define MAX_IFC_CNT 16
#define BROADCAST_INTERVAL 1000
#define LONG_BROADCAST_INTERVAL 180000
#define BROADCAST_COUNT 8

#define ETHER_TYPE_MX	0x86DF
#define MYRI_TYPE_ETHER 0x0009

/*
 * Info about each NIC
 */
struct mxoed_pkt {
  uint32_t dest_mac_high32;
  uint16_t dest_mac_low16;
  uint16_t src_mac_high16;
  uint32_t src_mac_low32;
  uint16_t proto;		/* ethertype */
  uint16_t sender_peer_index;
  uint8_t pkt_type;
  uint8_t chargap[3];
  uint32_t gap[3];		/* pad to 32 bytes */
  uint32_t nic_id_hi;
  uint32_t nic_id_lo;
  uint32_t serial;
  uint8_t  pad[20];		/* then to 64 bytes */
};

struct nic_info {
  omx_raw_endpoint_t raw_ep;

  int nic_index;
  uint64_t my_nic_id;
  uint32_t my_serial;

  uint64_t peers[MAX_PEERS];
  uint64_t gw[MAX_PEERS];
  uint32_t peer_serial[MAX_PEERS];
  int num_peers;

  int bc_count;
  int bc_interval;

  struct mxoed_pkt outpkt;
  struct mxoed_pkt mxoepkt;

  int die; /* set to non-zero to exit */
};


/* return difference between two timevals in ms (t1 - t2) */
static inline int32_t
tv_diff(
  struct timeval *t1,
  struct timeval *t2)
{
  int32_t ms;

  ms = (t1->tv_sec - t2->tv_sec) * 1000;
  ms += (t1->tv_usec - t2->tv_usec) / 1000;
  return ms;
}

void
add_peer(
  struct nic_info *nip,
  uint64_t peer_mac,
  int serial,
  uint64_t gw)
{
  /* Add this to our local peer table */
  nip->peers[nip->num_peers] = peer_mac;
  nip->gw[nip->num_peers] = gw;
  nip->peer_serial[nip->num_peers] = serial;
  ++nip->num_peers;

  omx__driver_peer_add(peer_mac, NULL);
  omx__driver_set_peer_table_state(1, 1, nip->num_peers+1, nip->my_nic_id);
}

int
get_peer_index(
  struct nic_info *nip,
  uint64_t peer_mac)
{
  int i;

  for (i=0; i<nip->num_peers; ++i) {
    if (nip->peers[i] == peer_mac) {
      return i;
    }
  }
  return -1;
}

void
broadcast_my_id(
  struct nic_info *nip)
{
  omx_return_t ret;

  ret = omx_raw_send(nip->raw_ep, &nip->outpkt, sizeof(nip->outpkt));
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Error sending raw packet: %s\n", omx_strerror(ret));
    exit(1);
  }
#if MXOED_DEBUG
    printf("sent my ID\n");
#endif
}

int
check_for_packet(
  struct nic_info *nip,
  int *elapsed)
{
  uint32_t len;
  struct timeval before;
  struct timeval after;
  int timeout;
  omx_raw_status_t status;
  omx_return_t ret;
  uint32_t length;
  int rc;

  gettimeofday(&before, NULL);

  len = sizeof(nip->mxoepkt);
  if (nip->bc_interval > 0) {
    timeout = nip->bc_interval;
  } else {
    timeout = 0;
  }

  length = sizeof(nip->mxoepkt);
  ret = omx_raw_next_event(nip->raw_ep,
			   &nip->mxoepkt, &length,
			   timeout, &status);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Error from omx_raw_next_event: %s\n", omx_strerror(ret));
    exit(1);
  }

  gettimeofday(&after, NULL);

  if (status == OMX_RAW_RECV_COMPLETE) {
#if MXOED_DEBUG
    int i;
    unsigned char *p = (unsigned char *)(&nip->mxoepkt);

    printf("recv len = %d\n", len);
    for (i=0; i<16; ++i) printf(" %02x", p[i]); printf("\n");
    for (; i<32; ++i) printf(" %02x", p[i]); printf("\n");
    for (; i<48; ++i) printf(" %02x", p[i]); printf("\n");
#endif

    rc = 1;

  } else {
    rc = 0;
  }

  /* return elapsed time in ms */
  *elapsed = tv_diff(&after, &before);

#if MXOED_DEBUG
  printf("elapsed = %d\n", *elapsed);
#endif

  return rc;
}

void
process_pkt(
  struct nic_info *nip)
{
  uint32_t nic_half;
  uint64_t nic_id;
  int serial;
  int index;
  struct mxoed_pkt *pkt;
  uint64_t gw;

  pkt = &nip->mxoepkt;
  gw = 0;

  /* get peer NIC id from packet */
  nic_id = ntohl(pkt->nic_id_lo);
  nic_half = ntohl(pkt->nic_id_hi);
  nic_id |= ((uint64_t)nic_half << 32);

  serial = ntohl(pkt->serial);

  if (nic_id == nip->my_nic_id) {
    return;
  }

#if MXOED_DEBUG
  printf("got pkt from nic_id %02x%04x, sn=%d ",
      nic_half, ntohl(pkt->nic_id_lo), serial);
#endif

  index = get_peer_index(nip, nic_id);
  if (index == -1) {
#if MXOED_DEBUG
  printf("new peer\n");
#endif
    add_peer(nip, nic_id, serial, gw);
    nip->bc_count = BROADCAST_COUNT;

    /* make sure interval is at most BROADCAST_INTERVAL */
    if (nip->bc_interval > BROADCAST_INTERVAL) {
      nip->bc_interval = BROADCAST_INTERVAL;
    }
  } else {

    /* new serial number means he likely does not know me */
    if (nip->peer_serial[index] != serial) {
#if MXOED_DEBUG
      printf("known, but serial changed\n");
#endif

      /* record new serial # and broadcast my ID */
      nip->peer_serial[index] = serial;
      nip->bc_count = BROADCAST_COUNT;
      if (nip->bc_interval > BROADCAST_INTERVAL) {
	nip->bc_interval = BROADCAST_INTERVAL;
      }
    } else {
#if MXOED_DEBUG
      printf("already known\n");
#endif
    }
  }
  return;
}

void
fill_nic_info(
  struct nic_info *nip)
{
  omx_return_t omxrc;
  uint32_t nic_half;
  uint32_t board_num;

  omxrc = omx_board_number_to_nic_id(nip->nic_index, &nip->my_nic_id);
  if (omxrc != OMX_SUCCESS) {
    fprintf(stderr, "Error getting nic_id for NIC %d\n", nip->nic_index);
    exit(1);
  }

  memset(&nip->outpkt, 0, sizeof(nip->outpkt));
  nip->outpkt.dest_mac_high32 = 0xFFFFFFFF;
  nip->outpkt.dest_mac_low16 = 0xFFFF;
  nip->outpkt.src_mac_high16 = htons((nip->my_nic_id >> 32) & 0xFFFF);
  nip->outpkt.src_mac_low32 = htonl(nip->my_nic_id & 0xFFFFFFFF);
  nip->outpkt.proto = htons(0x86DF);
  nip->outpkt.pkt_type = 1;

  add_peer(nip, nip->my_nic_id, 0, 0);

  /* put my nic_id in outbound packet */
  nic_half = (nip->my_nic_id >> 32) & 0xFFFFFFFF;
  nip->outpkt.nic_id_hi = htonl(nic_half);
  nic_half = nip->my_nic_id & 0xFFFFFFFF;
  nip->outpkt.nic_id_lo = htonl(nic_half);

  /* assign a random serial number for this invocation */
  nip->my_serial = random();
  nip->outpkt.serial = htonl(nip->my_serial);

  board_num = nip->nic_index;
}

void *
nic_thread(
  void *vnip)
{
  struct nic_info *nip;
  int elapsed;
  int rc;

  nip = vnip;

  fill_nic_info(nip);

  nip->bc_count = BROADCAST_COUNT;
  nip->bc_interval = 0;

  while (!nip->die) {

    /* If broadcasts left to do and interval expired, send one now */
    if (nip->bc_count > 0 && nip->bc_interval <= 0) {
      broadcast_my_id(nip);
      --nip->bc_count;
      if (nip->bc_count > 0) {
	nip->bc_interval = BROADCAST_INTERVAL;
      } else {
	nip->bc_count = 1;
	nip->bc_interval = LONG_BROADCAST_INTERVAL;
      }
    }

    rc = check_for_packet(nip, &elapsed);

    if (nip->bc_interval > 0) {
      nip->bc_interval -= elapsed;
    }

    if (rc > 0) {
      process_pkt(nip);
    }
  }
  return NULL;
}


/*
 * Open NICs
 */
void
open_all_nics()
{
  int i;
  int rc;
  int num_nics;
  struct nic_info *nip, *nip0 = NULL;
  pthread_t tid;

  num_nics = 0;
  for (i=0; i<MAX_NICS; ++i) {
    omx_raw_endpoint_t ep;
    omx_return_t ret;

    ret = omx_raw_open_endpoint(i, NULL, 0, &ep);
    if (ret != OMX_SUCCESS) {
      if (ret == OMX_BOARD_NOT_FOUND)
	continue;
      fprintf(stderr, "Error opening raw endpoint for NIC %d, %m\n", i);
      exit(1);
    }

    /* allocate NIC info struct */
    nip = (struct nic_info *) calloc(sizeof(*nip), 1);
    if (nip == NULL) {
      fprintf(stderr, "Error allocating NIC info struct\n");
      exit(1);
    }
    nip->raw_ep = ep;
    nip->nic_index = i;
    nip->die = 0;

    /* the first NIC will be handled in main thread */
    if (num_nics > 0) {
      rc = pthread_create(&tid, NULL, nic_thread, nip);
      if (rc != 0) {
	fprintf(stderr, "Error creating thread for NIC %d\n", i);
	exit(1);
      }
    } else {
      nip0 = nip;
    }
    ++num_nics;
  }

  /* Error out if no NICs opened */
  if (num_nics == 0) {
    fprintf(stderr, "No NICs found\n");
    exit(1);
  } else {
    fprintf(stderr, "Now managing %d NICs...\n", num_nics);
    nic_thread(nip0);
  }
}

int
main(
  int argc,
  char *argv[])
{
  srandom((unsigned int)time(NULL));
  setlinebuf(stdout);
  if (!freopen(MXOED_LOGFILE, "w", stderr))
    fprintf(stderr, "%s: Failed to open " MXOED_LOGFILE ", sending errors to stderr.\n", argv[0]);
  setlinebuf(stderr);

  /* init mx */
  omx_init();
  open_all_nics();
  exit(0);
}
