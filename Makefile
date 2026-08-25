TUTORS := bututor gitutor nvimtutor zshtutor

# A tutor name passed alongside an action narrows that action to the tutor.
# Examples: `make build gitutor`, `make install bututor`.
SELECTED := $(filter $(TUTORS),$(MAKECMDGOALS))
ACTIVE   := $(if $(SELECTED),$(SELECTED),$(TUTORS))
ACTION   := $(filter all build install clean,$(MAKECMDGOALS))

all: build

build: $(addprefix build-,$(ACTIVE))

install: $(addprefix install-,$(ACTIVE))

clean: $(addprefix clean-,$(ACTIVE))

ifeq ($(ACTION),)
$(TUTORS): %: build-%
else
$(TUTORS):
endif

build-%:
	$(MAKE) -C $* build

install-%:
	$(MAKE) -C $* install

clean-%:
	$(MAKE) -C $* clean

help:
	@echo "Usage:"
	@echo "  make all                Build all tutors"
	@echo "  make build [tutor]      Build all tutors or one tutor"
	@echo "  make install [tutor]    Install all tutors or one tutor"
	@echo "  make clean [tutor]      Clean all tutors or one tutor"
	@echo "  make <tutor>            Build one tutor"
	@echo ""
	@echo "Tutors: $(TUTORS)"

.PHONY: all build install clean help $(TUTORS) FORCE

build-% install-% clean-%: FORCE

FORCE:
