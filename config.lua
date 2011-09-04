--[[ configuration script for luafcgid ]]--

-- port or socket path to listen to
-- listen = "127.0.0.1:9000"
listen = "/var/tmp/luafcgid.socket"

-- number of worker threads
workers = 3

-- max number of Lua VM states (NOTE: value needs to be larger then or equal to "workers")
states = workers * 2

-- max number of instances of an individual script allowed
clones = states

-- housekeeping sweep cycle in milliseconds
sweep = 5000

-- number of search cycles before creating
-- a new Lua VM state for a requested script
retries = 1

-- do we show errors in browser?
showerrors = true

-- do we perform output buffering?
buffering = true

-- starting buffer size for custom HTTP headers 
headersize = 64

-- starting buffer size for HTTP body 
bodysize = 1024

-- custom headers to add to all requests
headers = "X-Powered-By: Lua\r\n"

-- handler function name
handler = "main"

-- default HTTP status
httpstatus = "200 OK"

-- default HTTP content type
contenttype = "text/html"

-- max POST size allowed
maxpost = 1024 * 1024

-- full or relative path to logfile
logfile = "luafcgid.log"

