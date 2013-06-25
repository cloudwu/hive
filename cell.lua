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
	local co = coroutine.create(function() f() return "RETURN" end)
	coroutine.yield("FORK", co, session)
end

function cell.timeout(ti, f)
	local co = coroutine.create(function() f() return "RETURN" end)
	session = session + 1
	c.send(system, 2, self, session, "timeout", ti)
	new_task(nil, nil, co, session)
end

function cell.sleep(ti)
	session = session + 1
	c.send(system, 2, self, session, "timeout", ti)
	coroutine.yield("WAIT", session)
end

function cell.start(f)
	return cell.timeout(0, f)
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

function cell.call(addr, port_name, ...)
	local p = assert(port[port_name])
	c.send(addr, p.id, p.pack(...))
	if p.request then
		return select(2,assert(coroutine.yield("CALL", session)))
	end
end

function cell.dispatch(p)
	local id = assert(p.id)
	local name = assert(p.name)
	assert(port[id] == nil)
	assert(port[name] == nil)
	port[id] = p
	port[name] = p
end

function cell.cmd(...)
	return cell.call(system, "command", ...)
end

function cell.exit()
	cell.call(system, "command", "kill", self)
end

function cell.command(cmdfuncs)
	command = cmdfuncs
end

local function suspend(source, session, co, ok, op, ...)
	if ok then
		if op == "RETURN" then
			if source then
				c.send(source, 1, session, true, ...)
			end
		elseif op == "CALL" or op == "WAIT" then
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

cell.dispatch {
	id = 2,
	name = "command",
	request = true,
	pack = function(...)
		session = session + 1
		return self, session, ...
	end,
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
	id = 1,
	name = "response",
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