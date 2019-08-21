// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A generic kernel FIFO implementation
 *
 * Copyright (C) 2009/2010 Stefani Seibold <stefani@seibold.net>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "kfifo.h"

 

#define min(x,y) ({ 			\
		typeof(x) _x = (x);	\
		typeof(y) _y = (y);	\
		(void) (&_x == &_y);	\
		_x < _y ? _x : _y; })

#define roundup_pow_of_two(n)   \
    (1UL    <<                             \
        (                                  \
            (                              \
            (n) & (1UL << 31) ? 31 :       \
            (n) & (1UL << 30) ? 30 :       \
            (n) & (1UL << 29) ? 29 :       \
            (n) & (1UL << 28) ? 28 :       \
            (n) & (1UL << 27) ? 27 :       \
            (n) & (1UL << 26) ? 26 :       \
            (n) & (1UL << 25) ? 25 :       \
            (n) & (1UL << 24) ? 24 :       \
            (n) & (1UL << 23) ? 23 :       \
            (n) & (1UL << 22) ? 22 :       \
            (n) & (1UL << 21) ? 21 :       \
            (n) & (1UL << 20) ? 20 :       \
            (n) & (1UL << 19) ? 19 :       \
            (n) & (1UL << 18) ? 18 :       \
            (n) & (1UL << 17) ? 17 :       \
            (n) & (1UL << 16) ? 16 :       \
            (n) & (1UL << 15) ? 15 :       \
            (n) & (1UL << 14) ? 14 :       \
            (n) & (1UL << 13) ? 13 :       \
            (n) & (1UL << 12) ? 12 :       \
            (n) & (1UL << 11) ? 11 :       \
            (n) & (1UL << 10) ? 10 :       \
            (n) & (1UL <<  9) ?  9 :       \
            (n) & (1UL <<  8) ?  8 :       \
            (n) & (1UL <<  7) ?  7 :       \
            (n) & (1UL <<  6) ?  6 :       \
            (n) & (1UL <<  5) ?  5 :       \
            (n) & (1UL <<  4) ?  4 :       \
            (n) & (1UL <<  3) ?  3 :       \
            (n) & (1UL <<  2) ?  2 :       \
            (n) & (1UL <<  1) ?  1 :       \
            (n) & (1UL <<  0) ?  0 : -1    \
            ) + 1                          \
        )                                  \
)

/*
 * internal helper to calculate the unused elements in a fifo
 */
static inline unsigned int kfifo_unused(struct __kfifo *fifo)
{
	return (fifo->mask + 1) - (fifo->in - fifo->out);
}

int __kfifo_alloc(struct __kfifo *fifo, unsigned int size,
		size_t esize  /*gfp_t gfp_mask*/)
{
	/*
	 * round up to the next power of 2, since our 'let the indices
	 * wrap' technique works only in this case.
	 */
	size = roundup_pow_of_two(size);

	fifo->in = 0;
	fifo->out = 0;
	fifo->esize = esize;

	if (size < 2) {
		fifo->data = NULL;
		fifo->mask = 0;
		return -EINVAL;
	}

	fifo->data = malloc(esize * size );

	if (!fifo->data) {
		fifo->mask = 0;
		return -ENOMEM;
	}
	fifo->mask = size - 1;

	return 0;
}


void __kfifo_free(struct __kfifo *fifo)
{
	free(fifo->data);
	fifo->in = 0;
	fifo->out = 0;
	fifo->esize = 0;
	fifo->data = NULL;
	fifo->mask = 0;
}


int __kfifo_init(struct __kfifo *fifo, void *buffer,
		unsigned int size, size_t esize)
{
	size /= esize;

	size = roundup_pow_of_two(size);

	fifo->in = 0;
	fifo->out = 0;
	fifo->esize = esize;
	fifo->data = buffer;

	if (size < 2) {
		fifo->mask = 0;
		return -EINVAL;
	}
	fifo->mask = size - 1;

	return 0;
}


static void kfifo_copy_in(struct __kfifo *fifo, const void *src,
		unsigned int len, unsigned int off)
{
	unsigned int size = fifo->mask + 1;
	unsigned int esize = fifo->esize;
	unsigned int l;

	off &= fifo->mask;
	if (esize != 1) {
		off *= esize;
		size *= esize;
		len *= esize;
	}
	l = min(len, size - off);

	memcpy(fifo->data + off, src, l);
	memcpy(fifo->data, src + l, len - l);
	/*
	 * make sure that the data in the fifo is up to date before
	 * incrementing the fifo->in index counter
	 */
	smp_wmb();
}

unsigned int __kfifo_in(struct __kfifo *fifo,
		const void *buf, unsigned int len)
{
	unsigned int l;

	l = kfifo_unused(fifo);
	if (len > l)
		len = l;

	kfifo_copy_in(fifo, buf, len, fifo->in);
	fifo->in += len;
	return len;
}


static void kfifo_copy_out(struct __kfifo *fifo, void *dst,
		unsigned int len, unsigned int off)
{
	unsigned int size = fifo->mask + 1;
	unsigned int esize = fifo->esize;
	unsigned int l;

	off &= fifo->mask;
	if (esize != 1) {
		off *= esize;
		size *= esize;
		len *= esize;
	}
	l = min(len, size - off);

	memcpy(dst, fifo->data + off, l);
	memcpy(dst + l, fifo->data, len - l);
	/*
	 * make sure that the data is copied before
	 * incrementing the fifo->out index counter
	 */
	smp_wmb();
}

unsigned int __kfifo_out_peek(struct __kfifo *fifo,
		void *buf, unsigned int len)
{
	unsigned int l;

	l = fifo->in - fifo->out;
	if (len > l)
		len = l;

	kfifo_copy_out(fifo, buf, len, fifo->out);
	return len;
}


unsigned int __kfifo_out(struct __kfifo *fifo,
		void *buf, unsigned int len)
{
	len = __kfifo_out_peek(fifo, buf, len);
	fifo->out += len;
	return len;
}


unsigned int __kfifo_max_r(unsigned int len, size_t recsize)
{
	unsigned int max = (1 << (recsize << 3)) - 1;

	if (len > max)
		return max;
	return len;
}


#define	__KFIFO_PEEK(data, out, mask) \
	((data)[(out) & (mask)])
/*
 * __kfifo_peek_n internal helper function for determinate the length of
 * the next record in the fifo
 */
static unsigned int __kfifo_peek_n(struct __kfifo *fifo, size_t recsize)
{
	unsigned int l;
	unsigned int mask = fifo->mask;
	unsigned char *data = fifo->data;

	l = __KFIFO_PEEK(data, fifo->out, mask);

	if (--recsize)
		l |= __KFIFO_PEEK(data, fifo->out + 1, mask) << 8;

	return l;
}

#define	__KFIFO_POKE(data, in, mask, val) \
	( \
	(data)[(in) & (mask)] = (unsigned char)(val) \
	)

/*
 * __kfifo_poke_n internal helper function for storeing the length of
 * the record into the fifo
 */
static void __kfifo_poke_n(struct __kfifo *fifo, unsigned int n, size_t recsize)
{
	unsigned int mask = fifo->mask;
	unsigned char *data = fifo->data;

	__KFIFO_POKE(data, fifo->in, mask, n);

	if (recsize > 1)
		__KFIFO_POKE(data, fifo->in + 1, mask, n >> 8);
}

unsigned int __kfifo_len_r(struct __kfifo *fifo, size_t recsize)
{
	return __kfifo_peek_n(fifo, recsize);
}


unsigned int __kfifo_in_r(struct __kfifo *fifo, const void *buf,
		unsigned int len, size_t recsize)
{
	if (len + recsize > kfifo_unused(fifo))
		return 0;

	__kfifo_poke_n(fifo, len, recsize);

	kfifo_copy_in(fifo, buf, len, fifo->in + recsize);
	fifo->in += len + recsize;
	return len;
}


static unsigned int kfifo_out_copy_r(struct __kfifo *fifo,
	void *buf, unsigned int len, size_t recsize, unsigned int *n)
{
	*n = __kfifo_peek_n(fifo, recsize);

	if (len > *n)
		len = *n;

	kfifo_copy_out(fifo, buf, len, fifo->out + recsize);
	return len;
}

unsigned int __kfifo_out_peek_r(struct __kfifo *fifo, void *buf,
		unsigned int len, size_t recsize)
{
	unsigned int n;

	if (fifo->in == fifo->out)
		return 0;

	return kfifo_out_copy_r(fifo, buf, len, recsize, &n);
}


unsigned int __kfifo_out_r(struct __kfifo *fifo, void *buf,
		unsigned int len, size_t recsize)
{
	unsigned int n;

	if (fifo->in == fifo->out)
		return 0;

	len = kfifo_out_copy_r(fifo, buf, len, recsize, &n);
	fifo->out += n + recsize;
	return len;
}


void __kfifo_skip_r(struct __kfifo *fifo, size_t recsize)
{
	unsigned int n;

	n = __kfifo_peek_n(fifo, recsize);
	fifo->out += n + recsize;
}



