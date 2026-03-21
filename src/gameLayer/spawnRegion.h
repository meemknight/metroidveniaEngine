#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <vector>

// Spawn regions are simple editor-authored areas that share one spawn point.
// A single region can be built from multiple rectangles.
struct SpawnRegionRect
{
	glm::ivec2 position = {};
	glm::ivec2 size = {2, 2};

	glm::vec4 getRectF() const
	{
		return {
			static_cast<float>(position.x),
			static_cast<float>(position.y),
			static_cast<float>(size.x),
			static_cast<float>(size.y)
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

struct SpawnRegion
{
	std::vector<SpawnRegionRect> rects = {};
	glm::ivec2 spawnPosition = {};

	glm::vec4 getSpawnRectF() const
	{
		return {
			static_cast<float>(spawnPosition.x),
			static_cast<float>(spawnPosition.y),
			1.f,
			1.f
		};
	}
};
