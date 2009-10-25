-- configuration script for luafcgid

workers = 5    -- number of worker threads
states = 10    -- number of Lua VM states
sweep = 10000  -- housekeeping sweep cycle in milliseconds
retries = 3    -- number of search cycles before creating
               -- a new Lua VM state for a requested script
