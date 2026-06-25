# mruby build configuration for LÖVE (mruby-port branch).
#
# Produces a static libmruby.a that the LÖVE C++ runtime links against,
# replacing the previous Lua/LuaJIT dependency.
#
# CANONICAL COPY. The Makefile's `mruby` target installs this into the mruby
# tree (build_config/love.rb) before building, so the build is reproducible
# without the sibling mruby repo carrying its own. Pinned mruby: 4.0.0 @
# 9c4e7ea0f. Edit here, not in the mruby tree.
MRuby::Build.new do |conf|
  conf.toolchain

  # Core language + the gems LÖVE's runtime relies on. We deliberately keep
  # this lean: no stdio-only gems that assume a full POSIX environment beyond
  # what the host provides, but we do want eval (for require-style loading of
  # embedded boot scripts) and the standard data-structure gems.
  conf.gem core: 'mruby-compiler'      # compile Ruby source at runtime
  conf.gem core: 'mruby-eval'          # Kernel#eval / load embedded boot code
  conf.gem core: 'mruby-sprintf'       # Kernel#sprintf/format + String#%
  conf.gem core: 'mruby-io'            # Kernel#puts/print + IO (debug + boot output)
  conf.gem core: 'mruby-error'         # mrb_protect / exception helpers
  conf.gem core: 'mruby-metaprog'      # define_method, etc.
  conf.gem core: 'mruby-method'        # Method objects
  conf.gem core: 'mruby-object-ext'
  conf.gem core: 'mruby-kernel-ext'
  conf.gem core: 'mruby-symbol-ext'
  conf.gem core: 'mruby-string-ext'
  conf.gem core: 'mruby-array-ext'
  conf.gem core: 'mruby-hash-ext'
  conf.gem core: 'mruby-numeric-ext'
  conf.gem core: 'mruby-math'
  conf.gem core: 'mruby-enumerator'
  conf.gem core: 'mruby-enum-ext'
  conf.gem core: 'mruby-fiber'         # maps to LÖVE's coroutine-based boot loop
  conf.gem core: 'mruby-toplevel-ext'
  conf.gem core: 'mruby-class-ext'
  conf.gem core: 'mruby-proc-ext'

  # Build position-independent so the static lib can be linked into liblove
  # (a shared library on most platforms).
  conf.cc.flags << '-fPIC'
  conf.cxx.flags << '-fPIC'

  conf.enable_debug
end
