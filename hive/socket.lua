local csocket = require "cell.c.socket"
csocket.init()

local cell = require "cell"
local command = {}
local message = {}

local sockets = {}

function command.connect(source,addr,port)
	local fd = csocket.connect(addr, port)
	if fd then
		sockets[fd] = source
	end
	return fd
end

function message.disconnect(fd)
	sockets[fd] = nil
	csocket.close(fd)
end

cell.command(command)
cell.message(message)
cell.dispatch {
	id = 6, -- socket
	dispatch = csocket.send, -- fd, sz, msg
	replace = true,
}

function cell.main()
	local result = {}
	while true do
		for i = 1, csocket.poll(result) do
			local v = result[i]
			local c = sockets[v[1]]
			if c then
				cell.rawsend(c, 6, v[1], v[2], v[3])
			else
				csocket.freepack(v[3])
			end
		end
		cell.sleep(0)
	end
end