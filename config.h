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
	BOOL showerrors;
	BOOL buffering;
	int headersize;
	int bodysize;
	char* handler;
	char* httpstatus;
	char* contenttype;
	int maxpost;
	int maxcount;
	char* logfile;
	lua_State* L; /* global Lua state */
} typedef config_t;

config_t* config_load(const char* fn);
void config_free(config_t* conf);

#endif // CONFIG_H_INCLUDED
