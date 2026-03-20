#pragma once

#include "room.h"

#include <glm/vec2.hpp>
#include <platformInput.h>
#include <gl2d/gl2d.h>
#include <string>
#include <vector>

// Simple in-engine level editor for painting, erasing and filling tile blocks.
struct LevelEditor
{
	enum Tools
	{
		noneTool = 0,
		brushTool,
		rectTool,
		measureTool,
		doorTool,
		moveTool,
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

	struct PendingDoorRename
	{
		std::string oldName = {};
		std::string newName = {};
	};

	// The move tool keeps a detached snapshot of solid blocks and only writes it back on Enter.
	struct MoveSelection
	{
		bool active = false;
		bool previewActive = false;
		bool dragging = false;
		glm::ivec2 sourceStart = {};
		glm::ivec2 size = {};
		glm::ivec2 previewPosition = {};
		glm::ivec2 dragGrabOffset = {};
		std::vector<char> solidMask = {};
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
	glm::vec2 worldToScreen(glm::vec2 worldPos, gl2d::Renderer2D &renderer);
	void clearMoveSelection();
	void createMoveSelection(Room &room, glm::ivec2 a, glm::ivec2 b);
	bool moveSelectionContainsTile(glm::ivec2 tile) const;
	bool moveSelectionPreviewContainsTile(glm::ivec2 tile) const;
	void commitMoveSelection(Room &room);
	void clearDoorSelection();
	int getHoveredDoorIndex(Room &room, glm::vec2 mouseWorld);
	int getHoveredDoorSpawnIndex(Room &room, glm::vec2 mouseWorld);
	bool hoveredSelectedDoorResizeHandle(Room &room, glm::vec2 mouseWorld);
	std::string getNextDoorName(Room const &room);
	bool doorNameIsUnique(Room const &room, char const *name, int ignoreIndex);
	void createDoorAtHoveredTile(Room &room);
	void moveSelectedDoor(Room &room, glm::ivec2 position);
	void resizeSelectedDoor(Room &room, glm::ivec2 size);
	void requestDeleteSelectedDoor(Room &room);
	void deleteSelectedDoor(Room &room);
	void syncSelectedDoorBuffer(Room &room);
	void applySelectedDoorName(Room &room);
	void moveSelectedDoorSpawnPosition(Room &room, glm::ivec2 position);
	void clampDoorToRoom(Door &door, Room const &room);
	void clampCamera(Room &room, gl2d::Renderer2D &renderer);
	void drawRoom(Room &room, gl2d::Renderer2D &renderer);
	void drawGrid(Room &room, gl2d::Renderer2D &renderer);
	void drawMoveSelection(gl2d::Renderer2D &renderer);
	void drawDoors(Room &room, gl2d::Renderer2D &renderer);
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
	glm::vec2 mouseWorldPosition = {};

	bool rectDragActive = false;
	bool rectDragPlacesSolid = true;
	glm::ivec2 rectDragStart = {};
	glm::ivec2 rectDragEnd = {};
	MoveSelection moveSelection = {};
	int selectedDoorIndex = -1;
	bool doorDragActive = false;
	bool doorResizeActive = false;
	bool doorSpawnDragActive = false;
	glm::ivec2 doorDragGrabOffset = {};

	glm::ivec2 pendingRoomSize = {};
	glm::ivec2 pendingResizeConfirmSize = {};
	bool cameraInitialized = false;

	// Simple file-management state for the JSON room files under resources/levels.
	std::string currentLevelName = {};
	std::string selectedLevelName = {};
	std::string pendingDeleteLevelName = {};
	std::string fileActionMessage = {};
	bool fileActionHasError = false;
	bool levelDirty = false;
	int pendingFileAction = noPendingFileAction;
	bool pendingApplyPendingFileAction = false;
	bool pendingReloadCurrentLevel = false;
	bool requestGameplayMode = false;
	bool requestWorldEditorMode = false;
	char newLevelName[128] = {};
	char renameName[128] = {};
	char selectedDoorName[128] = {};
	std::string pendingDeleteDoorName = {};
	glm::ivec2 newLevelSize = {40, 24};
	std::string doorActionMessage = {};
	bool doorActionHasError = false;
	bool pendingOpenDeleteDoorPopup = false;
	std::vector<PendingDoorRename> pendingDoorRenames = {};
	std::vector<std::string> pendingDoorDeletes = {};
};
