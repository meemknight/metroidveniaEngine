#pragma once

#include "physics.h"

struct Player
{
	PhysicalEntity physics;
	float moveSpeed = 7.f;
	float smoothedMoveInput = 0.f;
	float coyoteTimer = 0.f;
	float jumpBufferTimer = 0.f;
	int wallGrabSide = 0;
	int lastWallGrabSide = 0;
	int rememberedWallInputSide = 0;
	float rememberedWallInputTimer = 0.f;
	float wallRegrabTimer = 0.f;
	float wallGrabLockTimer = 0.f;
	float wallJumpCarryVelocity = 0.f;
	int lastMoveDirection = 1;
	bool dashActive = false;
	bool airDashAvailable = true;
	bool jumpQueuedAfterDash = false;
	int dashDirection = 1;
	float dashTimer = 0.f;
	float dashCooldownTimer = 0.f;
	float dashStartX = 0.f;
	float dashPreviousOffset = 0.f;

	glm::vec2 getCenter() const
	{
		return physics.transform.getCenter();
	}
};
