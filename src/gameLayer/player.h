#pragma once

#include "physics.h"

struct Player
{
	PhysicalEntity physics;
	float moveSpeed = 7.f;
	float coyoteTimer = 0.f;
	float jumpBufferTimer = 0.f;

	glm::vec2 getCenter() const
	{
		return physics.transform.getCenter();
	}
};
