/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file openttd_folder.cpp Test copying the settings and saves from OpenTTD's folder into Wira Bandar's. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../fileio_func.h"

#include <filesystem>
#include <fstream>

#include "../safeguards.h"

namespace fs = std::filesystem;

static void WriteFile(const fs::path &path, std::string_view text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path) << text;
}

static std::string ReadFile(const fs::path &path)
{
	std::ifstream in(path);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

TEST_CASE("CopyOpenTTDFolder")
{
	const fs::path base = fs::temp_directory_path() / "wirabandar-copy-test";
	fs::remove_all(base);
	const fs::path openttd = base / "OpenTTD";
	const fs::path wira = base / "Wira Bandar";
	const std::string from = openttd.string() + "/";
	const std::string to = wira.string() + "/";

	/* Nothing to copy while OpenTTD's folder has no settings. */
	WriteFile(openttd / "save" / "game.sav", "save");
	CHECK_FALSE(CopyOpenTTDFolder(from, to));
	CHECK_FALSE(fs::exists(wira / "save" / "game.sav"));

	/* The first start: settings and saves, not graphics or downloads. */
	WriteFile(openttd / "openttd.cfg", "settings");
	WriteFile(openttd / "hotkeys.cfg", "keys");
	WriteFile(openttd / "save" / "autosave" / "autosave0.sav", "auto");
	WriteFile(openttd / "baseset" / "opengfx.tar", "graphics");
	WriteFile(openttd / "content_download" / "newgrf" / "x.tar", "grf");
	CHECK(CopyOpenTTDFolder(from, to));
	CHECK(ReadFile(wira / "openttd.cfg") == "settings");
	CHECK(ReadFile(wira / "hotkeys.cfg") == "keys");
	CHECK(ReadFile(wira / "save" / "game.sav") == "save");
	CHECK(ReadFile(wira / "save" / "autosave" / "autosave0.sav") == "auto");
	CHECK_FALSE(fs::exists(wira / "baseset"));
	CHECK_FALSE(fs::exists(wira / "content_download"));
	/* Copied, not moved. */
	CHECK(ReadFile(openttd / "openttd.cfg") == "settings");
	CHECK(fs::exists(openttd / "save" / "game.sav"));

	/* Later starts copy nothing and keep what Wira Bandar has. */
	WriteFile(wira / "openttd.cfg", "changed");
	WriteFile(openttd / "save" / "newer.sav", "newer");
	CHECK_FALSE(CopyOpenTTDFolder(from, to));
	CHECK(ReadFile(wira / "openttd.cfg") == "changed");
	CHECK_FALSE(fs::exists(wira / "save" / "newer.sav"));

	fs::remove_all(base);
}
