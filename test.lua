package.cpath = package.cpath .. ";./?.dylib"

local hive = require "hive"

hive.start {
	thread = 4,
}
