#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <unordered_map>

// Stores a world-level connection target for one door inside a placed room.
struct WorldDoorLink
{
	std::string levelName = {};
	std::string doorName = {};
};

struct WorldLevelPlacement
{
	std::string name = {};
	glm::vec2 position = {}; // top-left corner in the virtual world
	glm::ivec2 size = {}; // runtime room size loaded from the room file
	int flags = 0;
	std::unordered_map<std::string, WorldDoorLink> doorLinks = {};

	glm::vec4 getRect() const
	{
		return {position.x, position.y, static_cast<float>(size.x), static_cast<float>(size.y)};
	}
};

struct WorldData
{
	glm::vec4 bounds = {-500.f, -500.f, 1000.f, 1000.f};
	std::unordered_map<std::string, WorldLevelPlacement> levels;
};

struct WorldIoResult
{
	bool success = false;
	std::string message = {};
};

// Saves a single world layout file that places room files in a larger virtual map.
std::string getWorldFilePath();
WorldIoResult loadWorldData(WorldData &world);
WorldIoResult saveWorldData(WorldData const &world);
WorldIoResult renameLevelReferencesInWorld(char const *oldLevelName, char const *newLevelName);
WorldIoResult renameDoorReferencesInWorld(char const *levelName, char const *oldDoorName, char const *newDoorName);
WorldIoResult deleteDoorReferencesInWorld(char const *levelName, char const *doorName);
