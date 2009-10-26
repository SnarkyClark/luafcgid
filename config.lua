--[[ configuration script for luafcgid ]]--

-- port or socket path to listen to
listen = ":9000"

-- number of worker threads
workers = 5

-- number of Lua VM states
states = 10

-- housekeeping sweep cycle in milliseconds
sweep = 10000

-- number of search cycles before creating
-- a new Lua VM state for a requested script
retries = 3

-- max POST size allowed
maxpost = 1024 * 1024
