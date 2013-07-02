package = "hive"
version = "0.1-1"
source = {
   url = "http://..." -- We don't have one yet
}
description = {
	summary = "Parallel multiple lua states.",
	detailed = [[
		Hive is a small library to provide parallel 
		multiple lua states . It works like an erlang
		system . You can use actor model in lua.
	]],
	homepage = "https://github.com/cloudwu/hive",
	license = "MIT/X11",
	maintainer = "云风 <cloudwu@gmail.com>"
}

supported_platforms = {
	"unix",
	"windows",
	"macosx",
}

dependencies = {
	"lua >= 5.2, < 5.3"
}

build = {
	type = "builtin",
	modules = {
		hive = "hive.lua",
		["hive.system"] = "hive/system.lua",
		["hive.socket"] = "hive/socket.lua",
		["hive.core"] = {
			sources = { 
				"src/hive.c",
				"src/hive_cell.c" ,
				"src/hive_seri.c" ,
				"src/hive_scheduler.c" ,
				"src/hive_env.c" ,
				"src/hive_cell_lib.c" ,
				"src/hive_system_lib.c" ,
				"src/hive_socket_lib.c",
			},
			libraries = { "pthread" },
		}
	},
}