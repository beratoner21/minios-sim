CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pthread

minios: src/main.c
	$(CC) $(CFLAGS) src/main.c -o minios

.PHONY: test
test: minios
	python3 tests/run.py
