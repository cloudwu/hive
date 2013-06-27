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