#pragma once

#include "physics.h"

struct Player
{
	PhysicalEntity physics;
	float moveSpeed = 7.f;
	float smoothedMoveInput = 0.f;
	float coyoteTimer = 0.f;
	float jumpBufferTimer = 0.f;
	bool doubleJumpAvailable = true;
	bool glideArmedFromDoubleJump = false;
	int wallGrabSide = 0;
	int lastWallGrabSide = 0;
	int rememberedWallInputSide = 0;
	float rememberedWallInputTimer = 0.f;
	float wallRegrabTimer = 0.f;
	float wallGrabLockTimer = 0.f;
	float wallGrabHoldTimer = 0.f;
	// Small grace window after releasing a grab so a quick opposite-direction
	// input followed by jump can still wall jump cleanly.
	int wallJumpGraceSide = 0;
	float wallJumpGraceTimer = 0.f;
	bool wallHoldActive = false;
	float wallJumpCarryVelocity = 0.f;
	int lastMoveDirection = 1;
	// Attacks are short directional slashes with their own cooldown so the
	// gameplay code can render and test a live hitbox without an enemy system yet.
	bool attackActive = false;
	glm::ivec2 attackDirection = {1, 0};
	float attackTimer = 0.f;
	float attackCooldownTimer = 0.f;
	bool attackStartedOnGround = false;
	bool attackPogoConsumed = false;
	// Pogo bounces should keep their full scripted lift even if jump is not held.
	bool pogoBounceActive = false;
	bool dashActive = false;
	bool airDashAvailable = true;
	bool jumpQueuedAfterDash = false;
	int dashDirection = 1;
	float dashTimer = 0.f;
	float dashCooldownTimer = 0.f;
	float dashStartX = 0.f;
	float dashPreviousOffset = 0.f;
	// Wall-grab dash is a short upward burst that keeps the player latched to
	// the same wall instead of kicking them away like the regular dash does.
	bool wallGrabDashActive = false;
	float wallGrabDashTimer = 0.f;
	float wallGrabDashStartY = 0.f;
	float wallGrabDashPreviousOffset = 0.f;
	bool glideActive = false;
	float glideTimer = 0.f;
	bool ziplineActive = false;
	int ziplineIndex = -1;
	float ziplineDistance = 0.f;
	float ziplineSpeed = 0.f;
	float ziplineDetachTimer = 0.f;
	// Zipline dash is a short scripted burst that follows the rail direction
	// instead of the normal horizontal dash path.
	bool ziplineDashActive = false;
	int ziplineDashDirection = 1;
	float ziplineDashTimer = 0.f;
	float ziplineDashStartDistance = 0.f;
	bool wallClimbActive = false;
	int wallClimbSide = 0;
	float wallClimbTimer = 0.f;
	float wallClimbDuration = 0.f;
	glm::vec2 wallClimbStart = {};
	glm::vec2 wallClimbCorner = {};
	glm::vec2 wallClimbEnd = {};

	glm::vec2 getCenter() const
	{
		return physics.transform.getCenter();
	}
};
