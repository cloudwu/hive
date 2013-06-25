local hive = require "hive"

hive.start {
	thread = 4,
	main = "main.lua",
}