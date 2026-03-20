#include "WorldIo.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	fs::path getWorldFolderPath()
	{
		return fs::path(RESOURCES_PATH) / "world";
	}

	fs::path getWorldJsonPath()
	{
		return getWorldFolderPath() / "world.json";
	}

	WorldData getDefaultWorldData()
	{
		return {};
	}
}

std::string getWorldFilePath()
{
	return getWorldJsonPath().string();
}

WorldIoResult loadWorldData(WorldData &world)
{
	WorldIoResult result = {};

	try
	{
		fs::create_directories(getWorldFolderPath());

		if (!fs::exists(getWorldJsonPath()))
		{
			world = getDefaultWorldData();
			result.success = true;
			result.message = "No world file yet. Save to create one.";
			return result;
		}

		std::ifstream file(getWorldJsonPath());
		if (!file.is_open())
		{
			result.message = "Couldn't open world.json";
			return result;
		}

		nlohmann::json data;
		file >> data;

		world = getDefaultWorldData();
		if (data.contains("bounds") && data["bounds"].is_object())
		{
			auto const &bounds = data["bounds"];
			world.bounds.x = bounds.value("x", world.bounds.x);
			world.bounds.y = bounds.value("y", world.bounds.y);
			world.bounds.z = bounds.value("w", world.bounds.z);
			world.bounds.w = bounds.value("h", world.bounds.w);
		}

		if (data.contains("levels") && data["levels"].is_array())
		{
			for (auto const &entry : data["levels"])
			{
				WorldLevelPlacement placement = {};
				placement.name = entry.value("name", "");
				placement.position.x = entry.value("x", 0.f);
				placement.position.y = entry.value("y", 0.f);
				placement.size.x = entry.value("width", 0); // legacy fallback only
				placement.size.y = entry.value("height", 0); // legacy fallback only
				placement.flags = entry.value("flags", 0);

				if (placement.name.empty())
				{
					continue;
				}

				if (entry.contains("doorLinks") && entry["doorLinks"].is_array())
				{
					for (auto const &linkEntry : entry["doorLinks"])
					{
						std::string doorName = linkEntry.value("door", "");
						std::string linkedLevelName = linkEntry.value("linkedLevel", "");
						std::string linkedDoorName = linkEntry.value("linkedDoor", "");

						if (doorName.empty())
						{
							continue;
						}

						placement.doorLinks[doorName] = {
							linkedLevelName,
							linkedDoorName
						};
					}
				}

				world.levels[placement.name] = placement;
			}
		}

		result.success = true;
		result.message = "Loaded world";
	}
	catch (std::exception const &e)
	{
		result.message = e.what();
	}

	return result;
}

WorldIoResult saveWorldData(WorldData const &world)
{
	WorldIoResult result = {};

	try
	{
		fs::create_directories(getWorldFolderPath());

		nlohmann::json data = {
			{"bounds", {
				{"x", world.bounds.x},
				{"y", world.bounds.y},
				{"w", world.bounds.z},
				{"h", world.bounds.w}
			}},
			{"levels", nlohmann::json::array()}
		};

		std::vector<std::string> names;
		names.reserve(world.levels.size());
		for (auto const &it : world.levels)
		{
			names.push_back(it.first);
		}
		std::sort(names.begin(), names.end());

		for (auto const &name : names)
		{
			auto const &placement = world.levels.at(name);
			data["levels"].push_back({
				{"name", placement.name},
				{"x", placement.position.x},
				{"y", placement.position.y},
				{"flags", placement.flags},
				{"doorLinks", nlohmann::json::array()}
			});

			auto &levelJson = data["levels"].back();
			std::vector<std::string> doorNames;
			doorNames.reserve(placement.doorLinks.size());
			for (auto const &linkIt : placement.doorLinks)
			{
				doorNames.push_back(linkIt.first);
			}
			std::sort(doorNames.begin(), doorNames.end());

			for (auto const &doorName : doorNames)
			{
				auto const &doorLink = placement.doorLinks.at(doorName);
				levelJson["doorLinks"].push_back({
					{"door", doorName},
					{"linkedLevel", doorLink.levelName},
					{"linkedDoor", doorLink.doorName}
				});
			}
		}

		std::ofstream file(getWorldJsonPath());
		if (!file.is_open())
		{
			result.message = "Couldn't open world.json for writing";
			return result;
		}

		file << data.dump(1, '\t');
		result.success = true;
		result.message = "Saved world";
	}
	catch (std::exception const &e)
	{
		result.message = e.what();
	}

	return result;
}

WorldIoResult renameLevelReferencesInWorld(char const *oldLevelName, char const *newLevelName)
{
	WorldIoResult result = {};

	std::string oldLevelNameText = oldLevelName ? oldLevelName : "";
	std::string newLevelNameText = newLevelName ? newLevelName : "";

	if (oldLevelNameText.empty() || newLevelNameText.empty())
	{
		result.message = "Level rename sync needs valid level names";
		return result;
	}

	if (oldLevelNameText == newLevelNameText)
	{
		result.success = true;
		result.message = "Level references already match";
		return result;
	}

	try
	{
		fs::create_directories(getWorldFolderPath());
		if (!fs::exists(getWorldJsonPath()))
		{
			result.success = true;
			result.message = "No world file yet";
			return result;
		}

		WorldData world = {};
		result = loadWorldData(world);
		if (!result.success)
		{
			return result;
		}

		bool changed = false;

		auto renamedLevel = world.levels.find(oldLevelNameText);
		if (renamedLevel != world.levels.end())
		{
			if (world.levels.find(newLevelNameText) != world.levels.end())
			{
				result.message = "World already has a level with the renamed name";
				return result;
			}

			WorldLevelPlacement movedPlacement = renamedLevel->second;
			movedPlacement.name = newLevelNameText;
			world.levels.erase(renamedLevel);
			world.levels[newLevelNameText] = movedPlacement;
			changed = true;
		}

		for (auto &levelIt : world.levels)
		{
			for (auto &doorLinkIt : levelIt.second.doorLinks)
			{
				WorldDoorLink &doorLink = doorLinkIt.second;
				if (doorLink.levelName == oldLevelNameText)
				{
					doorLink.levelName = newLevelNameText;
					changed = true;
				}
			}
		}

		if (!changed)
		{
			result.success = true;
			result.message = "World had no matching level references";
			return result;
		}

		result = saveWorldData(world);
		if (result.success)
		{
			result.message = "Updated world level references";
		}
	}
	catch (std::exception const &e)
	{
		result.message = e.what();
	}

	return result;
}

WorldIoResult renameDoorReferencesInWorld(char const *levelName, char const *oldDoorName, char const *newDoorName)
{
	WorldIoResult result = {};

	std::string levelNameText = levelName ? levelName : "";
	std::string oldDoorNameText = oldDoorName ? oldDoorName : "";
	std::string newDoorNameText = newDoorName ? newDoorName : "";

	if (levelNameText.empty() || oldDoorNameText.empty() || newDoorNameText.empty())
	{
		result.message = "Door rename sync needs a valid level and door name";
		return result;
	}

	if (oldDoorNameText == newDoorNameText)
	{
		result.success = true;
		result.message = "Door references already match";
		return result;
	}

	try
	{
		fs::create_directories(getWorldFolderPath());
		if (!fs::exists(getWorldJsonPath()))
		{
			result.success = true;
			result.message = "No world file yet";
			return result;
		}

		WorldData world = {};
		result = loadWorldData(world);
		if (!result.success)
		{
			return result;
		}

		bool changed = false;

		auto renamedLevel = world.levels.find(levelNameText);
		if (renamedLevel != world.levels.end())
		{
			auto sourceLink = renamedLevel->second.doorLinks.find(oldDoorNameText);
			if (sourceLink != renamedLevel->second.doorLinks.end())
			{
				// World data keeps one link entry per source door, so move it to the new door name.
				WorldDoorLink link = sourceLink->second;
				renamedLevel->second.doorLinks.erase(sourceLink);
				renamedLevel->second.doorLinks[newDoorNameText] = link;
				changed = true;
			}
		}

		for (auto &levelIt : world.levels)
		{
			for (auto &doorLinkIt : levelIt.second.doorLinks)
			{
				WorldDoorLink &doorLink = doorLinkIt.second;
				if (doorLink.levelName == levelNameText &&
					doorLink.doorName == oldDoorNameText)
				{
					doorLink.doorName = newDoorNameText;
					changed = true;
				}
			}
		}

		if (!changed)
		{
			result.success = true;
			result.message = "World had no matching door references";
			return result;
		}

		result = saveWorldData(world);
		if (result.success)
		{
			result.message = "Updated world door references";
		}
	}
	catch (std::exception const &e)
	{
		result.message = e.what();
	}

	return result;
}

WorldIoResult deleteDoorReferencesInWorld(char const *levelName, char const *doorName)
{
	WorldIoResult result = {};

	std::string levelNameText = levelName ? levelName : "";
	std::string doorNameText = doorName ? doorName : "";

	if (levelNameText.empty() || doorNameText.empty())
	{
		result.message = "Door delete sync needs a valid level and door name";
		return result;
	}

	try
	{
		fs::create_directories(getWorldFolderPath());
		if (!fs::exists(getWorldJsonPath()))
		{
			result.success = true;
			result.message = "No world file yet";
			return result;
		}

		WorldData world = {};
		result = loadWorldData(world);
		if (!result.success)
		{
			return result;
		}

		bool changed = false;

		auto deletedLevel = world.levels.find(levelNameText);
		if (deletedLevel != world.levels.end())
		{
			changed |= deletedLevel->second.doorLinks.erase(doorNameText) > 0;
		}

		for (auto &levelIt : world.levels)
		{
			for (auto linkIt = levelIt.second.doorLinks.begin(); linkIt != levelIt.second.doorLinks.end(); )
			{
				if (linkIt->second.levelName == levelNameText &&
					linkIt->second.doorName == doorNameText)
				{
					linkIt = levelIt.second.doorLinks.erase(linkIt);
					changed = true;
				}
				else
				{
					++linkIt;
				}
			}
		}

		if (!changed)
		{
			result.success = true;
			result.message = "World had no matching door references";
			return result;
		}

		result = saveWorldData(world);
		if (result.success)
		{
			result.message = "Removed world door references";
		}
	}
	catch (std::exception const &e)
	{
		result.message = e.what();
	}

	return result;
}
