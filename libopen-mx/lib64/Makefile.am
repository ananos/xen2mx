ARCHSIZE	= 64
AM_CFLAGS_SPEC  = -O2
AM_LDFLAGS_SPEC = 

if OMX_MULTILIB
  AM_CFLAGS_SPEC  += -m64
  AM_LDFLAGS_SPEC += -m64
endif

LIBIDIR_SUFFIX    =

include ../Makefile-generic.am

