/*
 * luafcgid -- A simple multi-threaded Lua+FastCGI daemon.
 *
 * this code is provided under the "Simplified BSD License"
 * (c) STPeters 2009
 */

#include "main.h"

const char* CRLF = "\r\n";

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
	char** p = r->fcgi.envp;
	char* v = NULL;
	// create new table on the stack
	lua_newtable(L);
	// add gratuitous version info
	lua_pushinteger(L, LUAFCGID_VERSION);
	lua_setfield(L, -2, "LUAFCGID_VERSION");
	// iterate over the FCGI envp structure
	if (p == NULL) return;
	while (*p) {
		// string is in format 'name=value'
		v = strchr(*p, '=');
		if (v) {
			lua_pushlstring(L, *p, v-*p);
			lua_pushstring(L, ++v);
			lua_settable(L, -3);
		}
		p++;
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

/* worker thread */
static void *worker_run(void *a) {
	/* shared vars */
	static pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
	/* static pthread_mutex_t lua_mutex = PTHREAD_MUTEX_INITIALIZER; */

	/* local private vars */
	int rc, i, j, k;
	char errtype[ERR_STR_SIZE + 1];
	char errmsg[ERR_SIZE + 1];
	params_t* params = (params_t*) a;
	pool_t* pool = params->pool;
	slot_t* slot = NULL;
	int found = -1;
	request_t req;
	char* script = NULL;
	char* split = NULL;
	lua_State* L = NULL;
	struct stat fs;
	char* fbuf = NULL;
	int clones = 0;
	BOOL flag = FALSE;

	/* output buffering */
	int header_size = params->conf->headersize;
	int buffer_size = params->conf->buffersize;
	char* header = malloc(header_size);
	char* buffer = malloc(buffer_size);

#ifdef CHATTER
	logit("worker [%d] starting", params->wid);
#endif

	FCGX_InitRequest(&req.fcgi, params->sock, 0);
	req.wid = params->wid;
	req.conf = params->conf;

	for (;;) {

		header[0] = '\0';
		buffer[0] = '\0';

		/* use accept() serialization */
		pthread_mutex_lock(&accept_mutex);
		rc = FCGX_Accept_r(&req.fcgi);
		pthread_mutex_unlock(&accept_mutex);

		if (rc < 0)
			break;

#ifdef CHATTER
		logit("worker [%d] accepting connection", params->wid);
#endif

		/* init loop locals */
		script = FCGX_GetParam("SCRIPT_FILENAME", req.fcgi.envp);
		rc = STATUS_404;
		errmsg[0] = '\0';
		slot = NULL;

		if (script) {
#ifdef _WIN32
			/* normalize path seperator */
			j = strlen(script);
			for (i = 0; i < j; i++) {
				if (script[i] == '/') script[i] = SEP;
			}
#endif
			/* isolate the path in the script filename, */
			/* and change the cwd to it */
			split = strrchr(script, SEP);
			if (split) {
				*split = '\0';
				chdir(script);
				*split = SEP;
			}
		}

		/* search for running script in the state pool */
		k = 0;
		do {
			#ifdef CHATTER
			if (k) logit("   worker [%d] is retrying the script scan #%d", params->wid, k);
			#endif

			/* search the pool */
			found = pool_scan_idle(pool, script);
			if (found < 0) {
				/* if we have max clones running, start over */
				if (abs(++found) > req.conf->clones) k = -1;
				/* give someone else a chance to flag out */
				usleep(1);
			} else {
				#ifdef CHATTER
				logit("   worker [%d] found and locked slot [%d]", params->wid, found);
				#endif
			}
		} while ((found < 0) && (++k <= req.conf->retries));

		if (found >= 0) {
			/* found a matching state */
			slot = pool_slot(pool, found);
			L = slot->state;
			rc = STATUS_OK;
		} else {
			/* make a new state */
			L = lua_open();
			if (!L) {
				logit("[%d] unable to init lua!", params->wid);
				return NULL;
			}
			luaL_openlibs(L);
			luaL_loadrequest(L);

			fbuf = script_load(script, &fs);

			if (fbuf) {
				/* TODO: run state startup hook */
				/* load and run buffer */
				rc = luaL_loadbuffer(L, fbuf, fs.st_size, script);
				if (rc == STATUS_OK)
					rc = lua_pcall(L, 0, 0, 0);
				/* cleanup */
				free(fbuf);
			}

			if (rc == STATUS_OK) {
				/* pick which part of the pool to use */
				/* grab a free spot, kicking the quietest one out if needed */
				do {
					found = pool_scan_free(pool);
#ifdef CHATTER
					if (found >= 0) logit("   [%d] locked free slot [%d]", params->wid, found);
#endif
					if (found < 0) {
						/* the pool is full & everyone is busy! */
						/* wait around for a bit so someone else can flag out */
						usleep((int) ((rand() % 3) + 1));
					}
				} while(found < 0);

				/* load it up */
				pool_load(pool, found, L, script);
#ifdef CHATTER
				logit("   [%d] loaded '%s' into slot [%d]", params->wid, script, found);
#endif
			} else {
				if (lua_isstring(L, -1)) {
					/* capture the error message */
					strncpy(errmsg, lua_tostring(L, -1), ERR_SIZE);
					errmsg[ERR_SIZE] = '\0';
					lua_pop(L, 1);
				}
				if (L) lua_close(L);
			}
		}

		if (rc == STATUS_OK) {
			/* we have a valid VM state, time to roll! */
#ifdef CHATTER
			logit("[%d] running '%s', Lua stack size = %d", params->wid, script, lua_gettop(L));
#endif
			lua_getglobal(L, req.conf->handler);
			if (lua_isfunction(L, -1)) {
				/* prepare request object */
				req.headers_sent = FALSE;
				/* prepare handler args */
				luaL_pushcgienv(L, &req);
				luaL_pushrequest(L, &req);
				/* call script handler */
				rc = lua_pcall(L, 2, 0, 0);
				if (rc == LUA_ERRRUN || rc == LUA_ERRSYNTAX) {
					/* capture the error message */
					strncpy(errmsg, lua_tostring(L, -1), ERR_SIZE);
					errmsg[ERR_SIZE] = '\0';
					lua_pop(L, 1);
				}
			} else {
				rc = LUA_ERRRUN;
				sprintf(errmsg, "%s() function not found", req.conf->handler);
			}
		}

		/* translate for the puny humans */
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

		/* log any errors */
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

		/* send the data out the tubes */
		if (rc == STATUS_OK) {
			/* TODO: formulate headers */
			FCGX_PutStr(header, strlen(header), req.fcgi.out);
			FCGX_PutStr(CRLF, strlen(CRLF), req.fcgi.out);
		} else if (rc == STATUS_404) {
			/* TODO: custom 404 handler */
			HTTP_404(req.fcgi.out, script);
		} else {
			/* TODO: custom 500 handler */
			HTTP_500(req.fcgi.out, errtype, errmsg);
		}

		FCGX_Finish_r(&req.fcgi);

		if (i < j) {
			/* we are done with the slot, so we shall flag out */
			pool_lock(pool);
			slot->status = STATUS_OK;
			pool_unlock(pool);
		}

#ifdef CHATTER
		logit("[%d] done with request", params->wid);
#endif

		/* cooldown */
		usleep((int) ((rand() % 3) + 1));

	}

	return NULL;

}

#ifdef _WIN32
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
		pool_lock(pool);
		for (i = 0; i < pool->count; i++) {
			slot = pool_slot(pool, i);
			// check for stale moon chips
			if (!slot->status && slot->state && slot->name) {
				if ((stat(slot->name, &fs) == STATUS_OK) && (fs.st_mtime
						> slot->load)) {
					pool_flush(pool, i);
#ifdef CHATTER
					logit("[%d] has gone stale", i);
#endif
				}
			}
		}
		pool_unlock(pool);
		// TODO: run housekeeping hook
	}

	free(params);
	free(worker);

	pool_close(pool);
	config_free(conf);

	return 0;
}
