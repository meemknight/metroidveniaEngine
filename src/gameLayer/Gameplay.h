#pragma once

#include "player.h"
#include "room.h"

#include <glm/vec2.hpp>
#include <platformInput.h>
#include <gl2d/gl2d.h>
#include <string>

struct Gameplay
{
	struct DebugTuning
	{
		float moveSpeed = 25;
		float jumpHeight = 11.f;
		float gravity = 64.f;
		float maxFallSpeed = 38.f;
		float jumpCutMultiplier = 0.60f;
		float coyoteTime = 0.08f;
		float jumpBufferTime = 0.10f;
		float cameraZoom = 32.f;
		bool showGrid = true;
		float gridAlpha = 0.20f;
		float gridLineWidth = 0.05f;
	};

	void init();
	void update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer);

	void fillSolidRect(int minX, int minY, int maxX, int maxY);
	void respawnPlayer();
	void createStarterRoom();
	void setDefaultSpawnPoint();
	void loadLevel(char const *levelName);
	void updatePlayer(float deltaTime, float moveInput, bool jumpPressed, bool jumpHeld);
	void updateCamera(int width, int height);
	void drawRoom(gl2d::Renderer2D &renderer);
	void drawGrid(gl2d::Renderer2D &renderer);
	void drawPlayer(gl2d::Renderer2D &renderer);
	void drawDebugWindow();
	void drawLevelFilesWindow();

	Room room;
	Player player;
	gl2d::Camera camera;
	glm::vec2 spawnPoint = {};
	DebugTuning tuning;

	// Gameplay and editor share these names so both modes point at the same disk level.
	std::string currentLevelName = {};
	std::string selectedLevelName = {};
	std::string levelFileMessage = {};
	bool levelFileHasError = false;
	int levelLoadRevision = 0;
};
