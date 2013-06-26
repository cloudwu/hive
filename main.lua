local cell = require "cell"

function cell.main()
	print(cell.cmd("echo","Hello world"))
	local ping, pong = cell.cmd("launch", "pingpong","pong")
	print(ping,pong)
	print(cell.call(ping, "ping"))
	cell.cmd("kill",ping)
	print(pcall(cell.call,ping, "ping"))
	for i=1,10 do
		cell.sleep(100)
		print(i)
	end
	cell.exit()
end