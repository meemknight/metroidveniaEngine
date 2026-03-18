#pragma once

#include <cmath>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <glm/geometric.hpp>

struct Room;

struct Transform2D
{
	glm::vec2 pos = {}; // center
	float w = 0.f;
	float h = 0.f;

	glm::vec2 getCenter() const { return {pos.x, pos.y}; }
	glm::vec2 getTop() const { return {pos.x, pos.y - h * 0.5f}; }
	glm::vec2 getBottom() const { return {pos.x, pos.y + h * 0.5f}; }
	glm::vec2 getLeft() const { return {pos.x - w * 0.5f, pos.y}; }
	glm::vec2 getRight() const { return {pos.x + w * 0.5f, pos.y}; }
	glm::vec2 getTopLeft() const { return {pos.x - w * 0.5f, pos.y - h * 0.5f}; }
	glm::vec2 getTopRight() const { return {pos.x + w * 0.5f, pos.y - h * 0.5f}; }
	glm::vec2 getBottomLeft() const { return {pos.x - w * 0.5f, pos.y + h * 0.5f}; }
	glm::vec2 getBottomRight() const { return {pos.x + w * 0.5f, pos.y + h * 0.5f}; }

	glm::vec4 getAABB() const
	{
		return {pos.x - w * 0.5f, pos.y - h * 0.5f, w, h};
	}

	bool intersectPoint(glm::vec2 point, float delta = 0.f) const;
	bool intersectTransform(Transform2D other, float delta = 0.f) const;
};

struct PhysicalEntity
{
	Transform2D transform;
	glm::vec2 lastPosition = {};

	glm::vec2 velocity = {};
	glm::vec2 acceleration = {};

	bool upTouch = false;
	bool downTouch = false;
	bool leftTouch = false;
	bool rightTouch = false;

	void applyGravity(float gravity = 20.f)
	{
		acceleration += glm::vec2{0.f, gravity};
	}

	void jump(float force)
	{
		if (downTouch)
		{
			velocity.y = -force;
		}
	}

	void teleport(glm::vec2 pos)
	{
		transform.pos = pos;
		lastPosition = pos;
	}

	void updateForces(float deltaTime, glm::vec2 drag = {0.01f, 0.f})
	{
		velocity += acceleration * deltaTime;
		transform.pos += velocity * deltaTime;
		velocity += glm::vec2{
			velocity.x * (-drag.x * deltaTime),
			velocity.y * (-drag.y * deltaTime)};

		if (glm::length(velocity) < 0.01f)
		{
			velocity = {};
		}

		acceleration = {};
	}

	void updateFinal()
	{
		lastPosition = transform.pos;
	}

	void resolveConstrains(Room &room);
	void checkCollisionOnce(glm::vec2 &pos, Room &room);
	glm::vec2 performCollision(Room &room, glm::vec2 pos, glm::vec2 delta);
};
