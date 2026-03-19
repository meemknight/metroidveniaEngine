#pragma once

#include "room.h"

#include <string>
#include <vector>

struct RoomFileEntry
{
	std::string name = {};
};

struct RoomFilesListing
{
	std::vector<RoomFileEntry> files;
	std::string error = {};
};

struct RoomIoResult
{
	bool success = false;
	std::string levelName = {};
	std::string message = {};
};

// Loads and saves room tile data as simple JSON files inside resources/levels.
std::string getRoomFilesFolder();
RoomIoResult ensureRoomFilesFolder();
RoomFilesListing listRoomFiles();
bool roomFileExists(char const *levelName);
RoomIoResult saveRoomToFile(Room const &room, char const *levelName);
RoomIoResult loadRoomFromFile(Room &room, char const *levelName);
RoomIoResult renameRoomFile(char const *oldLevelName, char const *newLevelName);
RoomIoResult deleteRoomFile(char const *levelName);
