#include "RoomIo.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace
{
	namespace fs = std::filesystem;

	fs::path getRoomFilesFolderPath()
	{
		return fs::path(RESOURCES_PATH) / "levels";
	}

	std::string normalizeRoomFileName(char const *levelName)
	{
		std::string result = levelName ? levelName : "";

		if (result.size() >= 5 && result.substr(result.size() - 5) == ".json")
		{
			result.resize(result.size() - 5);
		}

		for (char &c : result)
		{
			unsigned char value = static_cast<unsigned char>(c);
			if (std::isalnum(value) || c == ' ' || c == '_' || c == '-')
			{
				continue;
			}

			c = '_';
		}

		while (!result.empty() && (result.back() == ' ' || result.back() == '.'))
		{
			result.pop_back();
		}

		while (!result.empty() && result.front() == ' ')
		{
			result.erase(result.begin());
		}

		if (result.empty())
		{
			result = "level";
		}

		return result;
	}

	fs::path getRoomFilePath(char const *levelName)
	{
		return getRoomFilesFolderPath() / (normalizeRoomFileName(levelName) + ".json");
	}
}

std::string getRoomFilesFolder()
{
	return getRoomFilesFolderPath().string();
}

RoomIoResult ensureRoomFilesFolder()
{
	RoomIoResult result = {};

	try
	{
		fs::create_directories(getRoomFilesFolderPath());
		result.success = true;
		result.message = "Level folder ready";
	}
	catch (std::exception const &e)
	{
		result.message = e.what();
	}

	return result;
}

RoomFilesListing listRoomFiles()
{
	RoomFilesListing result = {};

	try
	{
		fs::create_directories(getRoomFilesFolderPath());

		for (auto const &entry : fs::directory_iterator(getRoomFilesFolderPath()))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".json")
			{
				continue;
			}

			result.files.push_back({entry.path().stem().string()});
		}

		std::sort(result.files.begin(), result.files.end(),
			[](RoomFileEntry const &a, RoomFileEntry const &b)
			{
				return a.name < b.name;
			});
	}
	catch (std::exception const &e)
	{
		result.error = e.what();
	}

	return result;
}

bool roomFileExists(char const *levelName)
{
	try
	{
		return fs::exists(getRoomFilePath(levelName));
	}
	catch (...)
	{
		return false;
	}
}

RoomIoResult saveRoomToFile(Room const &room, char const *levelName)
{
	RoomIoResult result = ensureRoomFilesFolder();
	if (!result.success)
	{
		result.message = "Couldn't create levels folder: " + result.message;
		return result;
	}

	result.levelName = normalizeRoomFileName(levelName);

	try
	{
		nlohmann::json data = {
			{"width", room.size.x},
			{"height", room.size.y},
			{"blocks", nlohmann::json::array()}
		};

		for (Block const &block : room.blocks)
		{
			data["blocks"].push_back(block.solid ? 1 : 0);
		}

		std::ofstream file(getRoomFilePath(result.levelName.c_str()));
		if (!file.is_open())
		{
			result.message = "Couldn't open the level file for writing";
			return result;
		}

		file << data.dump(1, '\t');
		result.success = true;
		result.message = "Saved level";
	}
	catch (std::exception const &e)
	{
		result.message = e.what();
	}

	return result;
}

RoomIoResult loadRoomFromFile(Room &room, char const *levelName)
{
	RoomIoResult result = {};
	result.levelName = normalizeRoomFileName(levelName);

	try
	{
		std::ifstream file(getRoomFilePath(result.levelName.c_str()));
		if (!file.is_open())
		{
			result.message = "Couldn't open the level file";
			return result;
		}

		nlohmann::json data;
		file >> data;

		int width = data.value("width", 0);
		int height = data.value("height", 0);
		if (width <= 0 || height <= 0)
		{
			result.message = "Invalid room size in JSON";
			return result;
		}

		if (!data.contains("blocks") || !data["blocks"].is_array())
		{
			result.message = "Missing blocks array in JSON";
			return result;
		}

		auto const &blocks = data["blocks"];
		if (blocks.size() != static_cast<size_t>(width * height))
		{
			result.message = "Block count doesn't match width * height";
			return result;
		}

		room.create(width, height);
		for (int i = 0; i < width * height; i++)
		{
			if (blocks[i].is_boolean())
			{
				room.blocks[i].solid = blocks[i].get<bool>();
			}
			else
			{
				room.blocks[i].solid = blocks[i].get<int>() != 0;
			}
		}

		result.success = true;
		result.message = "Loaded level";
	}
	catch (std::exception const &e)
	{
		result.message = e.what();
	}

	return result;
}

RoomIoResult renameRoomFile(char const *oldLevelName, char const *newLevelName)
{
	RoomIoResult result = ensureRoomFilesFolder();
	if (!result.success)
	{
		result.message = "Couldn't create levels folder: " + result.message;
		return result;
	}

	std::string oldName = normalizeRoomFileName(oldLevelName);
	std::string newName = normalizeRoomFileName(newLevelName);
	result.levelName = newName;

	try
	{
		fs::path oldPath = getRoomFilePath(oldName.c_str());
		fs::path newPath = getRoomFilePath(newName.c_str());

		if (!fs::exists(oldPath))
		{
			result.message = "The selected level doesn't exist anymore";
			return result;
		}

		if (oldPath == newPath)
		{
			result.success = true;
			result.message = "Level already uses that name";
			return result;
		}

		if (fs::exists(newPath))
		{
			result.message = "A level with that name already exists";
			return result;
		}

		fs::rename(oldPath, newPath);
		result.success = true;
		result.message = "Renamed level";
	}
	catch (std::exception const &e)
	{
		result.message = e.what();
	}

	return result;
}

RoomIoResult deleteRoomFile(char const *levelName)
{
	RoomIoResult result = ensureRoomFilesFolder();
	if (!result.success)
	{
		result.message = "Couldn't create levels folder: " + result.message;
		return result;
	}

	result.levelName = normalizeRoomFileName(levelName);

	try
	{
		fs::path path = getRoomFilePath(result.levelName.c_str());
		if (!fs::exists(path))
		{
			result.message = "The selected level doesn't exist anymore";
			return result;
		}

		fs::remove(path);
		result.success = true;
		result.message = "Deleted level";
	}
	catch (std::exception const &e)
	{
		result.message = e.what();
	}

	return result;
}
