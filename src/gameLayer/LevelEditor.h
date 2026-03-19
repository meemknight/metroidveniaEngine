#pragma once

#include "room.h"

#include <glm/vec2.hpp>
#include <platformInput.h>
#include <gl2d/gl2d.h>
#include <string>

// Simple in-engine level editor for painting, erasing and filling tile blocks.
struct LevelEditor
{
	enum Tools
	{
		noneTool = 0,
		brushTool,
		rectTool,
		measureTool,
	};

	struct EditorTuning
	{
		float cameraZoom = 32.f;
		float cameraMoveSpeed = 28.f;
		bool showGrid = true;
		float gridAlpha = 0.22f;
		float gridLineWidth = 0.05f;
	};

	enum PendingFileAction
	{
		noPendingFileAction = 0,
		loadSelectedFileAction,
		createNewFileAction,
	};

	void init();
	void cleanup();
	void enter(Room &room, gl2d::Renderer2D &renderer);
	void update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer, Room &room,
		bool gameViewHovered, bool gameViewFocused);

	glm::vec2 getViewSize(gl2d::Renderer2D &renderer);
	glm::vec2 getViewCenter(gl2d::Renderer2D &renderer);
	void setViewCenter(glm::vec2 center, gl2d::Renderer2D &renderer);
	glm::vec2 screenToWorld(glm::vec2 screenPos, gl2d::Renderer2D &renderer);
	void syncPendingRoomSize(Room &room);
	void focusRoom(Room &room, gl2d::Renderer2D &renderer);
	void updateHoveredTile(platform::Input &input, gl2d::Renderer2D &renderer, Room &room);
	void updateCamera(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer, Room &room);
	void updateShortcuts(platform::Input &input, Room &room, bool gameViewFocused);
	void updateTools(platform::Input &input, Room &room, bool gameViewHovered);
	void setBlock(Room &room, int x, int y, bool solid);
	void fillRect(Room &room, glm::ivec2 a, glm::ivec2 b, bool solid);
	void resizeRoom(Room &room, int newSizeX, int newSizeY);
	glm::vec4 getRectPreview(glm::ivec2 a, glm::ivec2 b);
	void clampCamera(Room &room, gl2d::Renderer2D &renderer);
	void drawRoom(Room &room, gl2d::Renderer2D &renderer);
	void drawGrid(Room &room, gl2d::Renderer2D &renderer);
	void drawHoveredTile(gl2d::Renderer2D &renderer);
	void drawRectPreview(gl2d::Renderer2D &renderer);
	void drawMeasureText(gl2d::Renderer2D &renderer);
	void drawWindow(Room &room, gl2d::Renderer2D &renderer);
	void drawLevelFilesWindow(Room &room, gl2d::Renderer2D &renderer);
	void drawUnsavedChangesWindow(Room &room, gl2d::Renderer2D &renderer);
	void saveCurrentLevel(Room &room);
	void loadSelectedLevel(Room &room, gl2d::Renderer2D &renderer);
	void reloadCurrentLevel(Room &room, gl2d::Renderer2D &renderer);
	void createNewLevel(Room &room, gl2d::Renderer2D &renderer);

	gl2d::Camera camera = {};
	gl2d::Font measureFont = {};
	EditorTuning tuning = {};

	int tool = measureTool;
	glm::ivec2 hoveredTile = {-1, -1};
	bool hoveredTileValid = false;
	glm::vec2 mouseScreenPosition = {};

	bool rectDragActive = false;
	bool rectDragPlacesSolid = true;
	glm::ivec2 rectDragStart = {};
	glm::ivec2 rectDragEnd = {};

	glm::ivec2 pendingRoomSize = {};
	bool cameraInitialized = false;

	// Simple file-management state for the JSON room files under resources/levels.
	std::string currentLevelName = {};
	std::string selectedLevelName = {};
	std::string pendingDeleteLevelName = {};
	std::string fileActionMessage = {};
	bool fileActionHasError = false;
	bool levelDirty = false;
	int pendingFileAction = noPendingFileAction;
	char newLevelName[128] = {};
	char renameName[128] = {};
	glm::ivec2 newLevelSize = {40, 24};
};
