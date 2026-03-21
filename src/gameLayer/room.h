#pragma once

#include "door.h"
#include "zipline.h"

#include <glm/vec2.hpp>
#include <vector>

struct Block
{
	bool solid = false;
};

struct Room
{
	void create(int sizeX, int sizeY)
	{
		*this = {};
		blocks.resize(sizeX * sizeY);
		size = {sizeX, sizeY};
	}

	Block &getBlockUnsafe(int x, int y);
	const Block &getBlockUnsafe(int x, int y) const;

	Block *getBlockSafe(int x, int y);
	const Block *getBlockSafe(int x, int y) const;

	std::vector<Block> blocks;
	glm::ivec2 size = {};
	std::vector<Door> doors;
	std::vector<Zipline> ziplines;
};
