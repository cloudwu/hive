local cell = require "cell"

cell.start(function()
	print(cell.cmd("echo","Hello world"))
	local ping = cell.cmd("launch", "pingpong")
	print(cell.call(ping, "command", "ping"))
	cell.cmd("kill",ping)
	for i=1,10 do
		cell.sleep(100)
		print(i)
	end
	cell.exit()
end)