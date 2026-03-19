#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <unordered_map>

struct WorldLevelPlacement
{
	std::string name = {};
	glm::vec2 position = {}; // top-left corner in the virtual world
	glm::ivec2 size = {};
	int flags = 0;

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
