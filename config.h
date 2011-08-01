#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

struct hook_struct {
	int count;
	char** chunk;
} typedef hook_t;

struct config_struct {
	char* listen;
	int workers;
	int states;
	int clones;
	int sweep;
	int watchdog;
	int retries;
	int maxpost;
	char* logfile;
	lua_State* L; /* global Lua state */
} typedef config_t;

config_t* config_load(const char* fn);

#endif // CONFIG_H_INCLUDED
