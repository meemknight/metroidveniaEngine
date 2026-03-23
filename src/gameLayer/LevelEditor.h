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
		copyTool,
		ziplineTool,
		spawnRegionTool,
		spikeTool,
		noGrabTool,
		pogoCircleTool,
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

	struct UndoSnapshot
	{
		Room room = {};
		std::string currentLevelName = {};
		std::string selectedLevelName = {};
		std::string renameName = {};
		int selectedDoorIndex = -1;
		std::string selectedDoorName = {};
		int selectedSpawnRegionIndex = -1;
		int selectedSpawnRegionRectIndex = -1;
		int selectedZiplineIndex = -1;
		int selectedZiplinePoint = -1;
		int selectedPogoCircleIndex = -1;
		glm::ivec2 pendingRoomSize = {};
		glm::ivec2 newLevelSize = {};
		bool levelDirty = false;
		std::vector<PendingDoorRename> pendingDoorRenames = {};
		std::vector<std::string> pendingDoorDeletes = {};
	};

	// Move and copy both keep a detached snapshot of the selected blocks and
	// only write it back on Enter. Move also remembers intersecting room entities
	// so a dragged block selection can carry authored markers along with it.
	struct MoveSelection
	{
		struct DoorMove
		{
			int index = -1;
			Door door = {};
		};

		struct PogoCircleMove
		{
			int index = -1;
			PogoCircle pogoCircle = {};
		};

		struct SpawnRegionMove
		{
			int index = -1;
			SpawnRegion spawnRegion = {};
		};

		struct ZiplineMove
		{
			int index = -1;
			Zipline zipline = {};
			bool movePoint[2] = {};
		};

		bool active = false;
		bool previewActive = false;
		bool dragging = false;
		glm::ivec2 sourceStart = {};
		glm::ivec2 size = {};
		glm::ivec2 previewPosition = {};
		glm::ivec2 dragGrabOffset = {};
		std::vector<BlockType> blockTypes = {};
		std::vector<DoorMove> doors = {};
		std::vector<PogoCircleMove> pogoCircles = {};
		std::vector<SpawnRegionMove> spawnRegions = {};
		std::vector<ZiplineMove> ziplines = {};
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
	void updateCamera(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer, Room &room, bool gameViewHovered);
	void updateShortcuts(platform::Input &input, gl2d::Renderer2D &renderer, Room &room, bool gameViewFocused);
	void updateTools(platform::Input &input, Room &room, bool gameViewHovered);
	void setBlock(Room &room, int x, int y, BlockType type);
	void fillRect(Room &room, glm::ivec2 a, glm::ivec2 b, BlockType type);
	void resizeRoom(Room &room, int newSizeX, int newSizeY);
	glm::vec4 getRectPreview(glm::ivec2 a, glm::ivec2 b);
	glm::vec2 worldToScreen(glm::vec2 worldPos, gl2d::Renderer2D &renderer);
	void clearMoveSelection();
	void createMoveSelection(Room &room, glm::ivec2 a, glm::ivec2 b);
	bool moveSelectionContainsTile(glm::ivec2 tile) const;
	bool moveSelectionPreviewContainsTile(glm::ivec2 tile) const;
	void commitMoveSelection(Room &room, bool copyOnly);
	void clearDoorSelection();
	void clearPogoCircleSelection();
	void clearSpawnRegionSelection();
	void clearZiplineSelection();
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
	int getHoveredPogoCircleCenterIndex(Room &room, glm::vec2 mouseWorld);
	int getHoveredPogoCircleResizeIndex(Room &room, glm::vec2 mouseWorld);
	void createPogoCircle(Room &room, glm::vec2 center);
	void moveSelectedPogoCircle(Room &room, glm::vec2 center, bool snapToCellCenter);
	void resizeSelectedPogoCircle(Room &room, glm::vec2 mouseWorld, bool snapToHalfStep);
	void requestDeleteSelectedPogoCircle(Room &room);
	void deleteSelectedPogoCircle(Room &room);
	bool getHoveredSpawnRegionRect(Room &room, glm::vec2 mouseWorld, int &regionIndex, int &rectIndex);
	int getHoveredSpawnRegionSpawnIndex(Room &room, glm::vec2 mouseWorld);
	bool hoveredSelectedSpawnRegionResizeHandle(Room &room, glm::vec2 mouseWorld);
	void createSpawnRegion(Room &room, glm::ivec2 a, glm::ivec2 b);
	void addRectToSelectedSpawnRegion(Room &room, glm::ivec2 a, glm::ivec2 b);
	void moveSelectedSpawnRegion(Room &room, glm::ivec2 position);
	void resizeSelectedSpawnRegionRect(Room &room, glm::ivec2 size);
	void moveSelectedSpawnRegionSpawn(Room &room, glm::ivec2 position);
	void removeSelectedSpawnRegionRect(Room &room);
	void requestDeleteSelectedSpawnRegion(Room &room);
	void deleteSelectedSpawnRegion(Room &room);
	bool getHoveredZiplinePoint(Room &room, glm::vec2 mouseWorld, int &ziplineIndex, int &pointIndex);
	void createZipline(Room &room, glm::ivec2 firstPoint, glm::ivec2 secondPoint);
	void moveSelectedZiplinePoint(Room &room, glm::ivec2 position);
	void requestDeleteSelectedZipline(Room &room);
	void deleteSelectedZipline(Room &room, int ziplineIndex);
	void clampZiplineToRoom(Zipline &zipline, Room const &room);
	void clampDoorToRoom(Door &door, Room const &room);
	void clampSpawnRegionRectToRoom(SpawnRegionRect &rect, Room const &room);
	void clampSpawnRegionToRoom(SpawnRegion &spawnRegion, Room const &room);
	void clampCamera(Room &room, gl2d::Renderer2D &renderer);
	void drawRoom(Room &room, gl2d::Renderer2D &renderer);
	void drawGrid(Room &room, gl2d::Renderer2D &renderer);
	void drawMoveSelection(gl2d::Renderer2D &renderer);
	void drawDoors(Room &room, gl2d::Renderer2D &renderer);
	void drawPogoCircles(Room &room, gl2d::Renderer2D &renderer);
	void drawSpawnRegions(Room &room, gl2d::Renderer2D &renderer);
	void drawZiplines(Room &room, gl2d::Renderer2D &renderer);
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
	UndoSnapshot captureUndoSnapshot(Room &room) const;
	bool undoSnapshotsMatch(UndoSnapshot const &a, UndoSnapshot const &b) const;
	void resetUndoHistory(Room &room);
	void pushUndoSnapshot(Room &room);
	bool applyUndoSnapshot(UndoSnapshot const &snapshot, Room &room, gl2d::Renderer2D &renderer);
	void undo(Room &room, gl2d::Renderer2D &renderer);
	void redo(Room &room, gl2d::Renderer2D &renderer);

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
	int selectedPogoCircleIndex = -1;
	bool pogoCircleDragActive = false;
	bool pogoCircleResizeActive = false;
	glm::vec2 pogoCircleDragGrabOffset = {};
	int selectedSpawnRegionIndex = -1;
	int selectedSpawnRegionRectIndex = -1;
	bool spawnRegionDragActive = false;
	bool spawnRegionResizeActive = false;
	bool spawnRegionSpawnDragActive = false;
	bool spawnRegionCreateDragActive = false;
	bool spawnRegionAddRectArmed = false;
	glm::ivec2 spawnRegionDragGrabOffset = {};
	glm::ivec2 spawnRegionCreateStart = {};
	glm::ivec2 spawnRegionCreateEnd = {};
	int selectedZiplineIndex = -1;
	int selectedZiplinePoint = -1;
	bool ziplineCreateDragActive = false;
	bool ziplinePointDragActive = false;
	glm::ivec2 ziplineCreateStart = {};
	glm::ivec2 ziplineCreateEnd = {};

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
	bool requestEntityEditorMode = false;
	char newLevelName[128] = {};
	char renameName[128] = {};
	char selectedDoorName[128] = {};
	std::string pendingDeleteDoorName = {};
	int pendingDeletePogoCircleIndex = -1;
	int pendingDeleteSpawnRegionIndex = -1;
	int pendingDeleteZiplineIndex = -1;
	glm::ivec2 newLevelSize = {40, 24};
	std::string doorActionMessage = {};
	bool doorActionHasError = false;
	std::string spawnRegionActionMessage = {};
	bool spawnRegionActionHasError = false;
	bool pendingOpenDeleteDoorPopup = false;
	bool pendingOpenDeletePogoCirclePopup = false;
	bool pendingOpenDeleteSpawnRegionPopup = false;
	bool pendingOpenDeleteZiplinePopup = false;
	std::vector<PendingDoorRename> pendingDoorRenames = {};
	std::vector<std::string> pendingDoorDeletes = {};
	std::vector<UndoSnapshot> undoHistory = {};
	int undoHistoryIndex = -1;
	bool brushPaintActive = false;
	bool applyingUndoRedo = false;
};
