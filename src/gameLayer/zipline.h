#pragma once

#include <glm/vec2.hpp>

// Simple two-point traversal marker stored directly inside each room.
struct Zipline
{
	glm::ivec2 points[2] = {};

	glm::vec2 getPointCenter(int index) const
	{
		return glm::vec2(points[index]) + glm::vec2(0.5f, 0.5f);
	}
};
