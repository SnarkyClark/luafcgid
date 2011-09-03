#ifndef BUFFER_H_
#define BUFFER_H_

#include <stdlib.h>

/* implement an incremental buffer */

struct buffer_struct {
	size_t size;
	size_t len;
	char* data;
} typedef buffer_t;

int buffer_alloc(buffer_t* buf, size_t size);
void buffer_free(buffer_t* buf);
int buffer_grow(buffer_t* buf, size_t size);
int buffer_shrink(buffer_t* buf, size_t size);
int buffer_add(buffer_t* buf, const char* s, int len);

#endif /* BUFFER_H_ */
