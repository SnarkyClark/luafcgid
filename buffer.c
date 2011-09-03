#include <string.h>
#include "buffer.h"

int buffer_alloc(buffer_t* buf, size_t size) {
	buf->len = 0;
	buf->size = size;
	if (buf->size > 0) {
		buf->data = (char*)malloc(size);
		if (buf->data) return 1;
	}
	return 0;
}

void buffer_free(buffer_t* buf) {
	if (buf->data) free(buf->data);
}

/* grow buffer by at least 'size' bytes by doubling it's size */
int buffer_grow(buffer_t* buf, size_t size) {
	void* rdata = NULL;
	size_t rsize = buf->size;
	if (buf->data && (buf->size > 0)) {
		while (rsize < (buf->size + size)) rsize = rsize * 2;
		rdata = realloc(buf->data, rsize);
		if (rdata) {
			buf->data = (char*)rdata;
			buf->size = rsize;
			return 1;
		}
	}
	return 0;
}

/* shrink buffer back down to 'size' */
int buffer_shrink(buffer_t* buf, size_t size) {
	void* rdata = NULL;
	if (buf->data && (size > 0)) {
		if (buf->size >= size) return 1;
		rdata = realloc(buf->data, size);
		if (rdata) {
			buf->data = (char*)rdata;
			buf->size = size;
			if (buf->len > size) buf->len = size;
			return 1;
		}
	}
	return 0;
}

/* add data to buffer, return bytes added */
int buffer_add(buffer_t* buf, const char* s, int len) {
	if (len < 0) len = strlen(s);
	if ((len > 0) && buf->data && (buf->size > 0)) {
		if ((buf->len + len) > buf->size) {
			if (!buffer_grow(buf, len)) return 0;
		}
		memcpy(buf->data + buf->len, s, len);
		buf->len += len;
		return len;
	}
	return 0;
}

