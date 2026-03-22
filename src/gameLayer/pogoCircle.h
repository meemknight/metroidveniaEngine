#pragma once

#include <glm/vec2.hpp>

// Pogo circles are simple room markers with optional collision.
// They stay separate from tile blocks so gameplay can treat them as round shapes.
struct PogoCircle
{
	glm::vec2 center = {};
	float radius = 1.f;
	bool collisionEnabled = false;

	glm::vec2 getResizeHandle() const
	{
		return center + glm::vec2(radius, 0.f);
	}
};
