.PHONY: all configure install clean test

all: configure
	@$(MAKE) -C src all

clean:
	@$(MAKE) -C src -f Makefile.clean clean
	rm -f config.mk config.mk.tmp

test:
	@$(MAKE) -C src test

install: configure
	@$(MAKE) -C src install
	@$(MAKE) -C examples install-examples

configure: config.mk

config.mk: configure.mk
	@$(MAKE) -s -f configure.mk > config.mk.tmp
	@mv config.mk.tmp config.mk

