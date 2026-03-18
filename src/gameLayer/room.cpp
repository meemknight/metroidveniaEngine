#include "room.h"

Block &Room::getBlockUnsafe(int x, int y)
{
	return blocks[x + y * size.x];
}

const Block &Room::getBlockUnsafe(int x, int y) const
{
	return blocks[x + y * size.x];
}

Block *Room::getBlockSafe(int x, int y)
{
	if (x < 0 || y < 0 || x >= size.x || y >= size.y)
	{
		return nullptr;
	}

	return &getBlockUnsafe(x, y);
}

const Block *Room::getBlockSafe(int x, int y) const
{
	if (x < 0 || y < 0 || x >= size.x || y >= size.y)
	{
		return nullptr;
	}

	return &getBlockUnsafe(x, y);
}
