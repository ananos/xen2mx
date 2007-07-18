# Makefile including this file have to define
# DEPS = list of dependency files (.d)
# DEPSRCDIR = path of the corresponding source dir

# output dependency file in .d while building .c -o .o
BUILDDEPFLAGS	=	-MMD
# output dependency file in .d for .c
ONLYDEPFLAGS	=	-MMD -MM

# create a dependency file when it doesn't exist (called by the include line below)
$(DEPS): %.d:
	$(CC) $(CPPFLAGS) $(ONLYDEPFLAGS) $(DEPSRCDIR)/$(shell basename $@ .d).c

# include dependencies, except when cleaning (so that we don't re-create for nothing)
ifeq ($(filter ${MAKECMDGOALS},clean distclean),)
include $(DEPS)
endif
