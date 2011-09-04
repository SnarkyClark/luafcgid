local M = {}

-- serialize simple Lua values into a string
-- can be used with v = loadstring("return " .. s)()
function M.serialize(v)
	local s = ""
    if type(v) == "table" then
    	local t = {}
        for name, val in pairs(v) do
        	if type(name) == "number" then
        		name = tostring(name)
        	elseif type(name) == "string" then
        		name = string.format("[%q]", name)
        	end
            table.insert(t, string.format("%s = %s", name, M.serialize(val)))
        end
		s = '{' .. table.concat(t, ",")	.. '}'
    elseif type(v) == "number" then
        s = tostring(v)
    elseif type(v) == "string" then
        s = string.format("%q", v)
    elseif type(v) == "boolean" then
        s = v and "true" or "false"
    end
    return s
end

-- urlencode a string
function M.urlencode(s)
	if (s and #s > 0) then
	    s = string.gsub(s, "([^%w ])",
    	    function (c) return string.format ("%%%02X", string.byte(c)) end)
	    s = string.gsub(s, " ", "+")
  	end
  	return s	
end

-- urldecode a string
function M.urldecode(s)
	if (s and #s > 0) then
		s = string.gsub(s, "+", " ")
		s = string.gsub(s, "%%(%x%x)",
			function(h) return string.char(tonumber(h,16)) end)
	end
	return s
end

local function parse_pair(s)
	local n, v
	if (s and #s > 0) then
		_, _, n, v = string.find(s, "([^=]*)=([^=]*)")
		if not v then v = "" end
	end
	return M.urldecode(n), M.urldecode(v)
end

-- parse an 'application/x-www-form-urlencoded' string into a table
function M.parse(s)
	local t = {}
	for p in string.gmatch(s, "[^&]*") do
		if (p and #p > 0) then
			print('"' .. p .. '"') 
			local n, v = parse_pair(p)
			if t[n] then
				if type(t[n]) ~= "table" then
					t[n] = {t[n]}
				end
				table.insert(t[n], v)
			else
				t[n] = v
			end
		end 
	end
	return t	
end

return M