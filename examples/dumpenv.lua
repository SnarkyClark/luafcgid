function main(env, req)
	s = {}

	table.insert(s, "<pre>\n")
	for n, v in pairs(env) do
		table.insert(s, string.format("%s = %s\n", n, v))
	end
	table.insert(s, "</pre>\n")
	
	req:puts(table.concat(s))

	if env.REQUEST_METHOD == "POST" then
		req:puts(string.format("<textarea>%s</textarea>", req:gets()))
	end

end