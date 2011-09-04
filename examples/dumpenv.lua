local fcgi = require("luafcgid")

function main(env, req)
	-- gotta give Lua some props
	req:header("X-Powered-By", "Lua")
	
	req:puts("<h1>Environment</h1><pre>\n")
	for n, v in pairs(env) do
		req:puts(string.format("%s = %s\n", n, v))
	end
	req:puts("</pre>\n")

	if env.REQUEST_METHOD == "GET" then
		s = {}  -- string sets are MUCH faster then calling req:puts() all the time
		params = fcgi.parse(env.QUERY_STRING)
		table.insert(s, "<h1>GET Params</h1><pre>\n")
		for n, v in pairs(params) do
			table.insert(s, string.format("%s = %s\n", n, v))
		end
		table.insert(s, "</pre>\n")
		req:puts(table.concat(s))
	end

	if env.REQUEST_METHOD == "POST" then
		req:puts("<h1>POST</h1>\n")
		req:puts(string.format("<textarea>%s</textarea>", req:gets()))
	end
end