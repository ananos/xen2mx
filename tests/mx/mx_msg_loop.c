#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <getopt.h>

#include "myriexpress.h"
#if MX_OS_WINNT
#include <winsock2.h>
#include "mx_uni.h"
#define putenv _putenv
#else
#include <sys/time.h>
#endif

#define XXSTR(a) #a
#define XSTR(a) XXSTR(a)

#define MAX_SIZE 100000
#define PAD_LEN 4096 
#define PAD_INITIAL_VAL 0xfeU
#define PAD_SBUF_AROUND 0xfdU
#define PAD_RBUF_AROUND 0xfcU

#define MAX_CONCURRENT 128

void
usage(void)
{
  fprintf(stderr,
	  "usage: mx_msg_loop [ args ]\n"
	  "-b <board_id>\n"
	  "-S <start_len> -- (default 0)\n"
	  "-E <end_len> -- (default "XSTR(MAX_SIZE)")\n"
	  "-a <start_alignment> -- (default 0)\n"
	  "-A <end_alignment> -- (default 128)\n"
	  "-I <increment> -- (default 1)\n"
	  "-M <multiplier> -- (overrides -I)\n"
	  "-R  -- Random sizes (overrides -I and -M)\n"
	  "-P <n> -- do <n> messages in parallel\n"
	  "-N <n> -- number of iterations for each test\n"
	  "-s  -- use rndv/synchronous messages\n"
	  "-n  -- use network (disable self/shmem channed) [ default ]\n"
	  "-m  -- use self/shmem communication (NIC stays unused)\n"
	  "-k  -- keep going after an error\n" 
	  "-f  -- fast test no checking\n"
	  "-d  -- deterministic contents: all messages contents are 0,1,2,3,.. (16bit word)\n"
	  "\totherwise a random offset is added\n"
	  "-v  -- verbose : print each message status, or each error byte\n"
	  "-i <seconds> -- delay between printing progress status ou stdout\n"
	  "-t <timeout> -- maximum delay to wait for a completion (ms)\n"
	  );
  exit(1);
}

static double
get_dtime(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec * 1e-6;
}

int
main(int argc, char *argv[])
{
  uint8_t * sbuf[MAX_CONCURRENT], *rbuf[MAX_CONCURRENT];
  int a, sz, i, c, p;
  mx_segment_t sseg, rseg;
  mx_endpoint_t ep;
  uint32_t res;
  mx_status_t status;
  mx_endpoint_addr_t addr;
  mx_request_t snd[MAX_CONCURRENT], rcv[MAX_CONCURRENT];
  int iter;
  double last_status;
  uint32_t eid;
  uint64_t nic_id;
  char nic_name[MX_MAX_STR_LEN];

  unsigned errors = 0;
  uint64_t bytes = 0;
  uint64_t total_errors = 0;
  uint64_t total_msgs = 0;
  int first_align = 0;
  int last_align = 128;
  int first_sz = 0;
  int max_sz = MAX_SIZE;
  int inc = 1;
  int random = 0;
  double mult = 0.0;
  double stat_interval = 1.0;
  int rndv = 0;
  int verbose = 0;
  int keep = 0;
  int deterministic = 0;
  int parallel = 1;
  int net = 1;
  int timeout = 4000;
  int nb_iterations = 1;
  int unexpected = 0;
  int checking = 1;
  int board_id = MX_ANY_NIC;

  while ((c = getopt(argc, argv, "b:S:E:a:A:I:M:P:i:t:N:skdvnmufR")) != -1) {
    if (c == 'S')
      first_sz = strtoul(optarg, NULL, 0);
    else if (c == 'E')
      max_sz = strtoul(optarg, NULL, 0);
    else if (c == 'b')
      board_id = strtoul(optarg, NULL, 0);
    else if (c == 'a')
      first_align = strtoul(optarg, NULL, 0);
    else if (c == 'A')
      last_align = strtoul(optarg, NULL, 0);
    else if (c == 'N')
      nb_iterations = strtoul(optarg, NULL, 0);
    else if (c == 'i')
      stat_interval = strtoul(optarg, NULL, 0);
    else if (c == 't')
      timeout = strtoul(optarg, NULL, 0);
    else if (c == 'I')
      inc = strtoul(optarg, NULL, 0);
    else if (c == 'M')
      mult = strtod(optarg, NULL);
    else if (c == 'R')
      random = 1;
    else if (c == 'f')
      checking = 0;
    else if (c == 'P') {
      parallel = strtoul(optarg, NULL, 0);
      if (parallel > MAX_CONCURRENT) {
	fprintf(stderr, "The number of messages in parallel is limited to %d\n", MAX_CONCURRENT);
	parallel = MAX_CONCURRENT;
      }
    }
    else if (c == 's')
      rndv = 1;
    else if (c == 'k')
      keep = 1;
    else if (c == 'n')
      net = 1;
    else if (c == 'm')
      net = 0;
    else if (c == 'u')
      unexpected = 1;
    else if (c == 'd')
      deterministic = 1;
    else if (c == 'v')
      verbose += 1;
    else
      usage();
  }
  if (last_align >= 4096)
    last_align = 4096;
  for (p=0 ; p < parallel; p++) {
    sbuf[p] = malloc(max_sz + PAD_LEN + 4095);
    sbuf[p] += (-(size_t)sbuf[p]) & 4095;
    assert(((size_t)sbuf[p] & 4095) == 0);
    rbuf[p] = malloc(max_sz + PAD_LEN + 4095);
    rbuf[p] += (-(size_t)rbuf[p]) & 4095;
    assert(((size_t)rbuf[p] & 4095) == 0);
  }
  if (net) {
    putenv("MX_DISABLE_SHMEM=1");
    putenv("MX_DISABLE_SELF=1");  
  }
  fprintf(stderr, "Running mx_msg_loop: -N %d -S %d -E %d -a %d -A %d -I %d -M %.2f -t %d %s %s %s\n",
	  nb_iterations, first_sz, max_sz, first_align, last_align, inc, mult, timeout,
	  unexpected ? "-u" : "", net ? "-n" : "-m", rndv ? "-s" : "" );
  mx_init();
  mx_open_endpoint(board_id, MX_ANY_ENDPOINT, 0xabcde, NULL, 0, &ep);
  mx_get_endpoint_addr(ep, &addr);
  mx_decompose_endpoint_addr(addr, &nic_id, &eid);
  mx_nic_id_to_hostname(nic_id, nic_name);
  fprintf(stderr,"Using %s endpoint %d\n", nic_name, eid);

  last_status = get_dtime();
  for (a=first_align;a < last_align;a++) {
    
    sz = first_sz;
    while (sz < max_sz) {
      for (iter = 0; iter < nb_iterations;iter++) {
	uint32_t base[MAX_CONCURRENT];
	
	for (p=0; p < parallel; p++) {
	  if (checking) {
	    memset(sbuf[p], PAD_SBUF_AROUND, max_sz+PAD_LEN);
	    memset(rbuf[p], PAD_RBUF_AROUND, max_sz+PAD_LEN);
	    for (i=0;i<sz;i++)
	      rbuf[p][a+i] = PAD_INITIAL_VAL;
	    
	    if (deterministic)
	      base[p] = 0;
	    else
	      base[p] = rand();
	    
	    for (i=0; i < sz;i++) {
	      uint32_t val = base[p] + i;
	      sbuf[p][a+i] = 
		(i % 4 == 0) ? val >> 24:
		(i % 4 == 1) ? val >> 16 :
		(i % 4 == 2) ? val >> 8 :
		val;
	    }
	  }
	  if (unexpected == 0) {
	    rseg.segment_ptr = rbuf[p] + a;
	    rseg.segment_length = sz;
	    mx_irecv(ep, &rseg, 1, 0x12 + p, MX_MATCH_MASK_NONE, NULL, &rcv[p]);
	  }
	}
	
	for (p=0; p < parallel; p++) {
	  sseg.segment_ptr = sbuf[p] + a;
	  sseg.segment_length = sz;
	  if (rndv)
	    mx_issend(ep, &sseg, 1, addr, 0x12 + p, NULL, &snd[p]);
	  else
	    mx_isend(ep, &sseg, 1, addr, 0x12 + p, NULL, &snd[p]);
	}
	for (p=0; p < parallel; p++) {
	  if (unexpected) {
	    mx_probe(ep, timeout, 0x12 + p, MX_MATCH_MASK_NONE, &status, &res);
	  } else {
	    mx_wait(ep, &rcv[p], timeout, &status, &res);
	  }
	  if (!res) {
	    fprintf(stderr, "Timeout waiting for rcv (sz=%d,align=%d)\n", sz, a);
	    total_errors += 1;
	    goto end;
	  }
	  if (unexpected) {
	    rseg.segment_ptr = rbuf[p] + a;
	    rseg.segment_length = sz;
	    mx_irecv(ep, &rseg, 1, 0x12 + p, MX_MATCH_MASK_NONE, NULL, &rcv[p]);
	    mx_wait(ep, &rcv[p], timeout, &status, &res);
	    if (!res) {
	      fprintf(stderr, "Timeout waiting for rcv after probe (sz=%d,align=%d)\n", sz, a);
	      total_errors += 1;
	      goto end;
	    }
	  }
	  assert(status.xfer_length == sz);
	}
	for (p=0; p < parallel; p++) {
	  mx_wait(ep, &snd[p], timeout, &status, &res);
	  if (!res) {
	    fprintf(stderr, "Timeout waiting for snd (sz=%d,align=%d)\n", sz, a);
	    total_errors += 1;
	    goto end;
	  }
	  assert(status.xfer_length == sz);
          if (checking) {
	    for (i=0;i<a;i++) {
	      assert(sbuf[p][i] == PAD_SBUF_AROUND);
	      assert(rbuf[p][i] == PAD_RBUF_AROUND);
	    }
	    errors = 0;
	    bytes += sz;
	    for (i=0;i<sz;i++) {
	      uint32_t val = base[p] + i;
	      uint8_t expect = 
		(i % 4 == 0) ? val >> 24:
		(i % 4 == 1) ? val >> 16 :
		(i % 4 == 2) ? val >> 8 :
		val;
	      int error = (rbuf[p][i+a] != expect);
	      if (verbose && error) {
		fprintf(stderr,"Byte error for iter = %d sz=%d, a=%d, off=%d, recv=0x%02x, expect=0x%02x\n", 
			iter, sz, a, i , (uint8_t)rbuf[p][i+a], (uint8_t)expect);
	    }
	      errors += error;
	      assert(sbuf[p][i+a] == expect);
	    }
	  }
	  total_errors += errors;
	  if (errors) {
	    fprintf(stderr,"Errors in message iter=%d sz=%d, a=%d, erroneous bytes=%d/%d\n", 
		    iter, sz, a, errors, sz);
	    if (!keep)
	      goto end;
	  }
	  if (checking) {
	    for (i=a+sz;i<max_sz+PAD_LEN;i++) {
	      assert(sbuf[p][i] == PAD_SBUF_AROUND);
	      assert(rbuf[p][i] == PAD_RBUF_AROUND);
	    }
	  }
	  total_msgs += 1;
	}
	if (!stat_interval || get_dtime() - last_status >= stat_interval) {
	  fprintf(stderr, "Current(iter=%d,a=%d,sz=%d), byte-errors=%"PRId64"B/%"PRId64"KB, Msgs-nb=%"PRId64", \n",
		  iter, a, sz, total_errors, bytes >> 10, total_msgs);
	  last_status = get_dtime();
	}
      }
      
      if (random) {
	sz = first_sz + (int) ((max_sz - first_sz) * (rand() / (RAND_MAX + 1.0)));
      } else if (mult) {
	if (((int) (sz * mult)) > sz) {
	  sz = (int) (sz * mult);
	} else {
	  sz++;
	}
      } else {
	sz += inc;
      }
    }
  }
 end:
  fprintf(stderr, "Total errors=%"PRId64"B/%"PRId64"KB, Total msgs=%"PRId64", \n",
	  total_errors, bytes >> 10, total_msgs);
  mx_close_endpoint(ep);
  return total_errors ? 1 : 0;
}
