/*
 * luafcgid -- A simple multi-threaded Lua+FastCGI daemon.
 *
 * this code is provided under the "Simplified BSD License"
 * (c) STPeters 2009
 */

#include "main.h"

const char* env_var[] = {
		// standard CGI environment variables as per CGI Specification 1.1
		"SERVER_SOFTWARE", "SERVER_NAME", "GATEWAY_INTERFACE",
		"SERVER_PROTOCOL", "SERVER_PORT", "REQUEST_METHOD", "PATH_INFO",
		"PATH_TRANSLATED", "SCRIPT_NAME", "QUERY_STRING", "REMOTE_HOST",
		"REMOTE_ADDR", "AUTH_TYPE", "REMOTE_USER", "REMOTE_IDENT",
		"CONTENT_TYPE", "CONTENT_LENGTH", // WARNING! do NOT rely on this value
		// common client variables
		"HTTP_ACCEPT", "HTTP_USER_AGENT",
		// not spec, but required for this to work
		"SCRIPT_FILENAME", NULL, };

// utility functions

BOOL luaL_getglobal_int(lua_State* L, const char* name, int* v) {
	lua_getglobal(L, name);
	if (lua_isnumber(L, -1)) {
		*v = lua_tointeger(L, -1);
		lua_pop(L, 1);
		return TRUE;
	}
	lua_pop(L, 1);
	return FALSE;
}

BOOL luaL_getglobal_str(lua_State* L, const char* name, char** v) {
	const char* r = NULL;
	size_t l = 0;
	lua_getglobal(L, name);
	if (lua_isstring(L, -1)) {
		r = lua_tolstring(L, -1, &l);
		if (r && l) {
			if (*v)
				free(*v);
			*v = (char*) malloc(l + 1);
			strncpy(*v, r, l);
			(*v)[l] = '\0';
			lua_pop(L, 1);
			return TRUE;
		}
	}
	lua_pop(L, 1);
	return FALSE;
}

void luaL_pushcgienv(lua_State* L, request_t* r) {
	int i = 0;
	char* v = NULL;
	lua_newtable(L);
	lua_pushinteger(L, LUAFCGID_VERSION);
	lua_setfield(L, -2, "LUAFCGID_VERSION");
	while (env_var[i]) {
		v = FCGX_GetParam(env_var[i], r->fcgi.envp);
		if (v) {
			lua_pushstring(L, env_var[i]);
			lua_pushstring(L, v);
			lua_settable(L, -3);
		}
		i++;
	}
}

void luaL_pushcgicontent(lua_State* L, request_t* r) {
	char* buf = NULL;
	int size = r->conf->maxpost;
	size_t len = size;
	char* slen = FCGX_GetParam("CONTENT_LENGTH", r->fcgi.envp);

	// check if content length is provided
	if (slen)
		len = atoi(slen);
	if (len > size)
		len = size;
	// alloc and zero buffer
	buf = (char*) malloc(len);
	memset(buf, 0, len);
	// load and push buffer
	FCGX_GetStr(buf, len, r->fcgi.in);
	lua_pushlstring(L, buf, len);
	free(buf);
}

char* script_load(const char* fn, struct stat* fs) {
	FILE* fp = NULL;
	char* fbuf = NULL;

	// does it even exist?
	if (stat(fn, fs) == STATUS_OK) {
		// is it a file of more then 0 bytes?
		if (S_ISREG(fs->st_mode) && fs->st_size) {
			fp = fopen(fn, "rb");
			if (fp) {
				fbuf = (char*) malloc(fs->st_size);
				memset(fbuf, 0, fs->st_size);
				fread(fbuf, fs->st_size, 1, fp);
				fclose(fp);
			}
		}
	}

	return fbuf;
}

// create an ISO timestamp
void timestamp(char* ts) {
	time_t t = time(NULL);
	struct tm timeinfo;
	if (localtime_r(&t, &timeinfo)) {
		strftime(ts, 20, "%Y-%m-%d %H:%M:%S", &timeinfo);
	} else {
		strncpy(ts, "NULL", 20);
		ts[19] = '\0';
	}
}

// log a string and vars with a timestamp
void logit(const char* fmt, ...) {
	va_list args;
	char ts[20] = { '\0' };
	char *str = NULL;
	// make a copy and add the timestamp
	str = (char*) malloc(20 + strlen(fmt) + 2);
	timestamp(ts);
	sprintf(str, "%s %s\n", ts, fmt);
	// we just let stderr route it
	va_start(args, fmt);
	vfprintf(stderr, str, args);
	fflush(stderr);
	va_end(args);
	free(str);
}

// worker and parent threads

static void *worker_run(void *a) {
	// shared vars
	static pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
	// static pthread_mutex_t lua_mutex = PTHREAD_MUTEX_INITIALIZER;
	// local private vars
	int rc, i, j, k;
	char errtype[ERR_STR_SIZE + 1];
	char errmsg[ERR_SIZE + 1];
	params_t* params = (params_t*) a;
	pool_t* pool = params->pool;
	slot_t* slot = NULL;
	request_t req;
	char* script = NULL;
	char* split = NULL;
	lua_State* L = NULL;
	struct stat fs;
	char* fbuf = NULL;
	int clones = 0;
	BOOL flag = FALSE;

#ifdef CHATTER
	logit("[%d] starting", params->wid);
#endif

	FCGX_InitRequest(&req.fcgi, params->sock, 0);
	req.wid = params->wid;
	req.conf = params->conf;

	for (;;) {

		// use accept() serialization
		pthread_mutex_lock(&accept_mutex);
		rc = FCGX_Accept_r(&req.fcgi);
		pthread_mutex_unlock(&accept_mutex);

		if (rc < 0)
			break;

#ifdef CHATTER
		logit("[%d] accepting connection", params->wid);
#endif

		// init loop locals
		script = FCGX_GetParam("SCRIPT_FILENAME", req.fcgi.envp);
		rc = STATUS_404;
		errmsg[0] = '\0';
		slot = NULL;

		if (script) {
#ifdef _WIN32
			// normalize path seperator
			j = strlen(script);
			for (i = 0; i < j; i++) {
				if (script[i] == '/') script[i] = SEP;
			}
#endif
			// isolate the path in the script filename,
			// and change the cwd to it
			split = strrchr(script, SEP);
			if (split) {
				*split = '\0';
				chdir(script);
				*split = SEP;
			}
		}

		// search for script
		j = pool->count;
		k = 0;
		flag = FALSE;
		do {
			// give someone else a chance to flag out
			if (k)
				usleep(1);
#ifdef CHATTER
			if (k && !flag) logit("   [%d] is retrying the script scan #%d", params->wid, k);
#endif
			clones = 0;
			pthread_mutex_lock(&pool->mutex);
			for (i = 0; i < j; i++) {
				slot = &pool->slot[i];
				// do the names match and is it a valid state?
				if (((!script && !slot->name) || ((script && slot->name)
						&& (strcmp(script, slot->name) == 0))) && slot->state) {
					// count the clones
					clones++;
					// is the slot available?
					if (!slot->status) {
						// lock it
						slot->status = STATUS_BUSY;
#ifdef CHATTER
						logit("   [%d] found and locked slot [%d]", params->wid, i);
#endif
						break;
					}
				}
			}
			pthread_mutex_unlock(&pool->mutex);
			if ((i == j) && (clones > req.conf->clones)) {
				// we have max clones running, try again
				k = -1;
#ifdef CHATTER
				if (!flag) {
					logit("   [%d] thinks there are enough clones aleady", params->wid);
					flag = TRUE;
				}
#endif
			}
		} while ((i == j) && (++k <= req.conf->retries));

		if (i < j) {
			// found a matching state
			L = slot->state;
			rc = STATUS_OK;
		} else {
			// make a new state
			//pthread_mutex_lock(&lua_mutex);
			L = lua_open();
			if (!L) {
				logit("[%d] unable to init lua!", params->wid);
				return NULL;
			}
			luaL_openlibs(L);
			luaL_loadrequest(L);

			fbuf = script_load(script, &fs);

			if (fbuf) {
				// TODO: run state startup hook
				// load and run buffer
				rc = luaL_loadbuffer(L, fbuf, fs.st_size, script);
				if (rc == STATUS_OK)
					rc = lua_pcall(L, 0, 0, 0);
				// cleanup
				free(fbuf);
			}
			//pthread_mutex_unlock(&lua_mutex);

			if (rc == STATUS_OK) {
				// pick which part of the pool to use
				pthread_mutex_lock(&pool->mutex);
				// is there a free spot?
				for (i = 0; i < j; i++) {
					slot = &pool->slot[i];
					if (!slot->status && !slot->state) {
						slot->status = STATUS_BUSY;
#ifdef CHATTER
						logit("   [%d] locked free slot [%d]", params->wid, i);
#endif
						break;
					}
				}
				if (i == j) {
					// time to kick someone out of the pool :(
					// TODO: find a better way to pick a loser
					do {
						// search for an inactive state
						for (i = 0; i < j; i++) {
							slot = &pool->slot[i];
							if (!slot->status) {
								// found one, so lock it for ourselves
								slot->status = STATUS_BUSY;
#ifdef CHATTER
								logit("   [%d] locked inactive slot [%d]", params->wid, i);
#endif
								break;
							}
						}
						if (i == j) {
							// the pool is full & everyone is busy!
							// unlock the pool for a bit so someone else can flag out
							pthread_mutex_unlock(&pool->mutex);
							usleep((int) ((rand() % 3) + 1));
							pthread_mutex_lock(&pool->mutex);
						}
					} while (i == j);
#ifdef CHATTER
					logit("   [%d] kicked slot [%d] out of the pool", params->wid, i);
#endif
				}
				// 'slot' and 'i' should now reference a slot that is locked and free to use
				pthread_mutex_unlock(&pool->mutex);
				// scrub it clean and load it up
				slot_flush(slot);
				slot_load(slot, L, script);
#ifdef CHATTER
				logit("   [%d] loaded '%s' into slot [%d]", params->wid, script, i);
#endif
			} else {
				if (lua_isstring(L, -1)) {
					// capture the error message
					strncpy(errmsg, lua_tostring(L, -1), ERR_SIZE);
					errmsg[ERR_SIZE] = '\0';
					lua_pop(L, 1);
				}
				//pthread_mutex_lock(&lua_mutex);
				if (L)
					lua_close(L);
				//pthread_mutex_unlock(&lua_mutex);
				i = j;
			}
		}

		if (rc == STATUS_OK) {
			// we have a valid VM state, time to roll!
			lua_getglobal(L, "main");
			if (lua_isfunction(L, -1)) {
				// prepare request object
				req.headers_sent = FALSE;
				// prepare main() args
				luaL_pushcgienv(L, &req);
				luaL_pushrequest(L, &req);
				// call script handler
				rc = lua_pcall(L, 2, 0, 0);
				if (rc == LUA_ERRRUN || rc == LUA_ERRSYNTAX) {
					// capture the error message
					strncpy(errmsg, lua_tostring(L, -1), ERR_SIZE);
					errmsg[ERR_SIZE] = '\0';
					lua_pop(L, 1);
				}
			} else {
				rc = LUA_ERRRUN;
				strcpy(errmsg, "main() function not found");
			}
		}

		// translate for the puny humans
		switch (rc) {
		case STATUS_OK:
		case STATUS_404:
			break;
		case LUA_ERRFILE:
			strncpy(errtype, LUA_ERRFILE_STR, ERR_STR_SIZE);
			errtype[ERR_STR_SIZE] = '\0';
			break;
		case LUA_ERRRUN:
			strncpy(errtype, LUA_ERRRUN_STR, ERR_STR_SIZE);
			errtype[ERR_STR_SIZE] = '\0';
			break;
		case LUA_ERRSYNTAX:
			strncpy(errtype, LUA_ERRSYNTAX_STR, ERR_STR_SIZE);
			errtype[ERR_STR_SIZE] = '\0';
			break;
		case LUA_ERRMEM:
			strncpy(errtype, LUA_ERRMEM_STR, ERR_STR_SIZE);
			errtype[ERR_STR_SIZE] = '\0';
			break;
		default:
			strncpy(errtype, ERRUNKNOWN_STR, ERR_STR_SIZE);
			errtype[ERR_STR_SIZE] = '\0';
			break;
		};

		// log any errors
		switch (rc) {
		case STATUS_OK:
		case STATUS_404:
			break;
		case LUA_ERRRUN:
		case LUA_ERRSYNTAX:
			logit("[%d] %s: %s", params->wid, errtype, errmsg);
			break;
		case LUA_ERRFILE:
		case LUA_ERRMEM:
		default:
			logit("[%d] %s", params->wid, errtype);
			break;
		};

		// send the data out the tubes
		if (rc == STATUS_OK) {
			if (!req.headers_sent) {
				FCGX_FPrintF(req.fcgi.out, "Status: 200 OK\r\n"
					"Content-Type: text/html\r\n\r\n");
			}
		} else if (rc == STATUS_404) {
			// TODO: custom 404 handler
			HTTP_404(req.fcgi.out, script);
		} else {
			// TODO: custom 500 handler
			HTTP_500(req.fcgi.out, errtype, errmsg);
		}

		FCGX_Finish_r(&req.fcgi);

		if (i < j) {
			// we are done with the slot, so we shall flag out
			pthread_mutex_lock(&pool->mutex);
			slot->status = STATUS_OK;
			pthread_mutex_unlock(&pool->mutex);
		}

#ifdef CHATTER
		logit("[%d] done with request", params->wid);
#endif

		// cooldown
		usleep((int) ((rand() % 3) + 1));

	}

	return NULL;

}

#ifdef WIN32
void daemon(int nochdir, int noclose) {
	/* No daemon() on Windows - use srvany */
}
#endif

#ifdef NO_DAEMON
/* just in case there are a few older Linux users out there... */
#include <sys/resource.h>
#include <fcntl.h>
void daemon(int nochdir, int noclose) {
	/* Turn this process into a daemon
	 * based on work by JOACHIM Jona <jaj@hcl-club.lu>
	 */
	pid_t pid;
	struct rlimit fplim;
	int i, fd0, fd1, fd2;

	umask(0);

	if((pid = fork()) < 0) {
		logit("[DAEMON] unable to fork");
		exit(1);
	} else if(pid != 0) { /* parent */
		exit(0);
	}

	if(setsid() < 0) { /* become a session leader, lose controlling terminal */
		logit("[DAEMON] setsid error");
		exit(1);
	}
	if(nochdir == 0) {
		if(chdir("/") < 0) {
			logit("[DAEMON] chdir error");
			exit(1);
		}
	}
	if(getrlimit(RLIMIT_NOFILE, &fplim) < 0) {
		logit("[DAEMON] getrlimit error");
		exit(1);
	}
	for(i = 0; i < fplim.rlim_max; i++) close(i); /* close all open files */

	if(noclose == 0) {
		/* open stdin, stdout, stderr to /dev/null */
		fd0 = open("/dev/null", O_RDWR);
		fd1 = dup(0);
		fd2 = dup(0);

		/* TODO: initialize syslog */

		if(fd0 != 0 || fd1 != 1 || fd2 != 2) {
			logit("[DAEMON] unexected file descriptors");
			exit(1);
		}
	}
}
#endif

int main(int arc, char** argv) {

	int i, j, sock;
	pid_t pid = getpid();
	worker_t* worker = NULL;
	params_t* params = NULL;
	pool_t* pool = NULL;
	slot_t* slot = NULL;
	struct stat fs;
	config_t* conf = NULL;
	time_t now;
	time_t lastsweep;
	int interval;

	if (arc > 1 && argv[1]) {
		conf = config_load(argv[1]);
	} else {
		conf = config_load("config.lua");
	}

	daemon(0, 0);

	// redirect stderr to logfile
	if (conf->logfile)
		freopen(conf->logfile, "w", stderr);

	FCGX_Init();

	sock = FCGX_OpenSocket(conf->listen, 100);
	if (!sock) {
		logit("[PARENT] unable to create accept socket!");
		return 1;
	}

	pool = pool_open(conf->states);

	j = conf->workers;
	// alloc worker data
	worker = (worker_t*) malloc(sizeof(worker_t) * j);
	memset(worker, 0, sizeof(worker_t) * j);
	// alloc worker params
	params = (params_t*) malloc(sizeof(params_t) * j);
	memset(params, 0, sizeof(params_t) * j);

	for (i = 0; i < j; i++) {
		// initialize worker params
		params[i].pid = pid;
		params[i].wid = i;
		params[i].sock = sock;
		params[i].pool = pool;
		params[i].conf = conf;
		// create worker thread
		pthread_create(&worker[i].tid, NULL, worker_run, (void*) &params[i]);
		usleep(10);
	}

	lastsweep = time(NULL);

	for (;;) {
		// chill till the next sweep
		usleep(conf->sweep);
		// how long was the last cycle?
		now = time(NULL);
		interval = now - lastsweep;
		lastsweep = now;
		// housekeeping ...
		// first, we flush any Lua scripts that have been modified
		pthread_mutex_lock(&pool->mutex);
		for (i = 0; i < pool->count; i++) {
			slot = &pool->slot[i];
			// check for stale moon chips
			if (!slot->status && slot->state && slot->name) {
				if ((stat(slot->name, &fs) == STATUS_OK) && (fs.st_mtime
						> slot->load)) {
					slot_flush(slot);
#ifdef CHATTER
					logit("[%d] has gone stale", i);
#endif
				}
			}
		}
		pthread_mutex_unlock(&pool->mutex);
		// TODO: run housekeeping hook
	}

	free(params);
	free(worker);

	pool_close(pool);

	// dealloc config
	if (conf->L)
		lua_close(conf->L);
	if (conf->listen)
		free(conf->listen);
	free(conf);

	return 0;
}
