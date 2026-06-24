# Embed a text script as a C string in a header, robustly (hex byte array, so no
# escaping / raw-string-delimiter pitfalls -- unlike Lua's in-file R"luastring"
# trick, which relies on `--` comments and doesn't translate to Ruby).
#
# Used by the CMake builds to bake the ported boot scripts (arg.rb / callbacks.rb
# / boot.rb / nogame.rb) into the binary, so a shipped love doesn't read them
# from disk. Reusable verbatim by the main CMakeLists.txt migration.
#
#   cmake -DIN=<script> -DOUT=<header> -DVAR=<symbol> -P embed_script.cmake
#
# Emits:  static const char <symbol>[] = { 0x.., ..., 0x00 };  (NUL-terminated)

if(NOT IN OR NOT OUT OR NOT VAR)
	message(FATAL_ERROR "embed_script.cmake requires -DIN= -DOUT= -DVAR=")
endif()

file(READ "${IN}" hex HEX)
string(REGEX MATCHALL "[0-9a-f][0-9a-f]" bytes "${hex}")

set(arr "")
foreach(b ${bytes})
	string(APPEND arr "0x${b},")
endforeach()

# unsigned char so UTF-8 bytes (>127, e.g. the "Ö" in LÖVE) don't narrow; the
# consumer casts to const char* for mrb_load_string. NUL-terminated.
file(WRITE "${OUT}"
	"// Generated from ${IN} by embed_script.cmake. Do not edit.\n"
	"static const unsigned char ${VAR}[] = {${arr}0x00};\n")
