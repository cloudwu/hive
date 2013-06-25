local cell = require "cell"

cell.command {
	ping = function()
		return "pong"
	end
}