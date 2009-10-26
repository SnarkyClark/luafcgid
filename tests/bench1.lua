require('md5')
require('base64')

s = "Hello World"

function main(env, req)
   -- need a place to store our lines
   -- make it local so GC can come clean it up
   local line = {}
   -- start adding lines to page
   table.insert(line, "<pre>")
   for i = 1, 100 do
      table.insert(line, string.format(
         "%s + %d = %s", s, i, md5.digest(base64.encode(s .. i))
      ))
   end
   table.insert(line, "</pre>")
   -- build page from lines and send to browser
   req:puts(table.concat(line, '\r\n'))
end