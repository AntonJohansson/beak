ifeq (${PREFIX},)
	PREFIX := /usr/local
endif

beak: beak.c
	${CC} $^ -o $@ -g -std=c99 -O3 -lm -lraylib -lpthread -Wall -Wextra

install: beak
	install -d ${PREFIX}/bin/
	install -m 755 beak ${PREFIX}/bin/
