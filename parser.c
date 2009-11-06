parser_urldecode(char *s) {
	char* r = NULL;
	if (s) {
		r = malloc(strlen(s) + 1);
		while(*s != '\0') {
			if ((*s == '%') && (*(s + 1) != '\0') && (*(s + 2) != '\0')) {
				*r = isdigit(*s) ? *s - '0' : ((tolower(*s) - 'a')) + 10;
				s++;
				*r += (isdigit(*s) ? *s - '0' : ((tolower(*s) - 'a')) + 10) * 16;
				r++;
			} else if (*s == '+') {
				*r++ = ' ';
				s++;
			} else {
				*r++ = *s++;
			}
		}
	}
	return r;
}

parser_decode_pair(char* s, char** n, char** v) {
	// decodes a "name=value" string
	char* t;

	// search for equals sign
	t = strchr(s, '=');
	// do we have an equals sign AND is there anything after it?
	if (t && (t < (s + strlen(s) - 1))) {
		// convert the equals sign into a null terminator
		// and set 't' to point to value
		*t++ = '\0';
		// decode any URL entities
		*n = parser_urldecode(s);
		*v = parser_urldecode(t);
	}
}
parser_decode(lua_State* L, char* s) {
	char* t;	// pointer to token separator
	char* l;
	char* n;
	char* v;

	// create a local scratch copy
	char* buf = (char*)malloc(strlen(s));
	strcpy(buf, s);

	t = strtok_r(buf, "&", &l);
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
		t = strtok_r(NULL, "&", &l);
	}
}


