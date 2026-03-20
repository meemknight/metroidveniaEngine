#pragma once

#include "door.h"
#include "WorldIo.h"

#include <gl2d/gl2d.h>
#include <platformInput.h>
#include <string>
#include <unordered_map>
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
	float placedLevelDoubleClickTimer = 0.f;
	bool pendingDoorLinkPick = false;
	bool pendingDoorLinkDragActive = false;
	glm::vec2 dragGrabOffset = {};
	glm::vec2 pendingDoorLinkPreviewPoint = {};
	std::string lastClickedPlacedLevelName = {};

	bool requestGameplayMode = false;
	bool requestLevelEditorMode = false;
};
