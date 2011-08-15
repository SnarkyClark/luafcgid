#include "main.h"

config_t* config_load(const char* fn) {
	int rc;
	struct stat fs;
	char* fbuf = NULL;
    char errmsg[ERR_SIZE + 1];

    config_t* cf = (config_t*)malloc(sizeof(config_t));
    memset(cf, 0, sizeof(config_t));

	// defaults
	cf->listen = (char*)malloc(strlen(LISTEN_PATH) + 1);
	strcpy(cf->listen, LISTEN_PATH);
    cf->workers = 3;
    cf->states = 5;
    cf->clones = cf->states;
    cf->sweep = 1000;
    cf->watchdog = 60;
    cf->retries = 2;
    cf->maxpost = 1024 * 1024;
	cf->logfile = (char*)malloc(strlen(LOGFILE) + 1);
	strcpy(cf->logfile, LOGFILE);

    if (fn) fbuf = script_load(fn, &fs);
	if (fbuf) {
        // make a new state
        cf->L = lua_open();
        if (!cf->L) return NULL;
        luaL_openlibs(cf->L);
		// load and run buffer
		rc = luaL_loadbuffer(cf->L, fbuf, fs.st_size, fn);
		if (rc == STATUS_OK) rc = lua_pcall(cf->L, 0, 0, 0);
		// cleanup
		free(fbuf);
		if (rc == STATUS_OK) {
			// transfer globals to config struct
			luaL_getglobal_str(cf->L, "listen", &cf->listen);
			luaL_getglobal_int(cf->L, "workers", &cf->workers);
			luaL_getglobal_int(cf->L, "states", &cf->states);
			luaL_getglobal_int(cf->L, "clones", &cf->clones);
			luaL_getglobal_int(cf->L, "sweep", &cf->sweep);
			luaL_getglobal_int(cf->L, "watchdog", &cf->watchdog);
			luaL_getglobal_int(cf->L, "retries", &cf->retries);
			luaL_getglobal_int(cf->L, "maxpost", &cf->maxpost);
			luaL_getglobal_str(cf->L, "logfile", &cf->logfile);
			luaL_getglobal_str(cf->L, "logfile", &cf->logfile);
			luaL_getglobal_str(cf->L, "logfile", &cf->logfile);
		} else {
           	if (lua_isstring(cf->L, -1)) {
				// capture the error message
				strncpy(errmsg, lua_tostring(cf->L, -1), ERR_SIZE);
				errmsg[ERR_SIZE] = '\0';
				lua_pop(cf->L, 1);
			} else {
				errmsg[0] = '\0';
			}
			switch (rc) {
				case LUA_ERRSYNTAX:
					logit("[%d] %s: %s", 0, LUA_ERRSYNTAX_STR, errmsg);
					break;
				case LUA_ERRRUN:
					logit("[%d] %s", 0, LUA_ERRRUN_STR);
					break;
				case LUA_ERRMEM:
					logit("[%d] %s", 0, LUA_ERRMEM_STR);
					break;
				default:
					logit("[%d] %s", 0, ERRUNKNOWN_STR);
			}
		}
	}
	return cf;
}
