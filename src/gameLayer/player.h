#pragma once

#include <glm/vec2.hpp>

struct Player
{
	glm::vec2 position = {};
	glm::vec2 velocity = {};
	glm::vec2 size = {2.f, 3.f};

	bool grounded = false;
	float coyoteTimer = 0.f;
	float jumpBufferTimer = 0.f;

	glm::vec2 getCenter() const
	{
		return position + size * 0.5f;
	}
};
