#include "RoomIo.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>

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

	std::string trimWhitespace(std::string text)
	{
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
		{
			text.erase(text.begin());
		}

		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
		{
			text.pop_back();
		}

		return text;
	}

	bool validateRoomDoors(Room const &room, std::string &errorMessage)
	{
		std::unordered_set<std::string> usedNames;

		for (Door const &door : room.doors)
		{
			std::string trimmedName = trimWhitespace(door.name);
			if (trimmedName.empty())
			{
				errorMessage = "Door names can't be empty";
				return false;
			}

			if (!usedNames.insert(trimmedName).second)
			{
				errorMessage = "Door names must be unique";
				return false;
			}

			if (door.size.x <= 0 || door.size.y <= 0)
			{
				errorMessage = "Door sizes must stay above zero";
				return false;
			}

			if (door.playerSpawnPosition.x < 0 || door.playerSpawnPosition.y < 0 ||
				door.playerSpawnPosition.x >= room.size.x ||
				door.playerSpawnPosition.y >= room.size.y)
			{
				errorMessage = "Door player spawn positions must stay inside the room";
				return false;
			}
		}

		return true;
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
			{"blocks", nlohmann::json::array()},
			{"doors", nlohmann::json::array()}
		};

		if (!validateRoomDoors(room, result.message))
		{
			return result;
		}

		for (Block const &block : room.blocks)
		{
			data["blocks"].push_back(block.solid ? 1 : 0);
		}

		for (Door const &door : room.doors)
		{
			data["doors"].push_back({
				{"name", trimWhitespace(door.name)},
				{"x", door.position.x},
				{"y", door.position.y},
				{"width", door.size.x},
				{"height", door.size.y},
				{"playerSpawnX", door.playerSpawnPosition.x},
				{"playerSpawnY", door.playerSpawnPosition.y}
			});
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

		room.doors.clear();
		if (data.contains("doors"))
		{
			if (!data["doors"].is_array())
			{
				result.message = "Doors must be stored as a JSON array";
				return result;
			}

			std::unordered_set<std::string> usedNames;
			for (auto const &doorData : data["doors"])
			{
				if (!doorData.is_object())
				{
					result.message = "Door entries must be JSON objects";
					return result;
				}

				Door door = {};
				door.name = trimWhitespace(doorData.value("name", ""));
				door.position = {
					doorData.value("x", 0),
					doorData.value("y", 0)
				};
				door.size = {
					doorData.value("width", 1),
					doorData.value("height", 1)
				};
				door.playerSpawnPosition = door.position;
				if (doorData.contains("playerSpawnX") || doorData.contains("playerSpawnY"))
				{
					door.playerSpawnPosition = {
						doorData.value("playerSpawnX", door.position.x),
						doorData.value("playerSpawnY", door.position.y)
					};
				}

				if (door.name.empty())
				{
					result.message = "Doors need a non-empty name";
					return result;
				}

				if (!usedNames.insert(door.name).second)
				{
					result.message = "Door names must be unique";
					return result;
				}

				if (door.size.x <= 0 || door.size.y <= 0)
				{
					result.message = "Door sizes must stay above zero";
					return result;
				}

				if (door.playerSpawnPosition.x < 0 || door.playerSpawnPosition.y < 0 ||
					door.playerSpawnPosition.x >= room.size.x ||
					door.playerSpawnPosition.y >= room.size.y)
				{
					door.playerSpawnPosition = door.position;
				}

				room.doors.push_back(door);
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
