#include "physics.h"

#include "room.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace
{
	bool aabb(const glm::vec4 &a, const glm::vec4 &b)
	{
		return ((a.x - b.x < b.z) && (b.x - a.x < a.z) &&
			(a.y - b.y < b.w) && (b.y - a.y < a.w));
	}
}

bool Transform2D::intersectPoint(glm::vec2 point, float delta) const
{
	glm::vec4 aabbRect = getAABB();
	aabbRect.x -= delta;
	aabbRect.y -= delta;
	aabbRect.z += 2 * delta;
	aabbRect.w += 2 * delta;

	return point.x >= aabbRect.x && point.x <= aabbRect.x + aabbRect.z &&
		point.y >= aabbRect.y && point.y <= aabbRect.y + aabbRect.w;
}

bool Transform2D::intersectTransform(Transform2D other, float delta) const
{
	glm::vec4 a = getAABB();
	glm::vec4 b = other.getAABB();

	a.x -= delta;
	a.y -= delta;
	a.z += 2 * delta;
	a.w += 2 * delta;

	b.x -= delta;
	b.y -= delta;
	b.z += 2 * delta;
	b.w += 2 * delta;

	return aabb(a, b);
}

void PhysicalEntity::resolveConstrains(Room &room)
{
	upTouch = false;
	downTouch = false;
	leftTouch = false;
	rightTouch = false;

	glm::vec2 &pos = transform.pos;
	float distance = glm::distance(lastPosition, pos);

	if (distance == 0.f)
	{
		return;
	}

	float GRANULARITY = 0.8f;

	if (distance <= GRANULARITY)
	{
		checkCollisionOnce(pos, room);
	}
	else
	{
		glm::vec2 newPos = lastPosition;
		glm::vec2 delta = pos - lastPosition;
		delta = glm::normalize(delta);
		delta *= GRANULARITY;

		do
		{
			newPos += delta;
			glm::vec2 posTest = newPos;
			checkCollisionOnce(newPos, room);

			if (newPos != posTest)
			{
				pos = newPos;
				goto end;
			}

		} while (glm::length((newPos + delta) - pos) > GRANULARITY);

		checkCollisionOnce(pos, room);
	}

end:

	if (pos.x - transform.w / 2.f < 0.f) { pos.x = transform.w / 2.f; }
	if (pos.x + transform.w / 2.f > room.size.x) { pos.x = room.size.x - transform.w / 2.f; }

	if (leftTouch && velocity.x < 0.f) { velocity.x = 0.f; }
	if (rightTouch && velocity.x > 0.f) { velocity.x = 0.f; }

	if (upTouch && velocity.y < 0.f) { velocity.y = 0.f; }
	if (downTouch && velocity.y > 0.f) { velocity.y = 0.f; }
}

void PhysicalEntity::checkCollisionOnce(glm::vec2 &pos, Room &room)
{
	glm::vec2 delta = pos - lastPosition;

	glm::vec2 newPos = performCollision(room, {pos.x, lastPosition.y}, {delta.x, 0.f});
	pos = performCollision(room, {newPos.x, pos.y}, {0.f, delta.y});
}

glm::vec2 PhysicalEntity::performCollision(Room &room, glm::vec2 pos, glm::vec2 delta)
{
	int minX = 0;
	int minY = 0;
	int maxX = room.size.x;
	int maxY = room.size.y;

	if (delta.x == 0.f && delta.y == 0.f) { return pos; }

	glm::vec2 dimensions = {transform.w, transform.h};

	minX = static_cast<int>(std::floor(pos.x - dimensions.x / 2.f - 1.f));
	maxX = static_cast<int>(std::ceil(pos.x + dimensions.x / 2.f + 1.f));

	minY = static_cast<int>(std::floor(pos.y - dimensions.y / 2.f - 1.f));
	maxY = static_cast<int>(std::ceil(pos.y + dimensions.y / 2.f + 1.f));

	minX = std::max(0, minX);
	minY = std::max(0, minY);
	maxX = std::min(room.size.x, maxX);
	maxY = std::min(room.size.y, maxY);

	for (int y = minY; y < maxY; y++)
	{
		for (int x = minX; x < maxX; x++)
		{
			if (room.getBlockUnsafe(x, y).isSolid())
			{
				Transform2D entity;
				entity.pos = pos;
				entity.w = dimensions.x;
				entity.h = dimensions.y;

				Transform2D block;
				block.pos = {x + 0.5f, y + 0.5f};
				block.w = 1.f;
				block.h = 1.f;

				if (entity.intersectTransform(block, -0.00005f))
				{
					if (delta.x != 0.f)
					{
						if (delta.x < 0.f)
						{
							leftTouch = true;
							pos.x = x + 1.f + dimensions.x / 2.f;
							return pos;
						}
						else
						{
							rightTouch = true;
							pos.x = x - dimensions.x / 2.f;
							return pos;
						}
					}
					else if (delta.y != 0.f)
					{
						if (delta.y < 0.f)
						{
							upTouch = true;
							pos.y = y + 1.f + dimensions.y / 2.f;
							return pos;
						}
						else
						{
							downTouch = true;
							pos.y = y - dimensions.y / 2.f;
							return pos;
						}
					}
				}
			}
		}
	}

	return pos;
}
