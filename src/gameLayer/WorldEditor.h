#pragma once

#include "WorldIo.h"

#include <gl2d/gl2d.h>
#include <platformInput.h>
#include <string>

// World editor for arranging multiple room files inside one larger virtual map.
struct WorldEditor
{
	enum Tools
	{
		selectTool = 0,
		dragTool,
	};

	struct Tuning
	{
		float cameraZoom = 1.f;
		float cameraMoveSpeed = 380.f;
		bool showGrid = true;
		float gridStep = 100.f;
		float gridAlpha = 0.18f;
		float gridLineWidth = 0.6f;
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
	void spawnSelectedLevel(gl2d::Renderer2D &renderer);
	std::string getPlacedLevelAt(glm::vec2 worldPoint);
	void drawWorld(gl2d::Renderer2D &renderer);
	void drawGrid(gl2d::Renderer2D &renderer);
	void drawLevels(gl2d::Renderer2D &renderer);
	void drawLevelLabels(gl2d::Renderer2D &renderer);
	void drawWindow(gl2d::Renderer2D &renderer);
	void drawLevelFilesWindow(gl2d::Renderer2D &renderer);
	void drawDiscardWindow(gl2d::Renderer2D &renderer);

	gl2d::Camera camera = {};
	gl2d::Font labelFont = {};
	Tuning tuning = {};
	WorldData world = {};
	int tool = selectTool;

	std::string selectedLevelName = {};
	std::string selectedPlacedLevelName = {};
	std::string worldMessage = {};
	bool worldHasError = false;
	bool worldDirty = false;
	bool cameraInitialized = false;
	bool dragActive = false;
	glm::vec2 dragGrabOffset = {};

	bool requestGameplayMode = false;
	bool requestLevelEditorMode = false;
};
