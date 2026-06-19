/**
 * Copyright (c) 2006-2026 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

// mruby port of wrap_Filesystem.cpp + wrap_File.cpp + wrap_FileData.cpp.
//
// API shape: Love::Filesystem module functions, plus Love::File and
// Love::FileData object types. snake_case names, keyword arguments. Examples:
//
//   love.filesystem.write("save.txt", data)
//                            -> Love::Filesystem.write(name: "save.txt", data: data)
//   love.filesystem.getInfo("save.txt")
//                            -> Love::Filesystem.get_info(path: "save.txt")  (Hash or nil)
//   f = love.filesystem.openFile("save.txt", "r")
//                            -> f = Love::Filesystem.open_file(name: "save.txt", mode: "r")
//   f:read()                 -> f.read
//
// Functions returning multiple values in Lua (read -> contents, size) return a
// single Ruby value here (the string; its bytesize is the count). Iterators
// (lines) return arrays. Lua-loader-specific functions (load, require paths),
// CommonPath mounting, symlinks, fused/android settings are intentionally left
// for later passes.
// TODO(mruby) #fs-deferred: Lua-loader/require paths, CommonPath mounting,
// symlinks, fused/android settings, Data-based mounting (see PORTING.md §A)

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Data.h"
#include "common/int.h"
#include "Filesystem.h"
#include "File.h"
#include "FileData.h"
#include "physfs/Filesystem.h"

#include <algorithm>
#include <string>
#include <vector>

namespace love
{
namespace filesystem
{

#define instance() (Module::getInstance<Filesystem>(Module::M_FILESYSTEM))

// Doubles can't represent the full 64-bit int range; LÖVE clamps file sizes.
static const int64 MAX_SAFE_INT = 0x20000000000000LL;

// =========================================================================
// Love::FileData instance methods
// =========================================================================

static mrb_value fd_getString(mrb_state *mrb, mrb_value self)
{
	FileData *d = mrbx_checktype<FileData>(mrb, self);
	return mrb_str_new(mrb, (const char *) d->getData(), d->getSize());
}

static mrb_value fd_getSize(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, (int) mrbx_checktype<FileData>(mrb, self)->getSize());
}

static mrb_value fd_getFilename(mrb_state *mrb, mrb_value self)
{
	return mrbx_string(mrb, mrbx_checktype<FileData>(mrb, self)->getFilename());
}

static mrb_value fd_getExtension(mrb_state *mrb, mrb_value self)
{
	return mrbx_string(mrb, mrbx_checktype<FileData>(mrb, self)->getExtension());
}

static mrb_value fd_getName(mrb_state *mrb, mrb_value self)
{
	return mrbx_string(mrb, mrbx_checktype<FileData>(mrb, self)->getName());
}

static mrb_value fd_clone(mrb_state *mrb, mrb_value self)
{
	FileData *c = mrbx_checktype<FileData>(mrb, self)->clone();
	mrb_value out = mrbx_pushtype(mrb, c);
	c->release();
	return out;
}

static const MrbReg fd_functions[] =
{
	{ "get_string",    fd_getString,    MRB_ARGS_NONE() },
	{ "get_size",      fd_getSize,      MRB_ARGS_NONE() },
	{ "get_filename",  fd_getFilename,  MRB_ARGS_NONE() },
	{ "get_extension", fd_getExtension, MRB_ARGS_NONE() },
	{ "get_name",      fd_getName,      MRB_ARGS_NONE() },
	{ "clone",         fd_clone,        MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::File instance methods
// =========================================================================

static mrb_value f_open(mrb_state *mrb, mrb_value self)
{
	File *file = mrbx_checktype<File>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"mode"}, 1, v);
	std::string modestr = mrbx_checkstring(mrb, v[0]);

	File::Mode mode;
	if (!File::getConstant(modestr.c_str(), mode))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid file open mode: %s", modestr.c_str());

	bool ok = false;
	mrbx_catchexcept(mrb, [&]() { ok = file->open(mode); });
	return mrbx_boolean(mrb, ok);
}

static mrb_value f_close(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<File>(mrb, self)->close());
}

static mrb_value f_isOpen(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<File>(mrb, self)->isOpen());
}

static mrb_value f_getSize(mrb_state *mrb, mrb_value self)
{
	File *file = mrbx_checktype<File>(mrb, self);
	int64 size = -1;
	bool err = mrbx_catchexcept(mrb, [&]() { size = file->getSize(); });
	if (err || size < 0)
		return mrb_nil_value();
	return mrbx_integer(mrb, (int) size);
}

static mrb_value f_read(mrb_state *mrb, mrb_value self)
{
	File *file = mrbx_checktype<File>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"bytes"}, 0, v);

	FileData *d = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() {
		int64 size = mrb_undef_p(v[0]) ? file->getSize() : (int64) mrbx_checknumber(mrb, v[0]);
		d = file->read(size);
	});
	if (err || d == nullptr)
		return mrb_nil_value();

	mrb_value out = mrb_str_new(mrb, (const char *) d->getData(), d->getSize());
	d->release();
	return out;
}

static mrb_value f_write(mrb_state *mrb, mrb_value self)
{
	File *file = mrbx_checktype<File>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"data", "size"}, 1, v);
	std::string data = mrbx_checkstring(mrb, v[0]);
	size_t size = mrb_undef_p(v[1]) ? data.size() : (size_t) mrbx_checkint(mrb, v[1]);

	bool ok = false;
	mrbx_catchexcept(mrb, [&]() { ok = file->write(data.data(), size); });
	return mrbx_boolean(mrb, ok);
}

static mrb_value f_flush(mrb_state *mrb, mrb_value self)
{
	File *file = mrbx_checktype<File>(mrb, self);
	bool ok = false;
	mrbx_catchexcept(mrb, [&]() { ok = file->flush(); });
	return mrbx_boolean(mrb, ok);
}

static mrb_value f_isEOF(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<File>(mrb, self)->isEOF());
}

static mrb_value f_tell(mrb_state *mrb, mrb_value self)
{
	int64 pos = mrbx_checktype<File>(mrb, self)->tell();
	if (pos < 0 || pos >= MAX_SAFE_INT)
		return mrb_nil_value();
	return mrbx_integer(mrb, (int) pos);
}

static mrb_value f_seek(mrb_state *mrb, mrb_value self)
{
	File *file = mrbx_checktype<File>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"position"}, 1, v);
	double pos = mrbx_checknumber(mrb, v[0]);
	if (pos < 0.0 || pos >= 9007199254740992.0)
		return mrbx_boolean(mrb, false);
	return mrbx_boolean(mrb, file->seek((uint64) pos));
}

static mrb_value f_getMode(mrb_state *mrb, mrb_value self)
{
	File::Mode mode = mrbx_checktype<File>(mrb, self)->getMode();
	const char *str = nullptr;
	File::getConstant(mode, str);
	return mrbx_string(mrb, str ? str : "c");
}

static mrb_value f_getFilename(mrb_state *mrb, mrb_value self)
{
	return mrbx_string(mrb, mrbx_checktype<File>(mrb, self)->getFilename());
}

static mrb_value f_getExtension(mrb_state *mrb, mrb_value self)
{
	return mrbx_string(mrb, mrbx_checktype<File>(mrb, self)->getExtension());
}

// Returns all lines of the file as an array (the Lua API returns an iterator).
static mrb_value f_lines(mrb_state *mrb, mrb_value self)
{
	File *file = mrbx_checktype<File>(mrb, self);

	bool wasclosed = file->getMode() == File::MODE_CLOSED;
	if (file->getMode() != File::MODE_READ)
	{
		if (file->getMode() != File::MODE_CLOSED)
			file->close();
		bool ok = false;
		mrbx_catchexcept(mrb, [&]() { ok = file->open(File::MODE_READ); });
		if (!ok)
			mrb_raise(mrb, E_RUNTIME_ERROR, "Could not open file.");
	}

	mrb_value arr = mrb_ary_new(mrb);
	std::string line;
	char buf[1024];
	while (!file->isEOF())
	{
		int64 r = file->read(buf, sizeof(buf));
		if (r < 0)
			mrb_raise(mrb, E_RUNTIME_ERROR, "Could not read from file.");
		for (int64 i = 0; i < r; i++)
		{
			if (buf[i] == '\n')
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				mrb_ary_push(mrb, arr, mrb_str_new(mrb, line.data(), line.size()));
				line.clear();
			}
			else
				line.push_back(buf[i]);
		}
	}
	if (!line.empty())
	{
		if (line.back() == '\r')
			line.pop_back();
		mrb_ary_push(mrb, arr, mrb_str_new(mrb, line.data(), line.size()));
	}

	if (wasclosed)
		file->close();

	return arr;
}

static const MrbReg f_functions[] =
{
	{ "open",          f_open,         MRB_ARGS_KEY(1, 0) },
	{ "close",         f_close,        MRB_ARGS_NONE() },
	{ "is_open",       f_isOpen,       MRB_ARGS_NONE() },
	{ "get_size",      f_getSize,      MRB_ARGS_NONE() },
	{ "read",          f_read,         MRB_ARGS_KEY(1, 0) },
	{ "write",         f_write,        MRB_ARGS_KEY(2, 0) },
	{ "flush",         f_flush,        MRB_ARGS_NONE() },
	{ "is_eof",        f_isEOF,        MRB_ARGS_NONE() },
	{ "tell",          f_tell,         MRB_ARGS_NONE() },
	{ "seek",          f_seek,         MRB_ARGS_KEY(1, 0) },
	{ "lines",         f_lines,        MRB_ARGS_NONE() },
	{ "get_mode",      f_getMode,      MRB_ARGS_NONE() },
	{ "get_filename",  f_getFilename,  MRB_ARGS_NONE() },
	{ "get_extension", f_getExtension, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::Filesystem module functions
// =========================================================================

static mrb_value w_init(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"arg0"}, 0, v);
	std::string arg0 = mrbx_optstring(mrb, v[0], "");
	mrbx_catchexcept(mrb, [&]() { instance()->init(arg0.c_str()); });
	return mrb_nil_value();
}

static mrb_value w_setIdentity(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"name", "append_to_path"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);
	bool append = mrbx_optboolean(mrb, v[1], false);
	if (!instance()->setIdentity(name.c_str(), append))
		mrb_raise(mrb, E_RUNTIME_ERROR, "Could not set write directory.");
	return mrb_nil_value();
}

static mrb_value w_getIdentity(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_string(mrb, instance()->getIdentity());
}

static mrb_value w_setSource(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"path"}, 1, v);
	std::string path = mrbx_checkstring(mrb, v[0]);
	if (!instance()->setSource(path.c_str()))
		mrb_raise(mrb, E_RUNTIME_ERROR, "Could not set source.");
	return mrb_nil_value();
}

static mrb_value w_getSource(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_string(mrb, instance()->getSource());
}

static mrb_value w_mount(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"archive", "mountpoint", "append_to_path"}, 2, v);
	std::string archive = mrbx_checkstring(mrb, v[0]);
	std::string mountpoint = mrbx_checkstring(mrb, v[1]);
	bool append = mrbx_optboolean(mrb, v[2], false);
	return mrbx_boolean(mrb, instance()->mount(archive.c_str(), mountpoint.c_str(), append));
}

static mrb_value w_unmount(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"archive"}, 1, v);
	std::string archive = mrbx_checkstring(mrb, v[0]);
	return mrbx_boolean(mrb, instance()->unmount(archive.c_str()));
}

static mrb_value w_openFile(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"name", "mode"}, 2, v);
	std::string name = mrbx_checkstring(mrb, v[0]);
	std::string modestr = mrbx_checkstring(mrb, v[1]);

	File::Mode mode = File::MODE_CLOSED;
	if (!File::getConstant(modestr.c_str(), mode))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid file open mode: %s", modestr.c_str());

	File *t = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { t = instance()->openFile(name.c_str(), mode); });
	if (err || t == nullptr)
		return mrb_nil_value();

	mrb_value out = mrbx_pushtype(mrb, t);
	t->release();
	return out;
}

static mrb_value w_newFileData(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"contents", "name"}, 2, v);
	std::string contents = mrbx_checkstring(mrb, v[0]);
	std::string name = mrbx_checkstring(mrb, v[1]);

	FileData *t = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { t = instance()->newFileData(contents.data(), contents.size(), name.c_str()); });
	if (err || t == nullptr)
		return mrb_nil_value();

	mrb_value out = mrbx_pushtype(mrb, t);
	t->release();
	return out;
}

static mrb_value w_getWorkingDirectory(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_string(mrb, instance()->getWorkingDirectory());
}

static mrb_value w_getUserDirectory(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_string(mrb, instance()->getUserDirectory());
}

static mrb_value w_getAppdataDirectory(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_string(mrb, instance()->getAppdataDirectory());
}

static mrb_value w_getSaveDirectory(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_string(mrb, instance()->getSaveDirectory());
}

static mrb_value w_getRealDirectory(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"name"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);
	std::string dir;
	bool err = mrbx_catchexcept(mrb, [&]() { dir = instance()->getRealDirectory(name.c_str()); });
	return err ? mrb_nil_value() : mrbx_string(mrb, dir);
}

static mrb_value w_createDirectory(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"name"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);
	return mrbx_boolean(mrb, instance()->createDirectory(name.c_str()));
}

static mrb_value w_remove(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"name"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);
	return mrbx_boolean(mrb, instance()->remove(name.c_str()));
}

static mrb_value w_read(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"name", "size"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);
	int64 len = mrb_undef_p(v[1]) ? -1 : (int64) mrbx_checknumber(mrb, v[1]);

	FileData *data = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() {
		data = len >= 0 ? instance()->read(name.c_str(), len) : instance()->read(name.c_str());
	});
	if (err)
		return mrb_nil_value();
	if (data == nullptr)
		mrb_raisef(mrb, E_RUNTIME_ERROR, "Could not read file: %s", name.c_str());

	mrb_value out = mrb_str_new(mrb, (const char *) data->getData(), data->getSize());
	data->release();
	return out;
}

static mrb_value writeOrAppend(mrb_state *mrb, bool append)
{
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"name", "data", "size"}, 2, v);
	std::string name = mrbx_checkstring(mrb, v[0]);
	std::string data = mrbx_checkstring(mrb, v[1]);
	size_t len = mrb_undef_p(v[2]) ? data.size() : (size_t) mrbx_checkint(mrb, v[2]);

	bool err = mrbx_catchexcept(mrb, [&]() {
		if (append)
			instance()->append(name.c_str(), data.data(), len);
		else
			instance()->write(name.c_str(), data.data(), len);
	});
	return err ? mrb_nil_value() : mrbx_boolean(mrb, true);
}

static mrb_value w_write(mrb_state *mrb, mrb_value self)  { (void) self; return writeOrAppend(mrb, false); }
static mrb_value w_append(mrb_state *mrb, mrb_value self) { (void) self; return writeOrAppend(mrb, true); }

static mrb_value w_getDirectoryItems(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"dir"}, 1, v);
	std::string dir = mrbx_checkstring(mrb, v[0]);

	std::vector<std::string> items;
	instance()->getDirectoryItems(dir.c_str(), items);

	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) items.size());
	for (const std::string &it : items)
		mrb_ary_push(mrb, arr, mrbx_string(mrb, it));
	return arr;
}

// Returns every line of the file as an array (Lua returns an iterator).
static mrb_value w_lines(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"name"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);

	File *file = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { file = instance()->openFile(name.c_str(), File::MODE_READ); });
	if (err || file == nullptr)
		return mrb_nil_value();

	mrb_value fileobj = mrbx_pushtype(mrb, file);
	file->release();
	return f_lines(mrb, fileobj);
}

static mrb_value w_exists(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"path"}, 1, v);
	std::string path = mrbx_checkstring(mrb, v[0]);
	return mrbx_boolean(mrb, instance()->exists(path.c_str()));
}

static mrb_value w_getInfo(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"path", "type"}, 1, v);
	std::string path = mrbx_checkstring(mrb, v[0]);

	Filesystem::FileType filter = Filesystem::FILETYPE_MAX_ENUM;
	if (!mrb_undef_p(v[1]))
	{
		std::string typestr = mrbx_checkstring(mrb, v[1]);
		if (!Filesystem::getConstant(typestr.c_str(), filter))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid file type: %s", typestr.c_str());
	}

	Filesystem::Info info = {};
	if (!instance()->getInfo(path.c_str(), info))
		return mrb_nil_value();

	if (filter != Filesystem::FILETYPE_MAX_ENUM && info.type != filter)
		return mrb_nil_value();

	const char *typestr = nullptr;
	Filesystem::getConstant(info.type, typestr);

	mrb_value h = mrb_hash_new(mrb);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "type")), mrbx_string(mrb, typestr ? typestr : "other"));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "readonly")), mrbx_boolean(mrb, info.readonly));

	int64 size = std::min<int64>(info.size, MAX_SAFE_INT);
	if (size >= 0)
		mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "size")), mrbx_integer(mrb, (int) size));

	int64 modtime = std::min<int64>(info.modtime, MAX_SAFE_INT);
	if (modtime >= 0)
		mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "modtime")), mrbx_integer(mrb, (int) modtime));

	return h;
}

static const MrbReg functions[] =
{
	{ "init",                   w_init,               MRB_ARGS_KEY(1, 0) },
	{ "set_identity",           w_setIdentity,        MRB_ARGS_KEY(2, 0) },
	{ "get_identity",           w_getIdentity,        MRB_ARGS_NONE() },
	{ "set_source",             w_setSource,          MRB_ARGS_KEY(1, 0) },
	{ "get_source",             w_getSource,          MRB_ARGS_NONE() },
	{ "mount",                  w_mount,              MRB_ARGS_KEY(3, 0) },
	{ "unmount",                w_unmount,            MRB_ARGS_KEY(1, 0) },
	{ "open_file",              w_openFile,           MRB_ARGS_KEY(2, 0) },
	{ "new_file_data",          w_newFileData,        MRB_ARGS_KEY(2, 0) },
	{ "get_working_directory",  w_getWorkingDirectory, MRB_ARGS_NONE() },
	{ "get_user_directory",     w_getUserDirectory,   MRB_ARGS_NONE() },
	{ "get_appdata_directory",  w_getAppdataDirectory, MRB_ARGS_NONE() },
	{ "get_save_directory",     w_getSaveDirectory,   MRB_ARGS_NONE() },
	{ "get_real_directory",     w_getRealDirectory,   MRB_ARGS_KEY(1, 0) },
	{ "create_directory",       w_createDirectory,    MRB_ARGS_KEY(1, 0) },
	{ "remove",                 w_remove,             MRB_ARGS_KEY(1, 0) },
	{ "read",                   w_read,               MRB_ARGS_KEY(2, 0) },
	{ "write",                  w_write,              MRB_ARGS_KEY(3, 0) },
	{ "append",                 w_append,             MRB_ARGS_KEY(3, 0) },
	{ "get_directory_items",    w_getDirectoryItems,  MRB_ARGS_KEY(1, 0) },
	{ "lines",                  w_lines,              MRB_ARGS_KEY(1, 0) },
	{ "exists",                 w_exists,             MRB_ARGS_KEY(1, 0) },
	{ "get_info",               w_getInfo,            MRB_ARGS_KEY(2, 0) },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_filesystem: creates the physfs-backed module,
// registers Love::Filesystem plus the File and FileData types.
extern "C" void mrb_love_filesystem_init(mrb_state *mrb)
{
	Filesystem *inst = instance();
	if (inst == nullptr)
		inst = new physfs::Filesystem();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Filesystem";
	w.type = &Filesystem::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
	mrbx_register_type(mrb, File::type, f_functions);
	mrbx_register_type(mrb, FileData::type, fd_functions);
}

} // filesystem
} // love
