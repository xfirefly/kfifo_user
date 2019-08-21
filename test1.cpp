#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "kfifo.h"
 

#define FIFO_SIZE	1<<22

//#define	DYNAMIC
#ifdef DYNAMIC
static struct kfifo test[1];
#else
static DECLARE_KFIFO(test[1], unsigned char, FIFO_SIZE);
#endif

int main(void)
{
	unsigned char	buf[6];
	unsigned char	i;
	unsigned int	ret;
	unsigned int 	copied;

	printf("byte stream fifo test start\n");

#ifdef DYNAMIC
	if (kfifo_alloc(test, FIFO_SIZE, 0)) {
		printf("error kfifo_alloc\n");
		return 1;
	}
#else
	INIT_KFIFO(test[0]);
#endif

	printf("queue size: %u\n", kfifo_size(test));

	kfifo_in(test, "hello", 5);

	for(i = 0; i != 9; i++)
		kfifo_put(test, &i);

	printf("queue peek: %u\n", kfifo_peek_len(test));

	i = kfifo_out(test, buf, 5);
	printf("buf: %.*s\n", i, buf);

/* 	ret = kfifo_to_user(test, buf, sizeof(int), &copied);
	printf("ret: %d %u\n", ret, copied);
	ret = kfifo_from_user(test, buf, copied, &copied);
	printf("ret: %d %u\n", ret, copied); */

	ret = kfifo_out(test, buf, 2);
	printf("ret: %d\n", ret);
	ret = kfifo_in(test, buf, ret);
	printf("ret: %d\n", ret);

	printf("queue len: %u\n", kfifo_len(test));

	for(i = 20; kfifo_put(test, &i); i++)
		;

	while(kfifo_get(test, &i))
		printf("%d ", i);
	printf("\n");

	return 0;
}
