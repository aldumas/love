# Copyright (c) 2006-2026 LOVE Development Team
#
# This software is provided 'as-is', without any express or implied
# warranty.  In no event will the authors be held liable for any damages
# arising from the use of this software.
#
# Permission is granted to anyone to use this software for any purpose,
# including commercial applications, and to alter it and redistribute it
# freely, subject to the following restrictions:
#
# 1. The origin of this software must not be misrepresented; you must not
#    claim that you wrote the original software. If you use this software
#    in a product, an acknowledgment in the product documentation would be
#    appreciated but is not required.
# 2. Altered source versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.
# 3. This notice may not be removed or altered from any source distribution.

# Ruby port of arg.lua. Provides Love::Path and Love::Arg helpers plus the
# boot-time argument accessors on the Love module.
#
# The standalone harness does not receive a real command line / .love game, so
# the option parser is intentionally minimal compared to arg.lua: the game
# source is the script passed to --boot. The path helpers and the getLow /
# parseGameArguments structure are kept faithful.

module Love
  # Boot-time game-argument bookkeeping (set during Love.boot / Love.init).
  class << self
    attr_accessor :raw_game_arguments, :parsed_game_arguments
  end

  module Path
    # Replace any \ with /.
    def self.normalslashes(p)
      p.gsub("\\", "/")
    end

    # Make sure there is a trailing slash.
    def self.endslash(p)
      p.end_with?("/") ? p : p + "/"
    end

    # Whether a path is absolute (leading slash, or a Windows drive letter).
    def self.abs(p)
      tmp = normalslashes(p)
      return true if tmp.start_with?("/")
      tmp.length >= 2 && tmp[1] == ":"
    end

    # Convert any path into a normalized full path.
    def self.get_full(p)
      normalslashes(p)
    end

    # Return the leaf (last component) of a path.
    def self.leaf(p)
      normalslashes(p).split("/").reject(&:empty?).last || p
    end
  end

  module Arg
    # The raw argument list. The harness fills this in (argv); element 0 is the
    # executable, mirroring stand-alone Lua's `arg` table.
    @args = []

    class << self
      attr_accessor :args
    end

    # The lowest-indexed argument: the executable path.
    def self.low
      args[0] || (Love.const_defined?(:ARG0) ? Love::ARG0 : "")
    end

    # The arguments passed through to Love.load (everything after the program
    # name; option flags would be filtered here in the full parser).
    def self.parse_game_arguments(a)
      (a || []).drop(1)
    end
  end
end
