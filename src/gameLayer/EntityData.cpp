#include "EntityData.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace
{
	namespace fs = std::filesystem;

	fs::path getEntityDataFolderPath()
	{
		return fs::path(RESOURCES_PATH) / "entities";
	}

	fs::path getEntityDataJsonPath()
	{
		return getEntityDataFolderPath() / "entityShapes.json";
	}

	EditableConvexShape makeShape(std::initializer_list<glm::vec2> points)
	{
		EditableConvexShape shape = {};
		shape.points.assign(points.begin(), points.end());
		return shape;
	}

	nlohmann::json shapeToJson(EditableConvexShape const &shape)
	{
		nlohmann::json points = nlohmann::json::array();
		for (glm::vec2 const &point : shape.points)
		{
			points.push_back({
				{"x", point.x},
				{"y", point.y}
			});
		}

		return points;
	}

	bool readShapeFromJson(nlohmann::json const &jsonShape, EditableConvexShape &shape)
	{
		if (!jsonShape.is_array())
		{
			return false;
		}

		EditableConvexShape loadedShape = {};
		for (auto const &pointEntry : jsonShape)
		{
			if (!pointEntry.is_object())
			{
				return false;
			}

			loadedShape.points.push_back({
				pointEntry.value("x", 0.f),
				pointEntry.value("y", 0.f)
			});
		}

		shape = loadedShape;
		return true;
	}
}

char const *getPlayerAttackShapeName(int shapeId)
{
	switch (shapeId)
	{
		case playerAttackShapeUp: return "Up";
		case playerAttackShapeDownAir: return "Down Air";
		case playerAttackShapeDownGround: return "Down Ground";
		case playerAttackShapeWall: return "Wall";
		case playerAttackShapeFront:
		default:
			return "Front";
	}
}

EditableConvexShape &getPlayerAttackShape(EntityData &data, int shapeId)
{
	if (shapeId < 0 || shapeId >= playerAttackShapeCount)
	{
		shapeId = playerAttackShapeFront;
	}

	return data.player.attackShapes[shapeId];
}

EditableConvexShape const &getPlayerAttackShape(EntityData const &data, int shapeId)
{
	if (shapeId < 0 || shapeId >= playerAttackShapeCount)
	{
		shapeId = playerAttackShapeFront;
	}

	return data.player.attackShapes[shapeId];
}

EntityData makeDefaultEntityData()
{
	EntityData data = {};

	// These are the built-in right-facing player slash shapes.
	data.player.attackShapes[playerAttackShapeFront] = makeShape({
		{1.00f, -1.20f},
		{2.70f, -1.65f},
		{4.90f, -1.05f},
		{5.25f,  0.00f},
		{4.90f,  1.05f},
		{2.70f,  1.65f},
		{1.00f,  1.20f},
	});

	data.player.attackShapes[playerAttackShapeUp] = makeShape({
		{-1.05f, -2.55f},
		{ 0.20f, -5.95f},
		{ 1.60f, -5.10f},
		{ 2.05f, -3.40f},
		{ 1.20f, -1.95f},
		{-0.35f, -1.80f},
		{-1.45f, -2.80f},
	});

	data.player.attackShapes[playerAttackShapeDownAir] = makeShape({
		{-1.45f, 2.10f},
		{-0.20f, 5.95f},
		{ 1.05f, 5.55f},
		{ 1.75f, 3.55f},
		{ 1.25f, 1.90f},
		{-0.10f, 1.35f},
		{-1.20f, 1.70f},
	});

	data.player.attackShapes[playerAttackShapeDownGround] = makeShape({
		{-0.90f, 2.15f},
		{ 0.10f, 3.85f},
		{ 1.10f, 3.45f},
		{ 1.30f, 2.55f},
		{ 0.55f, 1.85f},
		{-0.55f, 1.85f},
		{-1.20f, 2.40f},
	});

	data.player.attackShapes[playerAttackShapeWall] = makeShape({
		{0.65f, -1.70f},
		{2.30f, -1.95f},
		{4.15f, -1.05f},
		{4.45f,  0.00f},
		{4.15f,  1.05f},
		{2.30f,  1.95f},
		{0.65f,  1.70f},
	});

	return data;
}

bool validateEntityData(EntityData const &data, std::string &errorMessage)
{
	for (int shapeId = 0; shapeId < playerAttackShapeCount; shapeId++)
	{
		EditableConvexShape const &shape = data.player.attackShapes[shapeId];
		if (shape.points.size() < 3)
		{
			errorMessage = std::string(getPlayerAttackShapeName(shapeId)) + " needs at least 3 points";
			return false;
		}

		if (shape.points.size() > maxEntityShapePoints)
		{
			errorMessage = std::string(getPlayerAttackShapeName(shapeId)) + " has too many points";
			return false;
		}
	}

	return true;
}

std::string getEntityDataFilePath()
{
	return getEntityDataJsonPath().string();
}

bool loadEntityData(EntityData &data, std::string *message)
{
	try
	{
		fs::create_directories(getEntityDataFolderPath());

		if (!fs::exists(getEntityDataJsonPath()))
		{
			data = makeDefaultEntityData();
			if (message)
			{
				*message = "No entity shape file yet. Save to create one.";
			}
			return true;
		}

		std::ifstream file(getEntityDataJsonPath());
		if (!file.is_open())
		{
			if (message)
			{
				*message = "Couldn't open entityShapes.json";
			}
			return false;
		}

		nlohmann::json jsonData;
		file >> jsonData;

		EntityData loadedData = makeDefaultEntityData();
		if (jsonData.contains("player") && jsonData["player"].is_object())
		{
			auto const &playerJson = jsonData["player"];
			if (playerJson.contains("attackShapes") && playerJson["attackShapes"].is_object())
			{
				auto const &attackJson = playerJson["attackShapes"];
				readShapeFromJson(attackJson.value("front", nlohmann::json::array()),
					loadedData.player.attackShapes[playerAttackShapeFront]);
				readShapeFromJson(attackJson.value("up", nlohmann::json::array()),
					loadedData.player.attackShapes[playerAttackShapeUp]);
				readShapeFromJson(attackJson.value("downAir", nlohmann::json::array()),
					loadedData.player.attackShapes[playerAttackShapeDownAir]);
				readShapeFromJson(attackJson.value("downGround", nlohmann::json::array()),
					loadedData.player.attackShapes[playerAttackShapeDownGround]);
				readShapeFromJson(attackJson.value("wall", nlohmann::json::array()),
					loadedData.player.attackShapes[playerAttackShapeWall]);
			}
		}

		std::string validationError = {};
		if (!validateEntityData(loadedData, validationError))
		{
			if (message)
			{
				*message = validationError;
			}
			return false;
		}

		data = loadedData;
		if (message)
		{
			*message = "Loaded entity shapes";
		}
		return true;
	}
	catch (std::exception const &e)
	{
		if (message)
		{
			*message = e.what();
		}
		return false;
	}
}

bool saveEntityData(EntityData const &data, std::string *message)
{
	std::string validationError = {};
	if (!validateEntityData(data, validationError))
	{
		if (message)
		{
			*message = validationError;
		}
		return false;
	}

	try
	{
		fs::create_directories(getEntityDataFolderPath());

		nlohmann::json jsonData = {
			{"player", {
				{"attackShapes", {
					{"front", shapeToJson(data.player.attackShapes[playerAttackShapeFront])},
					{"up", shapeToJson(data.player.attackShapes[playerAttackShapeUp])},
					{"downAir", shapeToJson(data.player.attackShapes[playerAttackShapeDownAir])},
					{"downGround", shapeToJson(data.player.attackShapes[playerAttackShapeDownGround])},
					{"wall", shapeToJson(data.player.attackShapes[playerAttackShapeWall])}
				}}
			}}
		};

		std::ofstream file(getEntityDataJsonPath());
		if (!file.is_open())
		{
			if (message)
			{
				*message = "Couldn't open entityShapes.json for writing";
			}
			return false;
		}

		file << jsonData.dump(1, '\t');
		if (message)
		{
			*message = "Saved entity shapes";
		}
		return true;
	}
	catch (std::exception const &e)
	{
		if (message)
		{
			*message = e.what();
		}
		return false;
	}
}
