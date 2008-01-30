# Makefile including this file have to define
# DEPS = list of dependency files (.d)
# DEPSRCDIR = path of the corresponding source dir

# output dependency file in .d while building .c -o .o
BUILDDEPFLAGS	=	-MMD
# output dependency file in .d for .c
ONLYDEPFLAGS	=	-MMD -MM

# create a dependency file when it doesn't exist (called by the include line below)
ifeq ($(BUILD_O_AND_LO_OBJECTS),1)
$(DEPS): %.d: $(DEPSRCDIR)/%.c
	@echo "Generating dependency file for $(subst .d,.c,$@)"
	@$(CC) $(CPPFLAGS) $(ONLYDEPFLAGS) $(DEPSRCDIR)/$(subst .d,.c,$@) -MT $(subst .d,.o,$@) -MT $(subst .d,.lo,$@)
else
$(DEPS): %.d: $(DEPSRCDIR)/%.c
	@echo "Generating dependency file for $(subst .d,.c,$@)"
	@$(CC) $(CPPFLAGS) $(ONLYDEPFLAGS) $(DEPSRCDIR)/$(subst .d,.c,$@)
endif

# include dependencies, except when cleaning (so that we don't re-create for nothing)
ifeq ($(filter ${MAKECMDGOALS},clean distclean),)
-include $(DEPS)
endif
