-- path of the temporary file
tmpfile = "/tmp/promscrcount.txt"

-- read the access count from the file and write back
function _begin()
   local file = io.open(tmpfile, "r+")
   local count = 0
   if file then
      count = file:read()
      file:seek("set", 0)
   else
      file = io.open(tmpfile, "w")
   end
   if not count then
      count = 0
   end
   count = count + 1
   file:write(string.format("%d\n\n", count))
   file:close()
   return "The access count is " .. count .. "."
end
