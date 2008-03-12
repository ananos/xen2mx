# Makefile including this file have to define
# DEPS = list of dependency files (.d)
# DEPSRCDIR = path of the corresponding source dir

# output dependency file in .d while building .c -o .o
BUILDDEPFLAGS	=	-MMD
# output dependency file in .d for .c
ONLYDEPFLAGS	=	-MMD -MM

ifeq ($(REL_DEPDIR),)
	REL_DEPDIR	=	$(REL_DIR)
endif

# create a dependency file when it doesn't exist (called by the include line below)
ifeq ($(BUILD_O_AND_LO_OBJECTS),1)
$(DEPS): $(DEPDIR)%.d: $(DEPSRCDIR)%.c
	$(CMDECHO) "  DEP     $(REL_DEPDIR)$(notdir $@)"
	$(CMDPREFIX) $(CC) $(CPPFLAGS) $(ONLYDEPFLAGS) $(patsubst $(DEPDIR)%.d,$(DEPSRCDIR)%.c,$@) -MT $(patsubst $(DEPDIR)%.d,%.o,$@) -MT $(patsubst $(DEPDIR)%.d,.lo,$@)
else
$(DEPS): $(DEPDIR)%.d: $(DEPSRCDIR)%.c
	$(CMDECHO) "  DEP     $(REL_DEPDIR)$(notdir $@)"
	$(CMDPREFIX) $(CC) $(CPPFLAGS) $(ONLYDEPFLAGS) $(patsubst $(DEPDIR)%.d,$(DEPSRCDIR)%.c,$@)
endif

# include dependencies, except when cleaning (so that we don't re-create for nothing)
ifeq ($(filter ${MAKECMDGOALS},clean distclean),)
-include $(DEPS)
endif
