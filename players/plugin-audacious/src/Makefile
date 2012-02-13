
SRC_OBJS	= audacious.o
SRC_DFILES	= Makefile $(SRC_OBJS:.o=.c)
SRC_PATH	= src

OBJS += $(addprefix $(SRC_PATH)/,$(SRC_OBJS))

dist-src:
	mkdir -p $(DIST)/$(SRC_PATH)
	cp -RPp $(addprefix $(SRC_PATH)/,$(SRC_DFILES)) $(DIST)/$(SRC_PATH)

$(SRC_PATH)/$(PLUGIN): $(LOBJS)
	echo $(LOBJS)
	@CMD='$(LD) -shared -o $@ $(LDFLAGS) $(LOBJS) $(LIBS)'; \
	if [ "$(V)" -gt 0 ]; then echo $$CMD; else echo LD $@ ; fi; \
	eval $$CMD

install-plugin: $(SRC_PATH)/$(PLUGIN)
	@echo Installing plugin in $(DESTDIR)$(BINDIR)
	@[ -d $(DESTDIR)$(BINDIR) ] || mkdir -p $(DESTDIR)$(BINDIR)
	@$(INSTALL_PROGRAM) $+ $(DESTDIR)$(BINDIR)

