-- utilies for processing a FCGI request
local lf = require("luafcgid")

function main(env, con)
	-- gotta give Lua some props
	con:header("X-Powered-By", "Lua")
	
	con:puts("<h1>Environment</h1><pre>\n")
	for n, v in pairs(env) do
		con:puts(string.format("%s = %s\n", n, v))
	end
	con:puts("</pre>\n")

	if env.REQUEST_METHOD == "GET" then
		s = {}  -- string sets are faster then calling req:puts() all the time
		params = lf.parse(env.QUERY_STRING)
		table.insert(s, "<h1>GET Params</h1><pre>\n")
		for n, v in pairs(params) do
			table.insert(s, string.format("%s = %s\n", n, v))
		end
		table.insert(s, "</pre>\n")
		con:puts(table.concat(s))
	end

	if env.REQUEST_METHOD == "POST" then
		con:puts("<h1>POST</h1>\n")
		con:puts(string.format("<textarea>%s</textarea>", con:gets()))
	end
end
