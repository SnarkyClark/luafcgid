#include "main.h"

config_t* config_load(const char* fn) {
	int rc;
	struct stat fs;
	char* fbuf = NULL;
	lua_State* L = NULL;

    config_t* cf = (config_t*)malloc(sizeof(config_t));
    memset(cf, 0, sizeof(config_t));

	// defaults
	cf->listen = (char*)malloc(strlen(LISTEN_PATH) + 1);
	strcpy(cf->listen, LISTEN_PATH);
    cf->workers = 3;
    cf->states = 5;
    cf->sweep = 1000;
    cf->retries = 1;
    cf->maxpost = 1024 * 1024;

    if (fn) fbuf = script_load(fn, &fs);
	if (fbuf) {
        // make a new state
        L = lua_open();
        if (!L) return NULL;
        luaL_openlibs(L);
		// load and run buffer
		rc = luaL_loadbuffer(L, fbuf, fs.st_size, fn);
		if (rc == STATUS_OK) rc = lua_pcall(L, 0, 0, 0);
		// cleanup
		free(fbuf);
		if (rc == STATUS_OK) {
			// transfer globals to config struct
			luaL_getglobal_str(L, "listen", &cf->listen);
			luaL_getglobal_int(L, "workers", &cf->workers);
			luaL_getglobal_int(L, "states", &cf->states);
			luaL_getglobal_int(L, "sweep", &cf->sweep);
			// TODO
		}
		lua_close(L);
	}
	return cf;
}
