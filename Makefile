CC       := gcc
CFLAGS   := -Wall -Wextra -g
VALGRIND := valgrind
VFLAGS   := -s --leak-check=full --show-leak-kinds=all --track-origins=yes

# 共通ソース
COMMON_SRCS := radix.c fib.c route_entry.c test.c ptree.c queue.c
COMMON_OBJS := $(COMMON_SRCS:.c=.o)

# プログラム main
PROGS     := main
SRCS_main := main.c $(COMMON_SRCS)
OBJS_main := $(SRCS_main:.c=.o)

# ビルドタイプ (release or debug)
BUILDTYPE ?= release

# release / debug で CFLAGS 切替
ifeq ($(BUILDTYPE),debug)
    CFLAGS += -DDEBUG -O0
else
    CFLAGS += -O2
endif

all: $(PROGS)

main: $(OBJS_main)
	$(CC) $(CFLAGS) -o $@ $^ -lresolv

# デバッグビルド
debug:
	$(MAKE) BUILDTYPE=debug

# valgrind 実行
main-valgrind: main
	$(VALGRIND) $(VFLAGS) ./main tests/rib.simple.0000.ipv4.txt tests/lookup.address.ipv4.txt

# .c → .o のルール
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(PROGS) *.o

.PHONY: all clean main-valgrind debug
