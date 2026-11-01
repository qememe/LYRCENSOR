CC ?= cc
PKGS = ncursesw libcurl jansson libutf8proc
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Isrc $(shell pkg-config --cflags $(PKGS))
CFLAGS ?= -O2 -g
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Wformat=2 -fstack-protector-strong -D_FORTIFY_SOURCE=2
LDFLAGS += -Wl,-z,relro,-z,now
LDLIBS += $(shell pkg-config --libs $(PKGS)) -pthread
PREFIX ?= /usr/local
CORE = src/common.c src/unicode.c src/editor.c src/tokenizer.c src/censor.c src/config.c src/response.c src/openrouter.c src/classifier.c src/output.c src/diagnostics.c
OBJ = $(CORE:.c=.o) src/tui.o src/main.o
.PHONY: all clean test install uninstall
all: lyrcensor
lyrcensor: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)
$(OBJ): $(wildcard src/*.h)
tests/test_core: tests/test_core.c $(CORE) $(wildcard src/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_core.c $(CORE) $(LDLIBS)
tests/lyrcensor_fixture: $(CORE) src/tui.c src/main.c $(wildcard src/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -DLY_TEST_HTTP -o $@ $(CORE) src/tui.c src/main.c $(LDLIBS)
tests/test_http: tests/test_http.c tests/support.c tests/support.h $(CORE) $(wildcard src/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -DLY_TEST_HTTP -Itests -o $@ tests/test_http.c tests/support.c $(CORE) $(LDLIBS)
tests/test_diagnostics: tests/test_diagnostics.c $(CORE) $(wildcard src/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_diagnostics.c $(CORE) $(LDLIBS) -lm
tests/test_requests: tests/test_requests.c $(CORE) $(wildcard src/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_requests.c $(CORE) $(LDLIBS)
tests/test_terminal: tests/test_terminal.c tests/support.c tests/support.h lyrcensor tests/lyrcensor_fixture
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Itests -o $@ tests/test_terminal.c tests/support.c -lutil -ljansson
test: tests/test_core tests/test_diagnostics tests/test_requests tests/test_http tests/test_terminal
	./tests/test_core
	./tests/test_diagnostics
	./tests/test_requests
	./tests/test_http
	./tests/test_terminal
install: lyrcensor
	install -Dm755 lyrcensor $(DESTDIR)$(PREFIX)/bin/lyrcensor
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/lyrcensor
clean:
	rm -f $(OBJ) lyrcensor tests/test_core tests/test_diagnostics tests/test_requests tests/test_http tests/test_terminal tests/lyrcensor_fixture
