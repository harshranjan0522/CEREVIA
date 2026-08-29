# CEREVIA — convenience wrapper around ./cerevia
#
#   make            build everything
#   make run        build and start the whole stack
#   make test       run the end-to-end suite
#   make clean      remove build output
#
# ./cerevia is the real entry point; these targets just forward to it.

.PHONY: all run start stop status logs test doctor clean reset

all: build

build:
	@./cerevia build

run:
	@./cerevia

start:
	@./cerevia start

stop:
	@./cerevia stop

status:
	@./cerevia status

logs:
	@./cerevia logs

test:
	@./cerevia test

doctor:
	@./cerevia doctor

reset:
	@./cerevia reset

clean:
	@$(MAKE) -C backend clean
	@rm -rf backend/build .run
	@find . -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true
	@echo "Cleaned."
