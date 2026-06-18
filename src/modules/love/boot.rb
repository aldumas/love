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

# Ruby port of boot.lua. Defines Love.boot and Love.init, and builds the root
# coroutine (the "root of all calls") as an mruby Fiber stored in $LOVE_MAIN.
#
# In the real engine love.cpp turns the returned boot function into a Lua
# coroutine and resumes it once per frame. Here the C harness resumes the
# $LOVE_MAIN Fiber the same way (see harness.cpp) -- this is the Lua-coroutine
# -> mruby-Fiber mapping called out in the porting roadmap.
#
# arg.rb and callbacks.rb must be loaded before this file.

module Love
  # Sets up the filesystem: detect the executable, mount the game source, and
  # choose a save identity. (The harness provides no .love archive, so the
  # source-detection dance of boot.lua collapses to "use the working tree".)
  def self.boot
    raise "Love::Filesystem is required to boot" unless const_defined?(:Filesystem)

    self.raw_game_arguments = Arg.args
    self.parsed_game_arguments = Arg.parse_game_arguments(Arg.args)

    Filesystem.init(arg0: Arg.low)

    # Pick an identity from the executable name, then mount the save directory
    # (appended after the source so source files take precedence).
    identity = Path.leaf(Arg.low)
    dot = identity.rindex(".")        # strip a trailing extension
    identity = identity[0...dot] if dot
    identity = identity.gsub(".", "_")
    identity = "lovegame" if identity.empty?
    Filesystem.set_identity(name: identity, append_to_path: true)
  end

  # Default configuration, mirroring the subset of boot.lua's `c` table that is
  # meaningful without the window/graphics modules.
  def self.default_config
    {
      title: "Untitled",
      identity: false,
      append_identity: false,
      window: { width: 800, height: 600 },
      modules: {
        timer: true, math: true, filesystem: true,
        event: true, window: true, graphics: true,
      },
    }
  end

  # Builds the config, lets the game's Love.conf adjust it, "loads" modules
  # (already registered natively here), wires up handlers, takes the first
  # timestep, then loads the game's main file.
  def self.init
    # Run the game's main file first so its callbacks (including Love.conf) are
    # defined before we read config. Real LÖVE loads conf.lua early and main.lua
    # last; the single-file demo game merges both, so we eval it up front.
    # mruby has no file-level Kernel#load, so the harness hands us the source.
    eval($LOVE_GAME_SOURCE) if $LOVE_GAME_SOURCE && !$LOVE_GAME_SOURCE.empty?

    c = default_config
    Love.conf(c) if Love.respond_to?(:conf)

    # In the native build modules are registered from C; here we simply note
    # which ones are present. (Real boot.lua require()s each enabled module.)
    create_handlers

    # First timestep -- window creation / load happen around here in real LÖVE.
    Timer.step if const_defined?(:Timer)

    if const_defined?(:Filesystem)
      ident = c[:identity] || Filesystem.get_identity
      Filesystem.set_identity(name: ident, append_to_path: c[:append_identity]) unless ident.to_s.empty?
    end
  end
end

# The root of all calls. Equivalent to the function boot.lua returns: run
# boot -> init -> run inside an error boundary, then drive the per-frame
# function, yielding to the C host between frames.
$LOVE_MAIN = Fiber.new do
  result = nil
  frame = nil

  begin
    Love.boot
    Love.init
    frame = Love.run
  rescue => e
    Love.error_handler(e)
    result = 1
  end

  if result.nil?
    loop do
      retval = nil
      begin
        retval = frame.call
      rescue => e
        Love.error_handler(e)
        retval = 1
      end

      if retval
        result = retval
        break
      end

      Fiber.yield
    end
  end

  result
end
