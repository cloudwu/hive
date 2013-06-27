local cell = require "cell"

function cell.main()
	print(cell.cmd("echo","Hello world"))
	local ping, pong = cell.cmd("launch", "pingpong","pong")
	print(ping,pong)
	print(cell.call(ping, "ping"))
	cell.fork(function()
		-- kill ping after 9 second
		cell.sleep(900)
		cell.cmd("kill",ping) end
	)
	for i=1,10 do
		print(pcall(cell.call,ping, "ping"))
		cell.sleep(100)
		print(i)
	end
	cell.exit()
end