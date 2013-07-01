local c = require "cell.c"
local csocket = require "cell.c.socket"
local coroutine = coroutine
local assert = assert
local select = select
local table = table
local next = next
local pairs = pairs
local type = type

local session = 0
local port = {}
local task_coroutine = {}
local task_session = {}
local task_source = {}
local command = {}
local message = {}

local cell = {}

local self = c.self
local system = c.system
cell.self = self

local event_q1 = {}
local event_q2 = {}

local function new_task(source, session, co, event)
	task_coroutine[event] = co
	task_session[event] = session
	task_source[event] = source
end

function cell.fork(f)
	local co = coroutine.create(function() f() return "EXIT" end)
	session = session + 1
	new_task(nil, nil, co, session)
	cell.wakeup(session)
end

function cell.timeout(ti, f)
	local co = coroutine.create(function() f() return "EXIT" end)
	session = session + 1
	c.send(system, 2, self, session, "timeout", ti)
	new_task(nil, nil, co, session)
end

function cell.sleep(ti)
	session = session + 1
	c.send(system, 2, self, session, "timeout", ti)
	coroutine.yield("WAIT", session)
end

function cell.wakeup(event)
	table.insert(event_q1, event)
end

function cell.event()
	session = session + 1
	return session
end

function cell.wait(event)
	coroutine.yield("WAIT", event)
end

function cell.call(addr, ...)
	-- command
	session = session + 1
	c.send(addr, 2, cell.self, session, ...)
	return select(2,assert(coroutine.yield("WAIT", session)))
end

function cell.rawcall(addr, session, ...)
	c.send(addr, ...)
	return select(2,assert(coroutine.yield("WAIT", session)))
end

function cell.send(addr, ...)
	-- message
	c.send(addr, 3, ...)
end

cell.rawsend = c.send

function cell.dispatch(p)
	local id = assert(p.id)
	if p.replace then
		assert(port[id])
	else
		assert(port[id] == nil)
	end
	port[id] = p
end

function cell.cmd(...)
	return cell.call(system, ...)
end

function cell.exit()
	cell.send(system, "kill", self)
	-- no return
	cell.wait(cell.event())
end

function cell.command(cmdfuncs)
	command = cmdfuncs
end

function cell.message(msgfuncs)
	message = msgfuncs
end

local function suspend(source, session, co, ok, op, ...)
	if ok then
		if op == "RETURN" then
			c.send(source, 1, session, true, ...)
		elseif op == "EXIT" then
			-- do nothing
		elseif op == "WAIT" then
			new_task(source, session, co, ...)
		else
			error ("Unknown op : ".. op)
		end
	elseif source then
		c.send(source, 1, session, false, op)
	else
		print(cell.self,op)
		print(debug.traceback(co))
	end
end

local function resume_co(session, ...)
	local co = task_coroutine[session]
	if co == nil then
		error ("Unknown response : " .. tostring(session))
	end
	local reply_session = task_session[session]
	local reply_addr = task_source[session]
	task_coroutine[session] = nil
	task_session[session] = nil
	task_source[session] = nil
	suspend(reply_addr, reply_session, co, coroutine.resume(co, ...))
end

local function deliver_event()
	while next(event_q1) do
		event_q1, event_q2 = event_q2, event_q1
		for i = 1, #event_q2 do
			local ok, err = pcall(resume_co,event_q2[i])
			if not ok then
				print(cell.self,err)
			end
			event_q2[i] = nil
		end
	end
end

function cell.main() end

------------ sockets api ---------------
local sockets = {}
local sockets_event = {}
local sockets_arg = {}
local sockets_closed = {}
local sockets_fd = nil

local socket = {}

local socket_meta = {
	__index = socket,
	__gc = function(self)
		cell.send(sockets_fd, "disconnect", self.__fd)
	end
}

function cell.connect(addr, port)
	sockets_fd = sockets_fd or cell.cmd("socket")
	local obj = { __fd = assert(cell.call(sockets_fd, "connect", self, addr, port), "Connect failed") }
	return setmetatable(obj, socket_meta)
end

function socket:disconnect()
	assert(sockets_fd)
	local fd = self.__fd
	sockets[fd] = nil
	sockets_closed[fd] = true
	if sockets_event[fd] then
		cell.wakeup(sockets_event[fd])
	end
	cell.send(sockets_fd, "disconnect", fd)
end

function socket:write(msg)
	local fd = self.__fd
	cell.rawsend(sockets_fd, 6, fd, csocket.sendpack(msg))
end

local function socket_wait(fd, sep)
	assert(sockets_event[fd] == nil)
	sockets_event[fd] = cell.event()
	sockets_arg[fd] = sep
	cell.wait(sockets_event[fd])
end

function socket:readbytes(bytes)
	local fd = self.__fd
	if sockets_closed[fd] then
		sockets[fd] = nil
		return
	end
	if sockets[fd] then
		local data = csocket.pop(sockets[fd], bytes)
		if data then
			return data
		end
	end
	socket_wait(fd, bytes)
	if sockets_closed[fd] then
		sockets[fd] = nil
		return
	end
	return csocket.pop(sockets[fd], bytes)
end

function socket:readline(sep)
	local fd = self.__fd
	if sockets_closed[fd] then
		sockets[fd] = nil
		return
	end
	sep = sep or "\n"
	if sockets[fd] then
		local line = csocket.readline(sockets[fd], sep)
		if line then
			return line
		end
	end
	socket_wait(fd, sep)
	if sockets_closed[fd] then
		sockets[fd] = nil
		return
	end
	return csocket.readline(sockets[fd], sep)
end

----------------------------------------

cell.dispatch {
	id = 6, -- socket
	dispatch = function(fd, sz, msg)
		local ev = sockets_event[fd]
		sockets_event[fd] = nil
		if sz == 0 then
			sockets_closed[fd] = true
			if ev then
				cell.wakeup(ev)
			end
		else
			local buffer, bsz = csocket.push(sockets[fd], msg, sz)
			sockets[fd] = buffer
			if ev then
				local arg = sockets_arg[fd]
				if type(arg) == "string" then
					local line = csocket.readline(buffer, arg, true)
					if line then
						cell.wakeup(ev)
					end
				else
					if bsz >= arg then
						cell.wakeup(ev)
					end
				end
			end
		end
	end
}

cell.dispatch {
	id = 5, -- exit
	dispatch = function()
		local err = tostring(self) .. " is dead"
		for event,session in pairs(task_session) do
			local source = task_source[event]
			if source ~= self then
				c.send(source, 1, session, false, err)
			end
		end
	end
}

cell.dispatch {
	id = 4, -- launch
	dispatch = function(source, session, report, ...)
		local args = { ... }
		local op = report and "RETURN" or "EXIT"
		local co = coroutine.create(function() return op, cell.main(table.unpack(args)) end)
		suspend(source, session, co, coroutine.resume(co))
	end
}

cell.dispatch {
	id = 3, -- message
	dispatch = function(cmd, ...)
		local f = message[cmd]
		if f == nil then
			print("Unknown message ", cmd)
		else
			local n = select("#", ...)
			local co
			if n < 5 then
				local p1,p2,p3,p4 = ...
				co = coroutine.create(function() return "EXIT", f(p1,p2,p3,p4) end)
			else
				local args = { ... }
				co = coroutine.create(function() return "EXIT", f(table.unpack(args)) end)
			end
			suspend(source, session, co, coroutine.resume(co))
		end
	end
}

cell.dispatch {
	id = 2,	-- command
	dispatch = function(source, session, cmd, ...)
		local f = command[cmd]
		if f == nil then
			c.send(source, 1, session, false, "Unknown command " ..  cmd)
		else
			local n = select("#", ...)
			local co
			if n < 5 then
				local p1,p2,p3,p4 = ...
				co = coroutine.create(function() return "RETURN", f(p1,p2,p3,p4) end)
			else
				local args = { ... }
				co = coroutine.create(function() return "RETURN", f(table.unpack(args)) end)
			end
			suspend(source, session, co, coroutine.resume(co))
		end
	end
}

cell.dispatch {
	id = 1,	-- response
	dispatch = function (session, ...)
		resume_co(session,...)
	end,
}

c.dispatch(function(p,...)
	local pp = port[p]
	if pp == nil then
		deliver_event()
		error ("Unknown port : ".. p)
	end
	pp.dispatch(...)
	deliver_event()
end)

return cell