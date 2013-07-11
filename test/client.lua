local cell = require "cell"

function cell.main(fd, addr)
	print(addr, "connected")
	local obj = cell.bind(fd)
	cell.fork(function()
		local line = obj:readline "\n"
		obj:write(line)
		obj:disconnect()
		cell.exit()
	end)
end