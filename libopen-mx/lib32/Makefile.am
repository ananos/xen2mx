ARCHSIZE	= 32
AM_CFLAGS_SPEC  = -O2
AM_LDFLAGS_SPEC =

if OMX_MULTILIB
  AM_CFLAGS_SPEC  += -m32
  AM_LDFLAGS_SPEC += -m32
endif

LIBIDIR_SUFFIX    =

include ../Makefile-generic.am

