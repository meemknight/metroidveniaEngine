#pragma once

#include <glm/vec2.hpp>
#include <string>
#include <vector>

constexpr int maxEntityShapePoints = 32;

enum PlayerAttackShapeId
{
	playerAttackShapeFront = 0,
	playerAttackShapeUp,
	playerAttackShapeDownAir,
	playerAttackShapeDownGround,
	playerAttackShapeWall,
	playerAttackShapeCount,
};

// Convex shapes are edited in the entity editor and later copied into fixed-size
// runtime buffers for gameplay collision tests.
struct EditableConvexShape
{
	std::vector<glm::vec2> points = {};
};

struct PlayerEntityShapes
{
	EditableConvexShape attackShapes[playerAttackShapeCount] = {};
};

struct EntityData
{
	PlayerEntityShapes player = {};
};

char const *getPlayerAttackShapeName(int shapeId);
EditableConvexShape &getPlayerAttackShape(EntityData &data, int shapeId);
EditableConvexShape const &getPlayerAttackShape(EntityData const &data, int shapeId);

EntityData makeDefaultEntityData();
bool validateEntityData(EntityData const &data, std::string &errorMessage);

std::string getEntityDataFilePath();
bool loadEntityData(EntityData &data, std::string *message = nullptr);
bool saveEntityData(EntityData const &data, std::string *message = nullptr);
