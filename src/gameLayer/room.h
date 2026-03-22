#pragma once

#include "door.h"
#include "spawnRegion.h"
#include "zipline.h"

#include <glm/vec2.hpp>
#include <vector>

// Room blocks are saved as tiny tile values so editors and gameplay can share
// one simple map format: 0 empty, 1 solid, 2 spike, 3 solid-without-wall-grab.
enum BlockType : unsigned char
{
	emptyBlock = 0,
	solidBlock = 1,
	spikeBlock = 2,
	noGrabBlock = 3,
};

inline BlockType getSafeBlockTypeFromInt(int value)
{
	switch (value)
	{
		case solidBlock: return solidBlock;
		case spikeBlock: return spikeBlock;
		case noGrabBlock: return noGrabBlock;
		default: return emptyBlock;
	}
}

struct Block
{
	BlockType type = emptyBlock;

	bool isSolid() const { return type == solidBlock || type == noGrabBlock; }
	bool isDanger() const { return type == spikeBlock; }
	bool isEmpty() const { return type == emptyBlock; }
	bool allowsWallActions() const { return type == solidBlock; }
	bool blocksWallActions() const { return type == noGrabBlock; }
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
	std::vector<SpawnRegion> spawnRegions;
	std::vector<Zipline> ziplines;
};
