local c = require "cell.c"
local coroutine = coroutine
local assert = assert
local select = select
local table = table
local next = next

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
	session = session + 1
	local co = coroutine.create(function() f() return "EXIT" end)
	coroutine.yield("FORK", co, session)
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
	assert(port[id] == nil)
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
		elseif op == "FORK" then
			new_task(nil, nil, ...)
			return suspend(source, session, co, coroutine.resume(co))
		else
			error ("Unknown op : ".. op)
		end
	elseif source then
		c.send(source, 1, session, false, op)
	else
		print(op)
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
			resume_co(event_q2[i])
			event_q2[i] = nil
		end
		event_q1, event_q2 = event_q2, event_q1
	end
end

function cell.main() end

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
	dispatch = resume_co,
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