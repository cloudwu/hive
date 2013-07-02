Hive
====

Parallel multiple lua states , actor model for lua.

Quick Start
===
make and run 
```
lua test.lua
```

the main logic is in test/main.lua .

You can read this blog first (http://blog.codingnow.com/2013/06/hive_lua_actor_model.html) (In Chinese)  

How to Launch the hive
===
```lua
local hive = require "hive"

hive.start {
  thread = 4,   -- 4 worker thread, You can set more if you have more cpu core.
	main = "test.main",  -- main cell, the cell name search rule is the same with require.
}
```

How the cell work
===
Let's read test/pingpong.lua first.

```lua
local cell = require "cell"

cell.command {
  ping = function()
		cell.sleep(1)
		return "pong"
	end
}

function cell.main(...)
	print("pingpong launched")
	return ...
end
```
If you launch test.pingping, the function cell.main(...) will be execute first.

You can use cell.cmd("launch", "test.pingpong", ...) to launch it.

test.pingpong is a simple cell that support one command 'ping'. If you send a command 'ping' to it,
It will sleep 0.01 second first, and the send 'pong' back.

None-blocking socket library
===
Hive support none-blocking socket api that can be used in every cell.

cell.listen can be used in a server cell, the accepter will be call every time accept a new connection. 
You can forward the data from new connection to a new cell, or forward to itself (fork a coroutine to deliver the data).

cell.connect can be used in a client cell.

Todo
====

* Multi-process support
* Broadcast message
* Signal for cell exit
* Database (Redis/Mongo/SQL) driver
* Debugger/Monitor
* Log system (Instead of print error message)
* Better document
* Better samples
* More options in hive.start
* And more

