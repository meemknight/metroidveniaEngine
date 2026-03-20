#pragma once

#include "door.h"
#include "WorldIo.h"

#include <gl2d/gl2d.h>
#include <platformInput.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// World editor for arranging multiple room files inside one larger virtual map.
struct WorldEditor
{
	struct Tuning
	{
		float cameraZoom = 1.f;
		float cameraMoveSpeed = 380.f;
		bool showGrid = true;
		float gridStep = 100.f;
		float gridAlpha = 0.18f;
		float gridLineWidth = 0.6f;
	};

	// Runtime preview data for one placed level. The framebuffer owns the texture shown in the world view.
	struct LevelPreview
	{
		gl2d::FrameBuffer frameBuffer = {};
		std::vector<Door> doors = {};
	};

	struct DoorReference
	{
		std::string levelName = {};
		std::string doorName = {};
	};

	void init();
	void cleanup();
	void enter(gl2d::Renderer2D &renderer);
	void update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer,
		bool gameViewHovered, bool gameViewFocused);

	glm::vec2 getViewSize(gl2d::Renderer2D &renderer);
	glm::vec2 getViewCenter(gl2d::Renderer2D &renderer);
	void setViewCenter(glm::vec2 center, gl2d::Renderer2D &renderer);
	glm::vec2 screenToWorld(glm::vec2 screenPos, gl2d::Renderer2D &renderer);
	glm::vec2 worldToScreen(glm::vec2 worldPos, gl2d::Renderer2D &renderer);
	void focusWorld(gl2d::Renderer2D &renderer);
	void focusPlacedLevel(std::string const &levelName, gl2d::Renderer2D &renderer);
	void clearPlacedLevelSelection();
	void selectPlacedLevel(std::string const &levelName);
	bool isPlacedLevelSelected(std::string const &levelName) const;
	glm::vec4 getSelectionRect() const;
	void ensureBoundsForPlacement(WorldLevelPlacement const &placement);
	void clampCamera(gl2d::Renderer2D &renderer);
	void updateCamera(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer);
	void updateShortcuts(platform::Input &input, bool gameViewFocused);
	void updateDragging(platform::Input &input, gl2d::Renderer2D &renderer, bool gameViewHovered);
	void loadWorld();
	void saveWorld();
	void discardWorldChanges();
	void selectDoor(DoorReference const &doorRef);
	void clearSelectedDoor();
	void clearPendingDoorLink();
	DoorReference getPlacedDoorAt(glm::vec2 worldPoint);
	Door const *getPreviewDoor(std::string const &levelName, std::string const &doorName) const;
	glm::vec2 getDoorWorldCenter(DoorReference const &doorRef);
	WorldDoorLink *getDoorLink(std::string const &levelName, std::string const &doorName);
	WorldDoorLink const *getDoorLink(std::string const &levelName, std::string const &doorName) const;
	void unlinkDoor(DoorReference const &doorRef);
	void linkDoors(DoorReference const &sourceDoor, DoorReference const &targetDoor);
	bool sanitizeDoorLinks();
	void cleanupPreview(std::string const &levelName);
	void cleanupAllPreviews();
	void syncPreviewStorageToWorld();
	void refreshPlacementSizesFromRooms();
	void refreshLevelPreview(std::string const &levelName, gl2d::Renderer2D &renderer);
	void refreshAllLevelPreviews(gl2d::Renderer2D &renderer);
	void spawnSelectedLevel(gl2d::Renderer2D &renderer);
	void duplicateSelectedLevel();
	std::string getPlacedLevelAt(glm::vec2 worldPoint);
	void drawWorld(gl2d::Renderer2D &renderer);
	void drawGrid(gl2d::Renderer2D &renderer);
	void drawLevels(gl2d::Renderer2D &renderer);
	void drawDoorOverlays(gl2d::Renderer2D &renderer);
	void drawLevelLabels(gl2d::Renderer2D &renderer);
	void drawWindow(gl2d::Renderer2D &renderer);
	void drawLevelFilesWindow(gl2d::Renderer2D &renderer);
	void drawDiscardWindow(gl2d::Renderer2D &renderer);

	gl2d::Camera camera = {};
	gl2d::Font labelFont = {};
	Tuning tuning = {};
	WorldData world = {};
	std::unordered_map<std::string, LevelPreview> levelPreviews = {};

	std::string selectedLevelName = {};
	std::string selectedPlacedLevelName = {};
	std::unordered_set<std::string> selectedPlacedLevels = {};
	DoorReference selectedDoor = {};
	DoorReference pendingLinkSourceDoor = {};
	DoorReference pendingLinkHoveredDoor = {};
	std::string pendingPreviewRefreshLevelName = {};
	std::string worldMessage = {};
	bool worldHasError = false;
	bool worldDirty = false;
	bool pendingDiscardChanges = false;
	bool cameraInitialized = false;
	bool dragActive = false;
	bool selectionPressActive = false;
	bool selectionDragActive = false;
	float placedLevelDoubleClickTimer = 0.f;
	bool pendingDoorLinkPick = false;
	bool pendingDoorLinkDragActive = false;
	glm::vec2 dragGrabOffset = {};
	// Box selection and grouped room dragging share these temporary interaction fields.
	glm::vec2 selectionPressStartScreen = {};
	glm::vec2 selectionDragStart = {};
	glm::vec2 selectionDragEnd = {};
	glm::vec2 pendingDoorLinkPreviewPoint = {};
	std::unordered_map<std::string, glm::vec2> dragStartPositions = {};
	std::string dragAnchorLevelName = {};
	std::string selectionPressedLevelName = {};
	std::string lastClickedPlacedLevelName = {};

	bool requestGameplayMode = false;
	bool requestLevelEditorMode = false;
	bool requestLoadedLevelEditorMode = false;
};
