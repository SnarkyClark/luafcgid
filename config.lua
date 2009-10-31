--[[ configuration script for luafcgid ]]--

-- port or socket path to listen to
listen = ":9000"

-- number of worker threads
workers = 3

-- number of Lua VM states
states = 5

-- housekeeping sweep cycle in milliseconds
sweep = 1000

-- number of search cycles before creating
-- a new Lua VM state for a requested script
retries = 1

-- max POST size allowed
maxpost = 1024 * 1024

-- full or relative path to logfile
logfile = "luafcgid.log"
