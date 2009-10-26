cgi_decode(char* s) {
	char* t;	// pointer to token separator
	char* l;
	char* n;
	char* v;
	
	// create a local scratch copy
	char* buf = (char*)malloc(strlen(s));
	strcpy(buf, s);

	t = strtok_r(buf, "&", &l);
	while (t) {       
		pair_decode(t, n, v);			// decode "name=value" pair
		// TODO: load it into a Lua table
		t = strtok_r(NULL, "&", &l);	// Find the next token
	}
}

pair_decode(char* s, char** n, char** v) {
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
		*n = url_decode(s);
		*v = url_decode(t);
	}
}

