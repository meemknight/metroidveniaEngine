#include "Gameplay.h"
#include "gameLayer.h"

#include "RoomIo.h"
#include "easing.h"
#include "imguiTools.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
namespace
{
	constexpr float kPlayerWidth = 3.f;
	constexpr float kPlayerHeight = 5.f;
	constexpr float kWallProbeWidth = 0.20f;
	constexpr float kWallProbeInsetY = 0.45f;
	constexpr float kMeasureCameraMoveSpeed = 36.f;
	constexpr float kMeasureCameraZoomSpeed = 40.f;
	constexpr float kMinMeasureCameraZoom = 4.f;
	constexpr float kMaxMeasureCameraZoom = 128.f;
	constexpr float kDashJumpCancelThreshold = 0.80f;

	bool aabbOverlaps(glm::vec4 a, glm::vec4 b)
	{
		return
			a.x < b.x + b.z &&
			a.x + a.z > b.x &&
			a.y < b.y + b.w &&
			a.y + a.w > b.y;
	}

	float moveTowards(float current, float target, float maxDelta)
	{
		if (current < target)
		{
			return std::min(current + maxDelta, target);
		}

		if (current > target)
		{
			return std::max(current - maxDelta, target);
		}

		return target;
	}

	int getMoveDirection(float moveInput)
	{
		if (moveInput > 0.01f) { return 1; }
		if (moveInput < -0.01f) { return -1; }
		return 0;
	}

	float getControllerMoveInput(platform::Controller const &controller)
	{
		float moveInput = controller.LStick.x;
		if (std::abs(moveInput) < 0.18f)
		{
			moveInput = 0.f;
		}

		if (moveInput == 0.f)
		{
			if (controller.buttons[platform::Controller::Left].held) { moveInput -= 1.f; }
			if (controller.buttons[platform::Controller::Right].held) { moveInput += 1.f; }
		}

		return std::clamp(moveInput, -1.f, 1.f);
	}

	bool roomRectHitsSolid(Room const &room, glm::vec4 rect)
	{
		int minX = std::max(static_cast<int>(std::floor(rect.x)), 0);
		int minY = std::max(static_cast<int>(std::floor(rect.y)), 0);
		int maxX = std::min(static_cast<int>(std::ceil(rect.x + rect.z)), room.size.x);
		int maxY = std::min(static_cast<int>(std::ceil(rect.y + rect.w)), room.size.y);

		for (int y = minY; y < maxY; y++)
		{
			for (int x = minX; x < maxX; x++)
			{
				if (!room.getBlockUnsafe(x, y).solid)
				{
					continue;
				}

				if (aabbOverlaps(rect, {static_cast<float>(x), static_cast<float>(y), 1.f, 1.f}))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool playerTouchesWallOnSide(Player const &player, Room const &room, int side)
	{
		glm::vec4 bounds = player.physics.transform.getAABB();
		glm::vec4 probe = {};
		probe.y = bounds.y + kWallProbeInsetY;
		probe.w = std::max(bounds.w - kWallProbeInsetY * 2.f, 0.25f);

		if (side < 0)
		{
			probe.x = bounds.x - kWallProbeWidth;
			probe.z = kWallProbeWidth + 0.02f;
		}
		else
		{
			probe.x = bounds.x + bounds.z - 0.02f;
			probe.z = kWallProbeWidth + 0.02f;
		}

		return roomRectHitsSolid(room, probe);
	}

	bool playerTouchesGround(Player const &player, Room const &room)
	{
		glm::vec4 bounds = player.physics.transform.getAABB();
		glm::vec4 probe = {
			bounds.x + 0.08f,
			bounds.y + bounds.w - 0.02f,
			std::max(bounds.z - 0.16f, 0.20f),
			0.10f
		};

		return roomRectHitsSolid(room, probe);
	}
}

void Gameplay::init()
{
	*this = {};
	std::string fontPath = std::string(RESOURCES_PATH) + "arial.ttf";
	measureFont.createFromFile(fontPath.c_str());

	RoomFilesListing levelFiles = listRoomFiles();
	if (!levelFiles.error.empty())
	{
		levelFileHasError = true;
		levelFileMessage = levelFiles.error;
		return;
	}

	if (!levelFiles.files.empty())
	{
		loadLevel(levelFiles.files.front().name.c_str());
		return;
	}

	camera.zoom = tuning.cameraZoom;
	levelFileMessage = "No levels found. Load one from Level Files to start playing.";
}

void Gameplay::cleanup()
{
	measureFont.cleanup();
}

void Gameplay::update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer)
{
	deltaTime = std::min(deltaTime, 0.05f);

	if (currentLevelName.empty())
	{
		drawLevelFilesWindow();
		return;
	}

	if (!measureMode && input.isButtonPressed(platform::Button::M))
	{
		measureMode = true;
		measureDragActive = false;
		measureCameraZoom = tuning.cameraZoom;
		camera.zoom = measureCameraZoom;
		// Measure mode starts from the player every time so it behaves like a temporary
		// free camera instead of resuming the last paused view.
		setMeasureViewCenter(player.getCenter(), renderer);
	}

	if (measureMode && input.isButtonPressed(platform::Button::Escape))
	{
		measureMode = false;
		measureDragActive = false;
	}

	float gameplayDeltaTime = measureMode ? 0.f : deltaTime;

	if (!measureMode && input.isButtonPressed(platform::Button::R))
	{
		respawnPlayer();
	}

	bool moveLeft = !measureMode && (input.isButtonHeld(platform::Button::A) || input.isButtonHeld(platform::Button::Left));
	bool moveRight = !measureMode && (input.isButtonHeld(platform::Button::D) || input.isButtonHeld(platform::Button::Right));
	bool jumpPressed = !measureMode && (
		input.isButtonPressed(platform::Button::Space) ||
		input.isButtonPressed(platform::Button::W) ||
		input.isButtonPressed(platform::Button::Up) ||
		input.controller.buttons[platform::Controller::A].pressed);
	bool jumpHeld = !measureMode && (
		input.isButtonHeld(platform::Button::Space) ||
		input.isButtonHeld(platform::Button::W) ||
		input.isButtonHeld(platform::Button::Up) ||
		input.controller.buttons[platform::Controller::A].held);
	bool dashPressed = !measureMode &&
		tuning.enableDash &&
		(input.isButtonPressed(platform::Button::LeftShift) || input.controller.RTButton.pressed);
	bool sprintHeld = !measureMode &&
		tuning.enableSprint &&
		(input.isButtonHeld(platform::Button::LeftShift) || input.controller.RTButton.held);

	float moveInput = 0.f;
	if (moveLeft) { moveInput -= 1.f; }
	if (moveRight) { moveInput += 1.f; }
	float controllerMoveInput = !measureMode ? getControllerMoveInput(input.controller) : 0.f;
	if (std::abs(controllerMoveInput) > std::abs(moveInput))
	{
		moveInput = controllerMoveInput;
	}

	updatePlayer(gameplayDeltaTime, moveInput, jumpPressed, jumpHeld, dashPressed, sprintHeld);
	if (!measureMode)
	{
		updateDoorTransition();
	}

	if (measureMode)
	{
		updateMeasureCamera(deltaTime, input, renderer);
	}
	else
	{
		updateCamera(renderer.windowW, renderer.windowH);
	}

	updateMeasureTool(input, renderer);
	renderer.setCamera(camera);

	drawRoom(renderer);
	drawGrid(renderer);
	drawPlayer(renderer);
	drawMeasureOverlay(renderer);
	drawDebugWindow();
	drawLevelFilesWindow();
}

void Gameplay::fillSolidRect(int minX, int minY, int maxX, int maxY)
{
	for (int y = minY; y <= maxY; y++)
	{
		for (int x = minX; x <= maxX; x++)
		{
			if (Block *block = room.getBlockSafe(x, y))
			{
				block->solid = true;
			}
		}
	}
}

void Gameplay::respawnPlayer()
{
	player = {};
	player.physics.transform.w = kPlayerWidth;
	player.physics.transform.h = kPlayerHeight;
	player.physics.teleport(spawnPoint);
	player.moveSpeed = tuning.moveSpeed;

	camera = {};
	camera.zoom = tuning.cameraZoom;
}

void Gameplay::createStarterRoom()
{
	room.create(160, 48);

	fillSolidRect(0, 42, room.size.x - 1, room.size.y - 1);
	fillSolidRect(0, 0, 0, room.size.y - 1);
	fillSolidRect(room.size.x - 1, 0, room.size.x - 1, room.size.y - 1);

	fillSolidRect(10, 32, 26, 32);
	fillSolidRect(34, 28, 48, 28);
	fillSolidRect(58, 24, 74, 24);
	fillSolidRect(82, 20, 96, 20);
	fillSolidRect(104, 26, 116, 26);
	fillSolidRect(124, 18, 138, 18);

	spawnPoint = {14.f + kPlayerWidth * 0.5f, 32.f - kPlayerHeight * 0.5f};

	respawnPlayer();
	levelLoadRevision++;
}

void Gameplay::setDefaultSpawnPoint()
{
	// Until we add authored spawn markers, start near the top middle of the loaded room.
	float minX = kPlayerWidth * 0.5f + 1.f;
	float maxX = std::max(minX, room.size.x - kPlayerWidth * 0.5f - 1.f);
	spawnPoint = {
		std::clamp(room.size.x * 0.5f, minX, maxX),
		kPlayerHeight * 0.5f + 1.f
	};
}

void Gameplay::setSpawnPointFromDoor(Door const &door)
{
	// Door spawn markers are 1x1 tiles, and the player's feet should land on the
	// bottom center of that marker instead of centering the whole player on it.
	spawnPoint = {
		door.playerSpawnPosition.x + 0.5f,
		door.playerSpawnPosition.y + 1.f - kPlayerHeight * 0.5f
	};
}

bool Gameplay::refreshWorldData()
{
	WorldData loadedWorld = {};
	WorldIoResult result = loadWorldData(loadedWorld);
	if (!result.success)
	{
		return false;
	}

	world = loadedWorld;
	return true;
}

void Gameplay::loadLevel(char const *levelName)
{
	Room loadedRoom = {};
	RoomIoResult result = loadRoomFromFile(loadedRoom, levelName);
	levelFileHasError = !result.success;
	levelFileMessage = result.message;

	if (!result.success)
	{
		return;
	}

	room = loadedRoom;
	currentLevelName = result.levelName;
	selectedLevelName = result.levelName;
	refreshWorldData();
	setDefaultSpawnPoint();
	blockedDoorLevelName.clear();
	blockedDoorName.clear();
	respawnPlayer();
	levelLoadRevision++;
}

Door const *Gameplay::findDoorByName(Room const &room, std::string const &doorName)
{
	for (Door const &door : room.doors)
	{
		if (door.name == doorName)
		{
			return &door;
		}
	}

	return {};
}

Door const *Gameplay::findTouchedDoor()
{
	glm::vec4 playerBounds = player.physics.transform.getAABB();
	for (Door const &door : room.doors)
	{
		glm::vec4 triggerRect = door.getRectF();
		float insetX = std::min(triggerRect.z * 0.15f, 0.18f);
		float insetY = std::min(triggerRect.w * 0.12f, 0.16f);
		triggerRect.x += insetX;
		triggerRect.y += insetY;
		triggerRect.z = std::max(triggerRect.z - insetX * 2.f, 0.10f);
		triggerRect.w = std::max(triggerRect.w - insetY * 2.f, 0.10f);

		// Make the trigger slightly smaller than the door art so you need to step
		// into the doorway a bit before a room transition happens.
		if (aabbOverlaps(playerBounds, triggerRect))
		{
			return &door;
		}
	}

	return {};
}

void Gameplay::updateDoorTransition()
{
	Door const *touchedDoor = findTouchedDoor();
	if (!touchedDoor)
	{
		if (blockedDoorLevelName == currentLevelName)
		{
			blockedDoorLevelName.clear();
			blockedDoorName.clear();
		}
		return;
	}

	if (blockedDoorLevelName == currentLevelName && blockedDoorName == touchedDoor->name)
	{
		return;
	}

	if (!travelThroughDoor(*touchedDoor))
	{
		// Avoid repeating the same failed load every frame while the player stays inside the door.
		blockedDoorLevelName = currentLevelName;
		blockedDoorName = touchedDoor->name;
	}
}

bool Gameplay::travelThroughDoor(Door const &door)
{
	if (!refreshWorldData())
	{
		levelFileHasError = true;
		levelFileMessage = "Couldn't load world door links";
		return false;
	}

	auto currentPlacement = world.levels.find(currentLevelName);
	if (currentPlacement == world.levels.end())
	{
		return false;
	}

	auto linkIt = currentPlacement->second.doorLinks.find(door.name);
	if (linkIt == currentPlacement->second.doorLinks.end())
	{
		return false;
	}

	if (linkIt->second.levelName.empty() || linkIt->second.doorName.empty())
	{
		levelFileHasError = true;
		levelFileMessage = "That door link is incomplete";
		return false;
	}

	Room loadedRoom = {};
	RoomIoResult roomResult = loadRoomFromFile(loadedRoom, linkIt->second.levelName.c_str());
	if (!roomResult.success)
	{
		levelFileHasError = true;
		levelFileMessage = roomResult.message;
		return false;
	}

	Door const *targetDoor = findDoorByName(loadedRoom, linkIt->second.doorName);
	if (!targetDoor)
	{
		levelFileHasError = true;
		levelFileMessage = "The linked target door no longer exists";
		return false;
	}

	room = loadedRoom;
	currentLevelName = roomResult.levelName;
	selectedLevelName = roomResult.levelName;
	setSpawnPointFromDoor(*targetDoor);
	blockedDoorLevelName = currentLevelName;
	blockedDoorName = targetDoor->name;
	levelFileHasError = false;
	levelFileMessage = "Entered " + currentLevelName + " through " + targetDoor->name;
	respawnPlayer();
	levelLoadRevision++;
	return true;
}

void Gameplay::updateMeasureTool(platform::Input &input, gl2d::Renderer2D &renderer)
{
	mouseScreenPosition = {static_cast<float>(input.mouseX), static_cast<float>(input.mouseY)};
	mouseWorldPosition = gl2d::internal::convertPoint(
		camera,
		mouseScreenPosition,
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));

	hoveredTile = {
		static_cast<int>(std::floor(mouseWorldPosition.x)),
		static_cast<int>(std::floor(mouseWorldPosition.y))
	};
	hoveredTileValid = room.getBlockSafe(hoveredTile.x, hoveredTile.y) != nullptr;

	if (!measureMode)
	{
		measureDragActive = false;
		return;
	}

	if ((input.isLMousePressed() || input.isRMousePressed()) && hoveredTileValid)
	{
		measureDragActive = true;
		measureStart = hoveredTile;
		measureEnd = hoveredTile;
	}

	if (measureDragActive && hoveredTileValid)
	{
		measureEnd = hoveredTile;
	}

	if (measureDragActive && (input.isLMouseReleased() || input.isRMouseReleased()))
	{
		measureDragActive = false;
	}
}

glm::vec2 Gameplay::getMeasureViewCenter(gl2d::Renderer2D &renderer)
{
	return gl2d::internal::convertPoint(
		camera,
		{static_cast<float>(renderer.windowW) * 0.5f, static_cast<float>(renderer.windowH) * 0.5f},
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));
}

void Gameplay::setMeasureViewCenter(glm::vec2 center, gl2d::Renderer2D &renderer)
{
	camera.follow(
		center,
		1.f,
		0.f,
		0.f,
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));
}

void Gameplay::updateMeasureCamera(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer)
{
	float moveSpeed = kMeasureCameraMoveSpeed;
	if (input.isButtonHeld(platform::Button::LeftShift))
	{
		moveSpeed *= 2.5f;
	}

	glm::vec2 center = getMeasureViewCenter(renderer);
	if (input.isButtonHeld(platform::Button::A) || input.isButtonHeld(platform::Button::Left)) { center.x -= moveSpeed * deltaTime; }
	if (input.isButtonHeld(platform::Button::D) || input.isButtonHeld(platform::Button::Right)) { center.x += moveSpeed * deltaTime; }
	if (input.isButtonHeld(platform::Button::W) || input.isButtonHeld(platform::Button::Up)) { center.y -= moveSpeed * deltaTime; }
	if (input.isButtonHeld(platform::Button::S) || input.isButtonHeld(platform::Button::Down)) { center.y += moveSpeed * deltaTime; }

	if (input.isButtonHeld(platform::Button::Q))
	{
		measureCameraZoom -= kMeasureCameraZoomSpeed * deltaTime;
	}

	if (input.isButtonHeld(platform::Button::E))
	{
		measureCameraZoom += kMeasureCameraZoomSpeed * deltaTime;
	}

	measureCameraZoom = std::clamp(measureCameraZoom, kMinMeasureCameraZoom, kMaxMeasureCameraZoom);
	camera.zoom = measureCameraZoom;
	setMeasureViewCenter(center, renderer);
}

glm::vec4 Gameplay::getMeasureRectPreview() const
{
	int minX = std::min(measureStart.x, measureEnd.x);
	int minY = std::min(measureStart.y, measureEnd.y);
	int maxX = std::max(measureStart.x, measureEnd.x);
	int maxY = std::max(measureStart.y, measureEnd.y);

	return {
		static_cast<float>(minX),
		static_cast<float>(minY),
		static_cast<float>(maxX - minX + 1),
		static_cast<float>(maxY - minY + 1)
	};
}

void Gameplay::updateRememberedWallInput(float deltaTime, float moveInput)
{
	int moveDirection = getMoveDirection(moveInput);
	if (moveDirection != 0)
	{
		player.rememberedWallInputSide = moveDirection;
		player.rememberedWallInputTimer = tuning.wallInputMemoryTime;
		return;
	}

	player.rememberedWallInputTimer -= deltaTime;
	if (player.rememberedWallInputTimer <= 0.f)
	{
		player.rememberedWallInputTimer = 0.f;
		player.rememberedWallInputSide = 0;
	}
}

void Gameplay::startDash(int direction)
{
	player.dashActive = true;
	player.dashDirection = direction == 0 ? player.lastMoveDirection : direction;
	if (player.dashDirection == 0)
	{
		player.dashDirection = 1;
	}

	player.dashTimer = 0.f;
	player.dashPreviousOffset = 0.f;
	player.dashStartX = player.physics.transform.pos.x;
	player.dashCooldownTimer = tuning.dashCooldownTime;
	player.wallGrabSide = 0;
	player.wallJumpCarryVelocity = 0.f;
	player.physics.velocity = {};
	player.physics.acceleration = {};

	if (!player.physics.downTouch)
	{
		player.airDashAvailable = false;
	}
}

bool Gameplay::updateDash(float deltaTime)
{
	float dashDuration = std::max(tuning.dashDuration, 0.001f);
	player.dashTimer += deltaTime;

	float dashProgress = easing::clamp01(player.dashTimer / dashDuration);
	float dashOffset = easing::easeOutCubic(dashProgress) * tuning.dashDistance * player.dashDirection;
	float dashDeltaX = dashOffset - player.dashPreviousOffset;
	float startX = player.physics.transform.pos.x;

	// Dash movement is scripted directly so it can have its own feel without
	// depending on the regular gravity / velocity path.
	player.physics.lastPosition = player.physics.transform.pos;
	player.physics.transform.pos.x += dashDeltaX;
	player.physics.velocity = {};
	player.physics.acceleration = {};
	player.physics.resolveConstrains(room);
	player.physics.downTouch = playerTouchesGround(player, room);
	player.physics.upTouch = false;
	player.physics.updateFinal();

	float actualOffset = player.physics.transform.pos.x - player.dashStartX;
	float actualDeltaX = player.physics.transform.pos.x - startX;
	player.dashPreviousOffset = actualOffset;

	bool blocked = std::abs(actualDeltaX - dashDeltaX) > 0.001f;
	if (dashProgress >= 1.f || blocked)
	{
		player.dashActive = false;
	}

	return player.dashActive;
}

void Gameplay::refreshDashAvailability()
{
	if (player.physics.downTouch || player.physics.leftTouch || player.physics.rightTouch || player.wallGrabSide != 0)
	{
		player.airDashAvailable = true;
	}
}

void Gameplay::updateWallGrabState(float moveInput)
{
	if (!tuning.enableWallGrab)
	{
		player.wallGrabSide = 0;
		player.lastWallGrabSide = 0;
		player.rememberedWallInputSide = 0;
		player.rememberedWallInputTimer = 0.f;
		return;
	}

	if (player.physics.downTouch)
	{
		player.wallGrabSide = 0;
		player.lastWallGrabSide = 0;
		return;
	}

	int desiredWallSide = getMoveDirection(moveInput);
	if (desiredWallSide == 0 && player.rememberedWallInputTimer > 0.f)
	{
		desiredWallSide = player.rememberedWallInputSide;
	}

	if (player.wallGrabSide != 0)
	{
		bool stillTouchingGrabWall = playerTouchesWallOnSide(player, room, player.wallGrabSide);
		bool stillHoldingTowardsWall = desiredWallSide == player.wallGrabSide;

		if (!stillTouchingGrabWall || !stillHoldingTowardsWall)
		{
			player.wallGrabSide = 0;
		}

		return;
	}

	if (desiredWallSide == 0)
	{
		return;
	}

	if (player.wallRegrabTimer > 0.f && desiredWallSide == player.lastWallGrabSide)
	{
		return;
	}

	if (player.wallGrabLockTimer > 0.f)
	{
		return;
	}

	bool canGrabLeftWall = desiredWallSide < 0 && playerTouchesWallOnSide(player, room, -1);
	bool canGrabRightWall = desiredWallSide > 0 && playerTouchesWallOnSide(player, room, 1);
	if (!canGrabLeftWall && !canGrabRightWall)
	{
		return;
	}

	// Wall grabs snap the vertical speed back to a controlled slide so the player
	// can catch the wall cleanly before the slower fall ramps in again.
	player.wallGrabSide = canGrabLeftWall ? -1 : 1;
	player.wallJumpCarryVelocity = 0.f;
	player.physics.velocity.y = 0.f;
}

void Gameplay::updatePlayer(float deltaTime, float moveInput, bool jumpPressed, bool jumpHeld, bool dashPressed, bool sprintHeld)
{
	if (moveInput != 0.f)
	{
		player.lastMoveDirection = getMoveDirection(moveInput);
	}

	player.dashCooldownTimer -= deltaTime;
	player.dashCooldownTimer = std::max(player.dashCooldownTimer, 0.f);

	if (player.dashActive && !tuning.enableDash)
	{
		player.dashActive = false;
	}

	// Sprint is a simple hold modifier over the normal horizontal speed.
	player.moveSpeed = sprintHeld ? tuning.sprintMoveSpeed : tuning.moveSpeed;
	float moveEaseTime = std::abs(moveInput) > 0.01f
		? tuning.moveStartEaseTime
		: tuning.moveStopEaseTime;
	player.smoothedMoveInput = easing::damp(
		player.smoothedMoveInput,
		moveInput,
		moveEaseTime,
		deltaTime);
	if (std::abs(player.smoothedMoveInput) < 0.001f)
	{
		player.smoothedMoveInput = 0.f;
	}
	bool grounded = player.physics.downTouch;
	if (tuning.enableWallGrab)
	{
		updateRememberedWallInput(deltaTime, moveInput);
	}
	else
	{
		player.wallGrabSide = 0;
		player.lastWallGrabSide = 0;
		player.rememberedWallInputSide = 0;
		player.rememberedWallInputTimer = 0.f;
		player.wallRegrabTimer = 0.f;
		player.wallGrabLockTimer = 0.f;
	}

	if (grounded)
	{
		player.coyoteTimer = tuning.coyoteTime;
		player.wallJumpCarryVelocity = 0.f;
		player.wallRegrabTimer = 0.f;
		player.wallGrabLockTimer = 0.f;
	}
	else
	{
		player.coyoteTimer -= deltaTime;
		player.coyoteTimer = std::max(player.coyoteTimer, 0.f);
		player.wallRegrabTimer -= deltaTime;
		player.wallRegrabTimer = std::max(player.wallRegrabTimer, 0.f);
		player.wallGrabLockTimer -= deltaTime;
		player.wallGrabLockTimer = std::max(player.wallGrabLockTimer, 0.f);
	}

	if (jumpPressed)
	{
		player.jumpBufferTimer = tuning.jumpBufferTime;
		if (player.dashActive)
		{
			// Jump presses during dash should survive until the dash finishes instead
			// of getting lost in the early-return dash path.
			player.jumpQueuedAfterDash = true;
		}
	}
	else
	{
		player.jumpBufferTimer -= deltaTime;
		player.jumpBufferTimer = std::max(player.jumpBufferTimer, 0.f);
	}

	if (player.dashActive && jumpPressed)
	{
		float dashDuration = std::max(tuning.dashDuration, 0.001f);
		float dashProgress = player.dashTimer / dashDuration;
		if (dashProgress >= kDashJumpCancelThreshold)
		{
			// Late jump presses should be able to break out of the dash a bit early
			// so ground dashes still flow into jumps without waiting for the last frames.
			player.dashActive = false;
			player.jumpQueuedAfterDash = false;
		}
	}

	bool canStartDash = tuning.enableDash &&
		dashPressed &&
		!player.dashActive &&
		player.dashCooldownTimer <= 0.f &&
		(grounded || player.airDashAvailable || player.wallGrabSide != 0);
	if (canStartDash)
	{
		int dashDirection = getMoveDirection(moveInput);
		if (dashDirection == 0)
		{
			dashDirection = player.lastMoveDirection;
		}

		startDash(dashDirection);
	}

	if (player.dashActive)
	{
		if (updateDash(deltaTime))
		{
			refreshDashAvailability();
			return;
		}

		updateWallGrabState(moveInput);
		refreshDashAvailability();
		grounded = player.physics.downTouch;
		if (grounded)
		{
			player.coyoteTimer = tuning.coyoteTime;
		}

		if (player.jumpQueuedAfterDash)
		{
			player.jumpQueuedAfterDash = false;
			player.jumpBufferTimer = std::max(player.jumpBufferTimer, tuning.jumpBufferTime);
		}
		else
		{
			return;
		}
	}

	float jumpRiseGravity = tuning.gravity * std::max(tuning.jumpRiseGravityMultiplier, 0.01f);
	float jumpSpeed = std::sqrt(2.f * jumpRiseGravity * tuning.jumpHeight);

	if (player.wallGrabSide != 0 && player.jumpBufferTimer > 0.f)
	{
		int previousWallGrabSide = player.wallGrabSide;
		player.wallGrabSide = 0;
		player.lastWallGrabSide = previousWallGrabSide;
		player.wallRegrabTimer = tuning.wallRegrabTime;
		// Wall jumps use the normal jump height, but also nudge the player away
		// from the wall right away so the launch feels more like a ground jump.
		player.physics.transform.pos.x += -previousWallGrabSide * tuning.wallJumpDetachDistance;
		player.wallJumpCarryVelocity = -previousWallGrabSide * tuning.wallJumpPushSpeed;
		player.physics.velocity.y = -jumpSpeed;
		player.physics.downTouch = false;
		player.coyoteTimer = 0.f;
		player.jumpBufferTimer = 0.f;
	}

	if (player.wallGrabSide == 0 && player.jumpBufferTimer > 0.f && player.coyoteTimer > 0.f)
	{
		player.physics.velocity.y = -jumpSpeed;
		player.physics.downTouch = false;
		player.coyoteTimer = 0.f;
		player.jumpBufferTimer = 0.f;
		player.wallGrabLockTimer = tuning.wallGrabDelayAfterGroundJump;
	}

	if (!jumpHeld && player.physics.velocity.y < 0.f)
	{
		player.physics.velocity.y *= tuning.jumpCutMultiplier;
	}

	if (player.wallGrabSide == 0)
	{
		player.physics.transform.pos.x += player.smoothedMoveInput * player.moveSpeed * deltaTime;
		player.physics.transform.pos.x += player.wallJumpCarryVelocity * deltaTime;
		player.wallJumpCarryVelocity = moveTowards(
			player.wallJumpCarryVelocity,
			0.f,
			tuning.wallJumpCarryDrag * deltaTime);

		float appliedGravity = player.physics.velocity.y < 0.f
			? jumpRiseGravity
			: tuning.gravity;
		player.physics.velocity.y += appliedGravity * deltaTime;
		player.physics.velocity.y = std::min(player.physics.velocity.y, tuning.maxFallSpeed);
	}
	else
	{
		player.wallJumpCarryVelocity = 0.f;
		player.physics.velocity.y += tuning.wallSlideGravity * deltaTime;
		player.physics.velocity.y = std::min(player.physics.velocity.y, tuning.wallSlideSpeed);
	}

	player.physics.velocity.x = 0.f;
	player.physics.updateForces(deltaTime, {0.f, 0.f});
	player.physics.resolveConstrains(room);
	player.physics.updateFinal();
	updateWallGrabState(moveInput);
	refreshDashAvailability();
}

//DON'T TOUCH THIS CODE!!!!!
void Gameplay::updateCamera(int width, int height)
{
	camera.zoom = tuning.cameraZoom;

	glm::vec2 roomSize = glm::vec2(room.size);
	glm::vec2 viewSize = glm::vec2(width, height) / camera.zoom;
	glm::vec2 maxCamera = roomSize - viewSize;
	maxCamera.x = std::max(maxCamera.x, 0.f);
	maxCamera.y = std::max(maxCamera.y, 0.f);

	//DON'T TOUCH THIS CODE!!!!!
	camera.follow(player.getCenter(), 10,
		0.f, 0.f, width, height);
	//camera.position = glm::clamp(camera.position, glm::vec2(0.f), maxCamera);
}

void Gameplay::drawRoom(gl2d::Renderer2D &renderer)
{
	const gl2d::Color4f roomBackground = {0.10f, 0.11f, 0.15f, 1.f};
	const gl2d::Color4f blockColor = {0.23f, 0.26f, 0.32f, 1.f};

	renderer.renderRectangle({0.f, 0.f, static_cast<float>(room.size.x), static_cast<float>(room.size.y)}, roomBackground);

	for (int y = 0; y < room.size.y; y++)
	{
		for (int x = 0; x < room.size.x; x++)
		{
			if (!room.getBlockUnsafe(x, y).solid)
			{
				continue;
			}

			renderer.renderRectangle({static_cast<float>(x), static_cast<float>(y), 1.f, 1.f}, blockColor);
		}
	}
}

void Gameplay::drawGrid(gl2d::Renderer2D &renderer)
{
	if (!tuning.showGrid)
	{
		return;
	}

	const gl2d::Color4f gridColor = {0.80f, 0.84f, 0.90f, tuning.gridAlpha};

	for (int x = 0; x <= room.size.x; x++)
	{
		renderer.renderLine(
			{static_cast<float>(x), 0.f},
			{static_cast<float>(x), static_cast<float>(room.size.y)},
			gridColor,
			tuning.gridLineWidth);
	}

	for (int y = 0; y <= room.size.y; y++)
	{
		renderer.renderLine(
			{0.f, static_cast<float>(y)},
			{static_cast<float>(room.size.x), static_cast<float>(y)},
			gridColor,
			tuning.gridLineWidth);
	}
}

void Gameplay::drawPlayer(gl2d::Renderer2D &renderer)
{
	gl2d::Color4f playerColor = player.physics.downTouch
		? gl2d::Color4f{1.0f, 0.63f, 0.19f, 1.f}
		: gl2d::Color4f{1.0f, 0.84f, 0.30f, 1.f};
	if (player.wallGrabSide != 0)
	{
		playerColor = {0.58f, 0.90f, 1.0f, 1.f};
	}
	if (player.dashActive)
	{
		playerColor = {0.36f, 1.0f, 0.82f, 1.f};
	}

	renderer.renderRectangle(player.physics.transform.getAABB(), playerColor);
}

void Gameplay::drawMeasureOverlay(gl2d::Renderer2D &renderer)
{
	if (!measureMode)
	{
		return;
	}

	if (hoveredTileValid)
	{
		renderer.renderRectangleOutline(
			{static_cast<float>(hoveredTile.x), static_cast<float>(hoveredTile.y), 1.f, 1.f},
			{0.28f, 0.68f, 1.0f, 0.9f},
			0.08f);
	}

	if (!measureDragActive)
	{
		return;
	}

	glm::vec4 rect = getMeasureRectPreview();
	renderer.renderRectangleOutline(rect, {0.28f, 0.68f, 1.0f, 0.95f}, 0.10f);

	if (!measureFont.texture.isValid())
	{
		return;
	}

	glm::ivec2 size = {
		std::abs(measureEnd.x - measureStart.x) + 1,
		std::abs(measureEnd.y - measureStart.y) + 1
	};

	char text[64] = {};
	std::snprintf(text, sizeof(text), "%d x %d", size.x, size.y);

	renderer.pushCamera();
	renderer.renderText(
		mouseScreenPosition + glm::vec2(18.f, 18.f),
		text,
		measureFont,
		{0.30f, 0.76f, 1.0f, 1.f},
		36.f,
		4.f,
		3.f,
		false,
		{0.03f, 0.06f, 0.10f, 0.95f});
	renderer.popCamera();
}

void Gameplay::drawDebugWindow()
{
#if REMOVE_IMGUI == 0
	if (!ImGui::isImguiWindowOpen())
	{
		return;
	}

	ImGui::SetNextWindowBgAlpha(0.88f);
	ImGui::SetNextWindowSize({320.f, 0.f}, ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Editor"))
	{
		ImGui::TextUnformatted("F10 hides / shows ImGui");
		ImGui::TextUnformatted("F6 Game, F7 Level Editor, F8 World Editor");
		ImGui::TextUnformatted("` toggles between gameplay and the last editor mode");
		ImGui::TextUnformatted("Shift presses dash, holding Shift keeps sprint on");
		ImGui::TextUnformatted("Controller: LStick/DPad move, A jumps, RT dashes");
		ImGui::TextUnformatted("M enters measure mode, Escape resumes gameplay");
		ImGui::TextUnformatted("Measure mode: WASD / Arrows move camera, Q/E zoom");
		if (measureMode)
		{
			ImGui::TextColored({0.30f, 0.76f, 1.0f, 1.f}, "Measure mode is pausing gameplay");
		}

		if (ImGui::Button("Respawn"))
		{
			respawnPlayer();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Tunables"))
		{
			tuning = {};
			respawnPlayer();
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Player");
		ImGui::Checkbox("Enable Dash", &tuning.enableDash);
		ImGui::Checkbox("Enable Sprint", &tuning.enableSprint);
		ImGui::Checkbox("Enable Wall Grab", &tuning.enableWallGrab);
		ImGui::SliderFloat("Move Speed", &tuning.moveSpeed, 0.5f, 30.f, "%.2f");
		ImGui::SliderFloat("Sprint Speed", &tuning.sprintMoveSpeed, 0.5f, 40.f, "%.2f");
		ImGui::SliderFloat("Move Start Ease", &tuning.moveStartEaseTime, 0.001f, 0.15f, "%.3f");
		ImGui::SliderFloat("Move Stop Ease", &tuning.moveStopEaseTime, 0.001f, 0.15f, "%.3f");
		ImGui::SliderFloat("Dash Distance", &tuning.dashDistance, 0.5f, 24.f, "%.2f");
		ImGui::SliderFloat("Dash Time", &tuning.dashDuration, 0.02f, 0.40f, "%.3f");
		ImGui::SliderFloat("Dash Cooldown", &tuning.dashCooldownTime, 0.f, 1.f, "%.2f");
		ImGui::SliderFloat("Jump Height", &tuning.jumpHeight, 0.5f, 20.f, "%.2f");
		ImGui::SliderFloat("Jump Snappiness", &tuning.jumpRiseGravityMultiplier, 0.25f, 4.f, "%.2f");
		ImGui::SliderFloat("Wall Jump Detach", &tuning.wallJumpDetachDistance, 0.f, 3.f, "%.2f");
		ImGui::SliderFloat("Wall Jump Push", &tuning.wallJumpPushSpeed, 0.f, 40.f, "%.2f");
		ImGui::SliderFloat("Wall Jump Drag", &tuning.wallJumpCarryDrag, 1.f, 120.f, "%.2f");
		ImGui::SliderFloat("Gravity", &tuning.gravity, 1.f, 120.f, "%.2f");
		ImGui::SliderFloat("Max Fall Speed", &tuning.maxFallSpeed, 1.f, 80.f, "%.2f");
		ImGui::SliderFloat("Wall Slide Gravity", &tuning.wallSlideGravity, 0.5f, 80.f, "%.2f");
		ImGui::SliderFloat("Wall Slide Speed", &tuning.wallSlideSpeed, 0.5f, 30.f, "%.2f");
		ImGui::SliderFloat("Jump Cut", &tuning.jumpCutMultiplier, 0.05f, 1.f, "%.2f");
		ImGui::SliderFloat("Coyote Time", &tuning.coyoteTime, 0.f, 0.30f, "%.2f");
		ImGui::SliderFloat("Jump Buffer", &tuning.jumpBufferTime, 0.f, 0.30f, "%.2f");
		ImGui::SliderFloat("Wall Input Memory", &tuning.wallInputMemoryTime, 0.f, 0.30f, "%.2f");
		ImGui::SliderFloat("Wall Regrab Time", &tuning.wallRegrabTime, 0.f, 0.30f, "%.2f");
		ImGui::SliderFloat("Ground Jump Wall Delay", &tuning.wallGrabDelayAfterGroundJump, 0.f, 0.30f, "%.2f");
		float jumpRiseGravity = tuning.gravity * std::max(tuning.jumpRiseGravityMultiplier, 0.01f);
		float jumpSpeed = std::sqrt(2.f * jumpRiseGravity * tuning.jumpHeight);
		float jumpApexTime = jumpSpeed / std::max(jumpRiseGravity, 0.001f);
		ImGui::Text("Derived Jump Speed: %.2f", jumpSpeed);
		ImGui::Text("Derived Rise Gravity: %.2f", jumpRiseGravity);
		ImGui::Text("Derived Apex Time: %.2f", jumpApexTime);

		ImGui::Separator();
		ImGui::TextUnformatted("Camera");
		ImGui::SliderFloat("Zoom", &tuning.cameraZoom, 4.f, 96.f, "%.1f");

		ImGui::Separator();
		ImGui::TextUnformatted("World");
		ImGui::Checkbox("Show Grid", &tuning.showGrid);
		if (tuning.showGrid)
		{
			ImGui::SliderFloat("Grid Alpha", &tuning.gridAlpha, 0.02f, 1.f, "%.2f");
			ImGui::SliderFloat("Grid Width", &tuning.gridLineWidth, 0.005f, 0.15f, "%.3f");
		}

		ImGui::Separator();
		ImGui::Text("Pos: %.2f, %.2f", player.physics.transform.pos.x, player.physics.transform.pos.y);
		ImGui::Text("Vel: %.2f, %.2f", player.physics.velocity.x, player.physics.velocity.y);
		ImGui::Text("Timers: coyote %.2f  buffer %.2f", player.coyoteTimer, player.jumpBufferTimer);
		ImGui::Text("Wall: grab %d  regrab %.2f  lock %.2f",
			player.wallGrabSide,
			player.wallRegrabTimer,
			player.wallGrabLockTimer);
		ImGui::Text("Dash: active %d  air %d  cooldown %.2f",
			player.dashActive ? 1 : 0,
			player.airDashAvailable ? 1 : 0,
			player.dashCooldownTimer);
		ImGui::Text("Move Input Smooth: %.2f", player.smoothedMoveInput);
		ImGui::Text("Wall Input Memory: %.2f", player.rememberedWallInputTimer);
		ImGui::Text("Wall Carry: %.2f", player.wallJumpCarryVelocity);
		ImGui::Text("Touch: D%d U%d L%d R%d",
			player.physics.downTouch ? 1 : 0,
			player.physics.upTouch ? 1 : 0,
			player.physics.leftTouch ? 1 : 0,
			player.physics.rightTouch ? 1 : 0);
	}
	ImGui::End();
#endif
}

void Gameplay::drawLevelFilesWindow()
{
#if REMOVE_IMGUI == 0
	if (!ImGui::isImguiWindowOpen())
	{
		return;
	}

	RoomFilesListing levelFiles = listRoomFiles();

	bool selectedLevelStillExists = selectedLevelName.empty();
	bool currentLevelStillExists = currentLevelName.empty();
	for (auto const &file : levelFiles.files)
	{
		if (file.name == selectedLevelName)
		{
			selectedLevelStillExists = true;
		}

		if (file.name == currentLevelName)
		{
			currentLevelStillExists = true;
		}
	}

	if (!selectedLevelStillExists)
	{
		selectedLevelName.clear();
	}

	if (!currentLevelStillExists)
	{
		currentLevelName.clear();
		levelFileMessage = "The loaded level is no longer available. Load another level to continue.";
		levelFileHasError = true;
	}

	if (selectedLevelName.empty() && !levelFiles.files.empty())
	{
		selectedLevelName = levelFiles.files.front().name;
	}

	ImGui::SetNextWindowBgAlpha(0.90f);
	ImGui::SetNextWindowSize({380.f, 0.f}, ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Level Files"))
	{
		ImGui::TextUnformatted("View");
		ImGui::RadioButton("Game", true);
		ImGui::SameLine();
		if (ImGui::RadioButton("Level Editor", false))
		{
			requestLevelEditorMode = true;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("World Editor", false))
		{
			requestWorldEditorMode = true;
		}

		ImGui::Separator();
		ImGui::Text("Current Level: %s", currentLevelName.empty() ? "None loaded" : currentLevelName.c_str());
		if (currentLevelName.empty())
		{
			ImGui::TextUnformatted("Room Size: -");
			ImGui::TextUnformatted("Load a saved level to start playing.");
		}
		else
		{
			ImGui::Text("Room Size: %d x %d", room.size.x, room.size.y);
		}

		ImGui::Separator();
		ImGui::Text("Existing Levels (%d)", static_cast<int>(levelFiles.files.size()));
		ImGui::TextColored({0.35f, 1.f, 0.55f, 1.f}, "Loaded level is shown in green");
		if (!levelFiles.error.empty())
		{
			ImGui::TextColored({1.f, 0.45f, 0.35f, 1.f}, "%s", levelFiles.error.c_str());
		}

		if (ImGui::BeginChild("GameplayLevelFileList", {0.f, 220.f}, true))
		{
			for (auto const &file : levelFiles.files)
			{
				bool selected = file.name == selectedLevelName;
				bool loaded = file.name == currentLevelName;
				std::string label = file.name;
				if (loaded)
				{
					label += "  [loaded]";
					ImGui::PushStyleColor(ImGuiCol_Text, {0.35f, 1.f, 0.55f, 1.f});
				}

				if (ImGui::Selectable(label.c_str(), selected))
				{
					selectedLevelName = file.name;
				}

				if (loaded)
				{
					ImGui::PopStyleColor();
				}
			}
		}
		ImGui::EndChild();

		bool canLoadSelected = !selectedLevelName.empty();
		bool canReloadCurrent = !currentLevelName.empty();

		if (!canLoadSelected) { ImGui::BeginDisabled(); }
		if (ImGui::Button("Load Selected"))
		{
			loadLevel(selectedLevelName.c_str());
		}
		if (!canLoadSelected) { ImGui::EndDisabled(); }

		if (!canReloadCurrent) { ImGui::BeginDisabled(); }
		ImGui::SameLine();
		if (ImGui::Button("Reload Current"))
		{
			loadLevel(currentLevelName.c_str());
		}
		if (!canReloadCurrent) { ImGui::EndDisabled(); }

		if (!levelFileMessage.empty())
		{
			ImGui::Separator();
			ImVec4 color = levelFileHasError
				? ImVec4(1.f, 0.45f, 0.35f, 1.f)
				: ImVec4(0.35f, 1.f, 0.55f, 1.f);
			ImGui::TextColored(color, "%s", levelFileMessage.c_str());
		}
	}
	ImGui::End();
#endif
}
