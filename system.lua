local cell = require "cell"
local system = require "cell.system"
local table = table

local command = {}
local message = {}
local ticker = 0
local timer = {}
local free_queue = {}

local function alloc_queue()
	local n = #free_queue
	if n > 0 then
		local r = free_queue[n]
		free_queue[n] = nil
		return r
	else
		return {}
	end
end

function command.launch(name, ...)
	local c = system.launch(name .. ".lua")
	if c then
		-- 4 is launch port
		local ev = cell.event()
		return c, cell.rawcall(c, ev, 4, cell.self, ev, true, ...)
	else
		error ("launch " ..  name .. " failed")
	end
end

function command.echo(str)
	return str
end

function command.kill(c)
	return assert(system.kill(c))
end

function command.timeout(n)
	if n > 0 then
		local ev = cell.event()
		local ti = ticker + n
		local q = timer[ti]
		if q == nil then
			q = alloc_queue()
			timer[ti] = q
		end
		table.insert(q, ev)
		cell.wait(ev)
	end
end

message.kill = command.kill

cell.command(command)
cell.message(message)

cell.dispatch {
	id = 0,
	dispatch = function()
		ticker = ticker + 1
		local q = timer[ticker]
		if q == nil then
			return
		end
		for i=1,#q do
			cell.wakeup(q[i])
			q[i] = nil
		end
		timer[ticker] = nil
		table.insert(free_queue, q)
	end
}

local function start()
	system.init()
	local c = system.launch("main.lua")
	if c then
		cell.rawsend(c, 4, nil, nil, false)
	else
		error ("launch main.lua failed")
	end
end

start()

