#pragma once

#include "player.h"
#include "room.h"
#include "WorldIo.h"

#include <glm/vec2.hpp>
#include <platformInput.h>
#include <gl2d/gl2d.h>
#include <string>

struct Gameplay
{
	struct DebugTuning
	{
		float moveSpeed = 18; // Normal horizontal speed.
		float sprintMoveSpeed = 26.f; // Horizontal speed while sprint is held.
		float moveStartEaseTime = 0.01f; // Small acceleration ease when starting to move.
		float moveStopEaseTime = 0.02f; // Small deceleration ease when releasing movement.
		float dashDistance = 10.f; // Horizontal distance covered by one dash.
		float dashDuration = 0.18f; // How long the scripted dash movement lasts.
		float dashCooldownTime = 0.18f; // Delay after a dash before another one can start.
		float jumpHeight = 11.f; // Ground jump height in tiles.
		float doubleJumpHeight = 9.f; // Air jump height for the one extra jump.
		float jumpRiseGravityMultiplier = 1.62f; // Upward gravity multiplier. Higher = snappier jump rise at the same height. (jump snappiness)
		float glideFallSpeed = 5.f; // Target falling speed while glide is fully active.
		float glideEnterTime = 0.09f; // Small delay for easing from normal fall into the glide fall speed.
		float wallJumpDetachDistance = 0.55f; // Instant shove away from the wall on wall jump.
		float wallJumpPushSpeed = 22.f; // Extra sideways carry added after the wall jump starts.
		float wallJumpCarryDrag = 56.f; // How fast that wall-jump sideways carry fades back to 0.
		float gravity = 100.f; // Normal downward acceleration.
		float maxFallSpeed = 38.f; // Fastest normal falling speed.
		float wallGrabHoldTime = 0.20f; // How long a fresh wall grab hangs in place before the slide starts.
		float wallSlideGravity = 18.f; // Downward acceleration while holding a wall.
		float wallSlideSpeed = 8.f; // Top falling speed while wall sliding.
		float wallClimbDuration = 0.18f; // Base time for a full ledge climb from the wall onto the top.
		float jumpCutMultiplier = 0.60f; // Upward speed kept when jump is released early.
		float coyoteTime = 0.08f; // Ground-jump grace time after leaving a ledge.
		float jumpBufferTime = 0.10f; // Early jump press grace time before landing or grabbing.
		float wallInputMemoryTime = 0.10f; // How long wall input is remembered for grabbing.
		float wallRegrabTime = 0.06f; // Delay before you can grab the same wall again.
		float wallGrabDelayAfterGroundJump = 0.08f; // Delay before a ground jump can latch onto a wall.
		float cameraZoom = 32.f; // Gameplay camera zoom.
		bool enableDash = true; // Allows air and ground dashes.
		bool enableSprint = true; // Allows sprint input to change move speed.
		bool enableDoubleJump = true; // Allows one extra jump while airborne.
		bool enableGlide = true; // Allows a second jump press while falling to slow the descent.
		bool enableWallGrab = true; // Allows wall grabbing, sliding, and wall jumps.
		bool enableWallHold = true; // Lets pushing into a grabbed wall freeze the slide in place.
		bool enableWallClimb = true; // Allows automatic ledge climbs when airborne and pressing into a wall.
		bool showGrid = true; // Draw the gameplay tile grid.
		float gridAlpha = 0.20f; // Grid line opacity.
		float gridLineWidth = 0.05f; // Grid line thickness in world units.
	};

	void init();
	void cleanup();
	void update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer);

	void fillSolidRect(int minX, int minY, int maxX, int maxY);
	void respawnPlayer();
	void createStarterRoom();
	void setDefaultSpawnPoint();
	void setSpawnPointFromDoor(Door const &door);
	void loadLevel(char const *levelName);
	bool refreshWorldData();
	Door const *findDoorByName(Room const &room, std::string const &doorName);
	Door const *findTouchedDoor();
	void updateDoorTransition();
	bool travelThroughDoor(Door const &door);
	void startDash(int direction);
	bool updateDash(float deltaTime);
	void refreshDashAvailability();
	bool tryStartZiplineRide();
	bool updateZiplineRide(float deltaTime, bool jumpPressed, bool downPressed, float jumpSpeed);
	void startWallClimb(int wallSide, glm::vec2 cornerCenter, glm::vec2 endCenter);
	bool tryStartWallClimb(float moveInput);
	bool updateWallClimb(float deltaTime);
	void updatePlayer(float deltaTime, float moveInput, bool jumpPressed, bool jumpHeld, bool downPressed, bool dashPressed, bool sprintHeld);
	void updateRememberedWallInput(float deltaTime, float moveInput);
	void updateWallGrabState(float moveInput);
	void updateMeasureTool(platform::Input &input, gl2d::Renderer2D &renderer);
	glm::vec2 getMeasureViewCenter(gl2d::Renderer2D &renderer);
	void setMeasureViewCenter(glm::vec2 center, gl2d::Renderer2D &renderer);
	void updateMeasureCamera(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer);
	glm::vec4 getMeasureRectPreview() const;
	void updateCamera(int width, int height);
	void drawRoom(gl2d::Renderer2D &renderer);
	void drawGrid(gl2d::Renderer2D &renderer);
	void drawPlayer(gl2d::Renderer2D &renderer);
	void drawMeasureOverlay(gl2d::Renderer2D &renderer);
	void drawDebugWindow();
	void drawLevelFilesWindow();

	Room room;
	Player player;
	WorldData world;
	gl2d::Camera camera;
	gl2d::Font measureFont;
	glm::vec2 spawnPoint = {};
	DebugTuning tuning;
	bool measureMode = false;
	bool measureDragActive = false;
	glm::ivec2 measureStart = {};
	glm::ivec2 measureEnd = {};
	glm::ivec2 hoveredTile = {-1, -1};
	bool hoveredTileValid = false;
	glm::vec2 mouseScreenPosition = {};
	glm::vec2 mouseWorldPosition = {};
	float measureCameraZoom = 0.f;

	// Gameplay and editor share these names so both modes point at the same disk level.
	std::string currentLevelName = {};
	std::string selectedLevelName = {};
	std::string levelFileMessage = {};
	bool levelFileHasError = false;
	int levelLoadRevision = 0;
	std::string blockedDoorLevelName = {};
	std::string blockedDoorName = {};
	bool requestLevelEditorMode = false;
	bool requestWorldEditorMode = false;
};
