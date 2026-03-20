#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <string>

// Simple per-room connector rectangle used by the level editor and world view.
// Doors also remember the tile where the player should appear after using them.
struct Door
{
	std::string name = {};
	glm::ivec2 position = {};
	glm::ivec2 size = {2, 3};
	glm::ivec2 playerSpawnPosition = {};

	glm::ivec4 getRect() const
	{
		return {position.x, position.y, size.x, size.y};
	}

	glm::vec4 getRectF() const
	{
		return {
			static_cast<float>(position.x),
			static_cast<float>(position.y),
			static_cast<float>(size.x),
			static_cast<float>(size.y)
		};
	}

	glm::vec4 getPlayerSpawnRectF() const
	{
		return {
			static_cast<float>(playerSpawnPosition.x),
			static_cast<float>(playerSpawnPosition.y),
			1.f,
			1.f
		};
	}

	bool contains(glm::vec2 point) const
	{
		return
			point.x >= position.x &&
			point.y >= position.y &&
			point.x <= position.x + size.x &&
			point.y <= position.y + size.y;
	}
};
