local c = require "hive.core"

local system_cell = assert(package.searchpath("hive.system", package.path),"system cell was not found")

local hive = {}

function hive.start(t)
	local main = assert(package.searchpath(t.main, package.path), "main cell was not found")
	return c.start(t, system_cell, main)
end

return hive