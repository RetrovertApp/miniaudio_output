require "tundra.syntax.glob"
require "tundra.path"
require "tundra.util"

-----------------------------------------------------------------------------------------------------------------------

SharedLibrary {
	Name = "miniaudio",
	Includes = { "../retrovert_api/c" },
	Sources = { "output_miniaudio.c" },
}

-----------------------------------------------------------------------------------------------------------------------

Default "miniaudio"
