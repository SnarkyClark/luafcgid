#include <string.h>
#include "main.h"

#define _isdigit(c) ((c >= '0') && (c <= '9'))
#define _isalpha_l(c) ((c >= 'a') && (c <= 'z'))
#define _isalpha_u(c) ((c >= 'A') && (c <= 'Z'))
#define _isalpha(c) (_isalpha_l(c) || _isalpha_u(c))
#define _issafe(c) (_isdigit(c) || _isalpha(c))
#define _ishex(c) (_isdigit(c) || \
    ((c >= 'a') && (c <= 'f')) || \
    ((c >= 'A') && (c <= 'F')))

char* _strtok(char** s, char c) {
    char* t = NULL;
    if (*s && (**s != '\0')) {
        t = *s;
        char* e = strchr(*s, c);
        if (e) {
            *e = '\0';
            *s = ++e;
        } else {
            *s = NULL;
        }
    }
    return t;
}


char* parser_urldecode(char *s) {
	char* r = NULL;
	char* d = NULL;
	char c = ' ';
	if (s) {
		r = malloc(strlen(s) + 1);
        c = *s;
        d = r;
		while(c != '\0') {
			if ((c == '%') && (_ishex(*(s + 1)) && _ishex(*(s + 2)))) {
				*d = '\0';
                c = *(++s);
				if (_isdigit(c)) *d += (c - '0') * 0x10;
				if (_isalpha_l(c)) *d += (c - 'a' + 10) * 0x10;
				if (_isalpha_u(c)) *d += (c - 'A' + 10) * 0x10;
				c = *(++s);
				if (_isdigit(c)) *d += c - '0';
				if (_isalpha_l(c)) *d += c - 'a' + 10;
				if (_isalpha_u(c)) *d += c - 'A' + 10;
				d++;
			} else if (c == '+') {
				*d++ = ' ';
			} else {
				*d++ = *s;
			}
			c = *(++s);
		}
		*d = '\0';
	}
	return r;
}

void parser_decode_pair(char* s, char** n, char** v) {
	// decodes a "name=value" string
	char* t;
	*n = NULL;
	*v = NULL;
	// search for equals sign
	t = strchr(s, '=');
	// do we have an equals sign AND is there anything after it?
	if (t && (*(t + 1) != '\0')) {
		// convert the equals sign into a null terminator
		// and set 't' to point to value
		*t++ = '\0';
		// decode any URL entities
		*n = parser_urldecode(s);
		*v = parser_urldecode(t);
	}
}

void parser_decode(lua_State* L, const char* s) {
	char* t;	// pointer to token separator
	char* l;
	char* n;
	char* v;

	// create a local scratch copy
	char* buf = (char*)malloc(strlen(s) + 1);
	strcpy(buf, s);
    l = buf;

	t = _strtok(&l, '&');
	while (t) {
		// decode "name=value" pair
		parser_decode_pair(t, &n, &v);
		if (n) {
			// load it into a Lua table field
			if (v) lua_pushstring(L, v);
			else lua_pushnil(L);
			lua_setfield(L, -2, n);
			free(n);
		}
		// cleanup
		if (v) free(v);
		// Find the next token
		t = _strtok(&l, '&');
	}

	free(buf);
}


