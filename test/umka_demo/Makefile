CC = gcc
CFLAGS = -Wall -Wextra -g -I../../vendor/umka/src
LDFLAGS = -L../../vendor/umka/build -lumka -lm

all: demo demo2 demo3 demo4 demo5 demo6

demo: demo.c
	$(CC) $(CFLAGS) -o demo demo.c $(LDFLAGS)

demo2: demo2.c
	$(CC) $(CFLAGS) -o demo2 demo2.c $(LDFLAGS)

demo3: demo3.c
	$(CC) $(CFLAGS) -o demo3 demo3.c $(LDFLAGS)

demo4: demo4.c
	$(CC) $(CFLAGS) -o demo4 demo4.c $(LDFLAGS)

demo5: demo5.c
	$(CC) $(CFLAGS) -o demo5 demo5.c $(LDFLAGS)

demo6: demo6.c
	$(CC) $(CFLAGS) -o demo6 demo6.c $(LDFLAGS)

clean:
	rm -f demo demo2 demo3 demo4 demo5 demo6

.PHONY: all clean
