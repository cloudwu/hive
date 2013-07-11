local cell = require "cell"

local function accepter(fd, addr, listen_fd)
	print("Accept from ", listen_fd)
	-- can't read fd in this function, because socket.cell haven't forward data from fd
	local client = cell.cmd("launch", "test.client",fd, addr)
	-- return cell the data from fd will forward to, you can also return nil for forwarding to self
	return client
end

function cell.main()
	print("[cell main]",cell.self)
	-- save listen_fd for prevent gc.
	cell.listen("127.0.0.1:8888",accepter)
--[[ socket api
	local sock = cell.connect("localhost", 8888)
	local line = sock:readline(fd)
	print(line)
	sock:write(line .. "\n")
]]
	print(cell.cmd("echo","Hello world"))
	local ping, pong = cell.cmd("launch", "test.pingpong","pong")
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