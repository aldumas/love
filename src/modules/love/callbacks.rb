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

# Ruby port of callbacks.lua: the default Love.run main loop, the event handler
# table, and the error handler.
#
# Game callbacks are public singleton methods on Love that the game defines,
# e.g. `def Love.load(args, raw); end`, `def Love.update(dt); end`,
# `def Love.draw; end`, `def Love.quit; end`. Boot checks for them with
# Love.respond_to?(:name). (Note: `load` collides with the private Kernel#load,
# but respond_to? only sees it once the game defines a *public* Love.load.)
#
# Modules not yet ported (graphics, window, ...) are detected with
# const_defined? and their branches skipped, exactly as the Lua version guards
# with `if love.graphics` etc. The event module IS ported: when present, the run
# loop pumps and polls Love::Event, dispatching each [:name, *args] through the
# handler table (and honoring :quit with the love.quit veto).

module Love
  class << self
    attr_reader :handlers
  end

  @quit_requested = false

  # Stand-in for an event-module "quit" until love.event is ported: a game can
  # call Love.quit! to ask the loop to stop.
  def self.quit!
    @quit_requested = true
  end

  def self.quit_requested?
    @quit_requested ||= false
  end

  # Forwards an event named :foo to the game's Love.foo callback if it defines
  # one. Used to build the handler table without repeating the respond_to? dance.
  def self.forward(name)
    ->(*a) { Love.send(name, *a) if Love.respond_to?(name) }
  end

  def self.create_handlers
    # The handler table forwards window/input events to the matching user
    # callback. :quit is handled specially in the run loop (it can be vetoed),
    # so its entry is just a no-op. Unknown events (anything without an entry)
    # are ignored -- the SDL backend can emit more than we forward.
    @handlers = {}
    @handlers[:quit]          = ->(*) { }
    [:keypressed, :keyreleased, :textinput,
     :mousepressed, :mousereleased, :mousemoved, :wheelmoved,
     :focus, :resize].each { |name| @handlers[name] = forward(name) }
  end

  # Default main loop. Returns a callable run once per frame by the boot Fiber:
  # it returns nil to keep looping, or an integer exit code to stop.
  def self.run
    Love.load(parsed_game_arguments, raw_game_arguments) if Love.respond_to?(:load)

    # Don't let the first frame's dt include the time taken by Love.load.
    Timer.step if const_defined?(:Timer)

    lambda do
      # Process events. With love.event ported, pump the OS queue and dispatch
      # each event through the handler table; :quit can be vetoed by Love.quit.
      # Without it, fall back to the Love.quit! stand-in.
      if const_defined?(:Event)
        Event.pump
        # Collect an exit code rather than `return`ing from inside the block:
        # a block's `return` targets its defining method, which has already
        # exited by the time the boot Fiber calls this lambda.
        exit_code = nil
        Event.poll.each do |name, *args|
          if name == :quit
            unless Love.respond_to?(:quit) && Love.quit
              exit_code = args[0] || 0
              break
            end
          else
            h = @handlers[name]
            h.call(*args) if h
          end
        end
        return exit_code unless exit_code.nil?
      elsif quit_requested?
        vetoed = Love.respond_to?(:quit) && Love.quit
        return 0 unless vetoed
        @quit_requested = false # quit was vetoed by the game
      end

      # Update dt and run update().
      dt = const_defined?(:Timer) ? Timer.step : 0.0
      Love.update(dt) if Love.respond_to?(:update)

      # Draw. Real LÖVE gates this behind love.graphics.isActive(); with no
      # graphics module ported, we call draw directly so the loop is observable.
      if const_defined?(:Graphics)
        g = Love::Graphics
        if g.active?
          g.origin
          g.clear(*g.background_color)
          Love.draw if Love.respond_to?(:draw)
          g.present
        end
      elsif Love.respond_to?(:draw)
        Love.draw
      end

      Timer.sleep(seconds: 0.001) if const_defined?(:Timer)
      nil
    end
  end

  # Prints an error and its backtrace. Real LÖVE also renders a blue error
  # screen via graphics; that branch is omitted until graphics is ported.
  def self.error_handler(msg)
    puts "Error: #{msg}"
    if msg.respond_to?(:backtrace) && msg.backtrace
      puts msg.backtrace.reject { |l| l.include?("boot.rb") }.join("\n")
    end
    nil
  end

  class << self
    alias_method :errhand, :error_handler
  end
end
