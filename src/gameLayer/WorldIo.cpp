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
				placement.size.x = entry.value("width", 0);
				placement.size.y = entry.value("height", 0);
				placement.flags = entry.value("flags", 0);

				if (placement.name.empty() || placement.size.x <= 0 || placement.size.y <= 0)
				{
					continue;
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
				{"width", placement.size.x},
				{"height", placement.size.y},
				{"flags", placement.flags}
			});
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
