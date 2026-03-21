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
	constexpr float kWallClimbWallPadding = 0.30f;
	constexpr float kMeasureCameraMoveSpeed = 36.f;
	constexpr float kMeasureCameraZoomSpeed = 40.f;
	constexpr float kMinMeasureCameraZoom = 4.f;
	constexpr float kMaxMeasureCameraZoom = 128.f;
	constexpr float kDashJumpCancelThreshold = 0.80f;
	constexpr float kMinWallClimbDuration = 0.05f;
	constexpr float kWallClimbVerticalPhase = 0.55f;
	constexpr float kWallJumpReleaseGraceTime = 0.06f;
	constexpr float kZiplineAttachDistance = 0.45f;
	constexpr float kZiplineDetachDelay = 0.12f;
	constexpr float kZiplineCollisionInset = 0.5f;

	struct WallClimbTarget
	{
		glm::vec2 cornerCenter = {};
		glm::vec2 endCenter = {};
	};

	struct ZiplineSlideData
	{
		glm::vec2 uphillPoint = {};
		glm::vec2 downhillPoint = {};
		glm::vec2 downhillDirection = {};
		float length = 0.f;
	};

	struct ZiplineRideTarget
	{
		int ziplineIndex = -1;
		glm::vec2 linePoint = {};
		float distanceAlong = 0.f;
		float distanceToFeet = 999999.f;
		ZiplineSlideData slide = {};
	};

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

	glm::vec4 getPlayerRectAtCenter(glm::vec2 center)
	{
		return {
			center.x - kPlayerWidth * 0.5f,
			center.y - kPlayerHeight * 0.5f,
			kPlayerWidth,
			kPlayerHeight
		};
	}

	glm::vec4 insetRect(glm::vec4 rect, float inset)
	{
		// Zipline grinding uses a slightly smaller temporary collision box so
		// nearby level geometry feels a bit more forgiving while still colliding.
		rect.x += inset;
		rect.y += inset;
		rect.z = std::max(rect.z - inset * 2.f, 0.01f);
		rect.w = std::max(rect.w - inset * 2.f, 0.01f);
		return rect;
	}

	bool playerRectFitsInsideRoom(Room const &room, glm::vec4 rect)
	{
		return
			rect.x >= 0.f &&
			rect.y >= 0.f &&
			rect.x + rect.z <= room.size.x &&
			rect.y + rect.w <= room.size.y;
	}

	bool playerRectHasGroundSupport(Room const &room, glm::vec4 rect)
	{
		glm::vec4 probe = {
			rect.x + 0.08f,
			rect.y + rect.w - 0.02f,
			std::max(rect.z - 0.16f, 0.20f),
			0.10f
		};

		return roomRectHitsSolid(room, probe);
	}

	bool playerTouchesWallOnSide(Player const &player, Room const &room, int side, float extraDistance = 0.f)
	{
		glm::vec4 bounds = player.physics.transform.getAABB();
		glm::vec4 probe = {};
		probe.y = bounds.y + kWallProbeInsetY;
		probe.w = std::max(bounds.w - kWallProbeInsetY * 2.f, 0.25f);

		if (side < 0)
		{
			probe.x = bounds.x - (kWallProbeWidth + extraDistance);
			probe.z = kWallProbeWidth + extraDistance + 0.02f;
		}
		else
		{
			probe.x = bounds.x + bounds.z - 0.02f;
			probe.z = kWallProbeWidth + extraDistance + 0.02f;
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

	bool getZiplineSlideData(Zipline const &zipline, ZiplineSlideData &slide)
	{
		glm::vec2 pointA = zipline.getPointCenter(0);
		glm::vec2 pointB = zipline.getPointCenter(1);
		if (pointB.y > pointA.y || (std::abs(pointB.y - pointA.y) < 0.001f && pointB.x > pointA.x))
		{
			slide.uphillPoint = pointA;
			slide.downhillPoint = pointB;
		}
		else
		{
			slide.uphillPoint = pointB;
			slide.downhillPoint = pointA;
		}

		glm::vec2 direction = slide.downhillPoint - slide.uphillPoint;
		slide.length = glm::length(direction);
		if (slide.length <= 0.001f)
		{
			return false;
		}

		slide.downhillDirection = direction / slide.length;
		return true;
	}

	float getClosestPointParam(glm::vec2 a, glm::vec2 b, glm::vec2 point)
	{
		glm::vec2 ab = b - a;
		float lengthSquared = glm::dot(ab, ab);
		if (lengthSquared <= 0.000001f)
		{
			return 0.f;
		}

		return std::clamp(glm::dot(point - a, ab) / lengthSquared, 0.f, 1.f);
	}

	glm::vec2 getZiplinePointAtDistance(ZiplineSlideData const &slide, float distanceAlong)
	{
		return slide.uphillPoint + slide.downhillDirection * std::clamp(distanceAlong, 0.f, slide.length);
	}

	bool findZiplineRideTarget(Player const &player, Room const &room, ZiplineRideTarget &target)
	{
		glm::vec4 playerBounds = player.physics.transform.getAABB();
		glm::vec2 feetSamples[3] = {
			{playerBounds.x + playerBounds.z * 0.20f, playerBounds.y + playerBounds.w},
			{playerBounds.x + playerBounds.z * 0.50f, playerBounds.y + playerBounds.w},
			{playerBounds.x + playerBounds.z * 0.80f, playerBounds.y + playerBounds.w},
		};

		for (int i = 0; i < static_cast<int>(room.ziplines.size()); i++)
		{
			ZiplineSlideData slide = {};
			if (!getZiplineSlideData(room.ziplines[i], slide))
			{
				continue;
			}

			for (glm::vec2 const &feetSample : feetSamples)
			{
				float param = getClosestPointParam(slide.uphillPoint, slide.downhillPoint, feetSample);
				glm::vec2 linePoint = glm::mix(slide.uphillPoint, slide.downhillPoint, param);
				float distanceToFeet = glm::distance(linePoint, feetSample);
				if (distanceToFeet > kZiplineAttachDistance)
				{
					continue;
				}

				// The rail needs to be under the player's lower half so jumping up
				// through it from below doesn't snap into a ride.
				if (linePoint.y < player.physics.transform.pos.y)
				{
					continue;
				}

				glm::vec2 rideCenter = {
					linePoint.x,
					linePoint.y - kPlayerHeight * 0.5f
				};
				glm::vec4 rideRect = getPlayerRectAtCenter(rideCenter);
				if (!playerRectFitsInsideRoom(room, rideRect) || roomRectHitsSolid(room, rideRect))
				{
					continue;
				}

				if (distanceToFeet < target.distanceToFeet)
				{
					target.ziplineIndex = i;
					target.linePoint = linePoint;
					target.distanceAlong = param * slide.length;
					target.distanceToFeet = distanceToFeet;
					target.slide = slide;
				}
			}
		}

		return target.ziplineIndex >= 0;
	}

	bool findWallClimbTarget(Player const &player, Room const &room, int wallSide, WallClimbTarget &target)
	{
		glm::vec4 playerBounds = player.physics.transform.getAABB();
		int minY = std::clamp(static_cast<int>(std::floor(playerBounds.y)), 0, room.size.y - 1);
		int maxY = std::clamp(static_cast<int>(std::floor(playerBounds.y + playerBounds.w - 0.02f)), 0, room.size.y - 1);
		int startWallX = wallSide > 0
			? static_cast<int>(std::floor(playerBounds.x + playerBounds.z + 0.02f))
			: static_cast<int>(std::floor(playerBounds.x - 0.02f));
		int endWallX = wallSide > 0
			? static_cast<int>(std::floor(playerBounds.x + playerBounds.z + kWallClimbWallPadding + 0.02f))
			: static_cast<int>(std::floor(playerBounds.x - kWallClimbWallPadding - 0.02f));

		int wallXStep = wallSide > 0 ? 1 : -1;
		for (int wallX = startWallX;
			wallSide > 0 ? wallX <= endWallX : wallX >= endWallX;
			wallX += wallXStep)
		{
			if (wallX < 0 || wallX >= room.size.x)
			{
				continue;
			}

			for (int y = minY; y <= maxY; y++)
			{
				if (!room.getBlockUnsafe(wallX, y).solid)
				{
					continue;
				}

				if (y > 0 && room.getBlockUnsafe(wallX, y - 1).solid)
				{
					continue;
				}

				float ledgeTopY = static_cast<float>(y);
				bool grabbedOnSameWall = player.wallGrabSide == wallSide;
				// If the player is already grabbing this wall, the touched top wall
				// tile is enough to allow the mantle. That keeps wall grab from
				// blocking the climb just because the slide pinned the player a bit
				// lower than the non-grab ledge test expects.
				if (!grabbedOnSameWall && playerBounds.y + 0.02f >= ledgeTopY)
				{
					continue;
				}

				glm::vec2 endCenter = {
					wallSide > 0
						? wallX + kPlayerWidth * 0.5f
						: wallX + 1.f - kPlayerWidth * 0.5f,
					ledgeTopY - kPlayerHeight * 0.5f
				};

				// If the player is already basically standing on that top surface,
				// don't start an extra mantle animation.
				if (player.physics.transform.pos.y <= endCenter.y + 0.05f)
				{
					continue;
				}

				glm::vec4 endRect = getPlayerRectAtCenter(endCenter);
				if (!playerRectFitsInsideRoom(room, endRect) || roomRectHitsSolid(room, endRect))
				{
					continue;
				}

				if (!playerRectHasGroundSupport(room, endRect))
				{
					continue;
				}

				glm::vec2 cornerCenter = {player.physics.transform.pos.x, endCenter.y};
				glm::vec4 cornerRect = getPlayerRectAtCenter(cornerCenter);
				if (!playerRectFitsInsideRoom(room, cornerRect) || roomRectHitsSolid(room, cornerRect))
				{
					continue;
				}

				glm::vec4 verticalSweep = {
					cornerRect.x,
					std::min(playerBounds.y, cornerRect.y),
					cornerRect.z,
					std::max(playerBounds.y + playerBounds.w, cornerRect.y + cornerRect.w) - std::min(playerBounds.y, cornerRect.y)
				};
				if (roomRectHitsSolid(room, verticalSweep))
				{
					continue;
				}

				glm::vec4 horizontalSweep = {
					std::min(cornerRect.x, endRect.x),
					endRect.y,
					std::max(cornerRect.x + cornerRect.z, endRect.x + endRect.z) - std::min(cornerRect.x, endRect.x),
					endRect.w
				};
				if (roomRectHitsSolid(room, horizontalSweep))
				{
					continue;
				}

				target.cornerCenter = cornerCenter;
				target.endCenter = endCenter;
				return true;
			}
		}

		return false;
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
	bool downPressed = !measureMode && (
		input.isButtonPressed(platform::Button::S) ||
		input.isButtonPressed(platform::Button::Down) ||
		input.controller.buttons[platform::Controller::Down].pressed ||
		input.controller.buttons[platform::Controller::Down].held ||
		input.controller.LStick.y < -0.55f);
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

	updatePlayer(gameplayDeltaTime, moveInput, jumpPressed, jumpHeld, downPressed, dashPressed, sprintHeld);
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
	player.doubleJumpAvailable = tuning.enableDoubleJump;
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
	player.wallGrabHoldTimer = 0.f;
	player.wallJumpGraceSide = 0;
	player.wallJumpGraceTimer = 0.f;
	player.wallJumpCarryVelocity = 0.f;
	player.glideArmedFromDoubleJump = false;
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

bool Gameplay::tryStartZiplineRide()
{
	if (!tuning.enableSprint || player.ziplineActive || player.ziplineDetachTimer > 0.f || player.dashActive || player.wallClimbActive)
	{
		return false;
	}

	ZiplineRideTarget target = {};
	if (!findZiplineRideTarget(player, room, target))
	{
		return false;
	}

	glm::vec2 incomingVelocity = {
		player.smoothedMoveInput * player.moveSpeed + player.wallJumpCarryVelocity,
		player.physics.velocity.y
	};

	// Riding a zipline becomes its own scripted state so it can ignore left/right
	// input and move only from the projected downhill gravity.
	player.ziplineActive = true;
	player.ziplineIndex = target.ziplineIndex;
	player.ziplineDistance = target.distanceAlong;
	player.ziplineSpeed = std::max(0.f, glm::dot(incomingVelocity, target.slide.downhillDirection));
	player.wallGrabSide = 0;
	player.wallGrabHoldTimer = 0.f;
	player.wallJumpGraceSide = 0;
	player.wallJumpGraceTimer = 0.f;
	player.lastWallGrabSide = 0;
	player.wallJumpCarryVelocity = 0.f;
	player.glideArmedFromDoubleJump = false;
	player.dashActive = false;
	player.jumpQueuedAfterDash = false;
	player.physics.transform.pos = {
		target.linePoint.x,
		target.linePoint.y - kPlayerHeight * 0.5f
	};
	player.physics.lastPosition = player.physics.transform.pos;
	player.physics.velocity = {};
	player.physics.acceleration = {};
	player.physics.upTouch = false;
	player.physics.downTouch = false;
	player.physics.leftTouch = false;
	player.physics.rightTouch = false;
	return true;
}

bool Gameplay::updateZiplineRide(float deltaTime, bool jumpPressed, bool downPressed, float jumpSpeed)
{
	if (!player.ziplineActive || player.ziplineIndex < 0 || player.ziplineIndex >= static_cast<int>(room.ziplines.size()))
	{
		player.ziplineActive = false;
		player.ziplineIndex = -1;
		player.ziplineDistance = 0.f;
		player.ziplineSpeed = 0.f;
		return false;
	}

	if (!tuning.enableSprint)
	{
		player.ziplineActive = false;
		player.ziplineIndex = -1;
		player.ziplineDistance = 0.f;
		player.ziplineSpeed = 0.f;
		player.ziplineDetachTimer = kZiplineDetachDelay;
		return false;
	}

	ZiplineSlideData slide = {};
	if (!getZiplineSlideData(room.ziplines[player.ziplineIndex], slide))
	{
		player.ziplineActive = false;
		player.ziplineIndex = -1;
		player.ziplineDistance = 0.f;
		player.ziplineSpeed = 0.f;
		player.ziplineDetachTimer = kZiplineDetachDelay;
		return false;
	}

	auto detachFromZipline = [&](glm::vec2 detachPoint, bool preserveZiplineMomentum)
	{
		player.ziplineActive = false;
		player.ziplineIndex = -1;
		player.ziplineDistance = 0.f;
		player.ziplineDetachTimer = kZiplineDetachDelay;
		player.physics.transform.pos = {
			detachPoint.x,
			detachPoint.y - kPlayerHeight * 0.5f
		};
		player.physics.lastPosition = player.physics.transform.pos;
		player.physics.upTouch = false;
		player.physics.downTouch = false;
		player.physics.leftTouch = false;
		player.physics.rightTouch = false;
		if (preserveZiplineMomentum)
		{
			player.wallJumpCarryVelocity = slide.downhillDirection.x * player.ziplineSpeed;
			player.physics.velocity.y = slide.downhillDirection.y * player.ziplineSpeed;
		}
		else
		{
			player.wallJumpCarryVelocity = 0.f;
			player.physics.velocity = {};
		}
		player.ziplineSpeed = 0.f;
	};

	glm::vec2 currentPoint = getZiplinePointAtDistance(slide, player.ziplineDistance);
	player.physics.transform.pos = {
		currentPoint.x,
		currentPoint.y - kPlayerHeight * 0.5f
	};
	player.physics.lastPosition = player.physics.transform.pos;
	player.physics.velocity = {};
	player.physics.acceleration = {};
	player.physics.upTouch = false;
	player.physics.downTouch = false;
	player.physics.leftTouch = false;
	player.physics.rightTouch = false;

	glm::vec4 currentRideRect = insetRect(
		getPlayerRectAtCenter(player.physics.transform.pos),
		kZiplineCollisionInset);
	if (roomRectHitsSolid(room, currentRideRect))
	{
		// Rails can pass near geometry, but the player should still lose the ride
		// if their body starts intersecting the world while grinding.
		detachFromZipline(currentPoint, true);
		return false;
	}

	if (jumpPressed)
	{
		detachFromZipline(currentPoint, false);
		player.physics.downTouch = true;
		player.coyoteTimer = tuning.coyoteTime;
		player.jumpBufferTimer = tuning.jumpBufferTime;
		player.wallGrabLockTimer = tuning.wallGrabDelayAfterGroundJump;
		return false;
	}

	if (downPressed)
	{
		detachFromZipline(currentPoint, false);
		player.physics.downTouch = false;
		player.coyoteTimer = 0.f;
		return false;
	}

	float downhillAcceleration = tuning.gravity * std::max(slide.downhillDirection.y, 0.f);
	player.ziplineSpeed += downhillAcceleration * deltaTime;
	player.ziplineDistance += player.ziplineSpeed * deltaTime;

	if (player.ziplineDistance >= slide.length)
	{
		glm::vec2 exitPoint = slide.downhillPoint;
		detachFromZipline(exitPoint, true);
		return false;
	}

	glm::vec2 nextPoint = getZiplinePointAtDistance(slide, player.ziplineDistance);
	glm::vec2 nextCenter = {
		nextPoint.x,
		nextPoint.y - kPlayerHeight * 0.5f
	};
	glm::vec4 nextRideRect = insetRect(
		getPlayerRectAtCenter(nextCenter),
		kZiplineCollisionInset);
	if (roomRectHitsSolid(room, nextRideRect))
	{
		detachFromZipline(currentPoint, true);
		return false;
	}

	player.physics.transform.pos = {
		nextCenter.x,
		nextCenter.y
	};
	player.physics.lastPosition = player.physics.transform.pos;
	return true;
}

bool Gameplay::tryStartWallClimb(float moveInput)
{
	if (!tuning.enableWallClimb || player.wallClimbActive || player.dashActive)
	{
		return false;
	}

	int wallSide = getMoveDirection(moveInput);
	if (wallSide == 0)
	{
		return false;
	}

	// Wall climb should win over wall grab when the player keeps pressing into
	// the ledge wall and there is a valid mantle target above.
	if (player.wallGrabSide != 0 && wallSide != player.wallGrabSide)
	{
		return false;
	}

	if (!playerTouchesWallOnSide(player, room, wallSide, kWallClimbWallPadding))
	{
		return false;
	}

	WallClimbTarget target = {};
	if (!findWallClimbTarget(player, room, wallSide, target))
	{
		return false;
	}

	startWallClimb(wallSide, target.cornerCenter, target.endCenter);
	return true;
}

void Gameplay::startWallClimb(int wallSide, glm::vec2 cornerCenter, glm::vec2 endCenter)
{
	// Wall climb is a small scripted mantle: first lift the player above the lip,
	// then slide them onto the platform top. The path stays deterministic and
	// doesn't depend on the regular velocity solver.
	player.wallClimbActive = true;
	player.wallClimbSide = wallSide;
	player.wallClimbTimer = 0.f;
	player.wallClimbStart = player.physics.transform.pos;
	player.wallClimbCorner = cornerCenter;
	player.wallClimbEnd = endCenter;

	float pathDistance =
		std::abs(player.wallClimbCorner.y - player.wallClimbStart.y) +
		std::abs(player.wallClimbEnd.x - player.wallClimbCorner.x);
	float referenceDistance = kPlayerWidth + kPlayerHeight;
	float distanceRatio = pathDistance / std::max(referenceDistance, 0.001f);
	player.wallClimbDuration = std::max(
		kMinWallClimbDuration,
		tuning.wallClimbDuration * std::max(distanceRatio, 0.20f));

	player.wallGrabSide = 0;
	player.wallGrabHoldTimer = 0.f;
	player.wallJumpGraceSide = 0;
	player.wallJumpGraceTimer = 0.f;
	player.lastWallGrabSide = 0;
	player.wallJumpCarryVelocity = 0.f;
	player.jumpBufferTimer = 0.f;
	player.jumpQueuedAfterDash = false;
	player.glideArmedFromDoubleJump = false;
	player.coyoteTimer = 0.f;
	player.physics.velocity = {};
	player.physics.acceleration = {};
	player.physics.upTouch = false;
	player.physics.downTouch = false;
	player.physics.leftTouch = false;
	player.physics.rightTouch = false;
	player.physics.lastPosition = player.physics.transform.pos;
}

bool Gameplay::updateWallClimb(float deltaTime)
{
	player.wallClimbTimer += deltaTime;
	float duration = std::max(player.wallClimbDuration, kMinWallClimbDuration);
	float progress = easing::clamp01(player.wallClimbTimer / duration);

	glm::vec2 newCenter = player.wallClimbEnd;
	if (progress < kWallClimbVerticalPhase)
	{
		float phase = easing::easeOutCubic(progress / kWallClimbVerticalPhase);
		newCenter = glm::mix(player.wallClimbStart, player.wallClimbCorner, phase);
	}
	else
	{
		float phase = easing::easeOutCubic((progress - kWallClimbVerticalPhase) / (1.f - kWallClimbVerticalPhase));
		newCenter = glm::mix(player.wallClimbCorner, player.wallClimbEnd, phase);
	}

	player.physics.transform.pos = newCenter;
	player.physics.lastPosition = newCenter;
	player.physics.velocity = {};
	player.physics.acceleration = {};
	player.physics.upTouch = false;
	player.physics.downTouch = false;
	player.physics.leftTouch = false;
	player.physics.rightTouch = false;

	if (progress >= 1.f)
	{
		player.wallClimbActive = false;
		player.wallClimbSide = 0;
		player.physics.transform.pos = player.wallClimbEnd;
		player.physics.lastPosition = player.wallClimbEnd;
		player.physics.downTouch = playerTouchesGround(player, room);
	}

	return player.wallClimbActive;
}

void Gameplay::updateWallGrabState(float moveInput)
{
	if (!tuning.enableWallGrab)
	{
		player.wallGrabSide = 0;
		player.wallGrabHoldTimer = 0.f;
		player.wallJumpGraceSide = 0;
		player.wallJumpGraceTimer = 0.f;
		player.lastWallGrabSide = 0;
		player.rememberedWallInputSide = 0;
		player.rememberedWallInputTimer = 0.f;
		return;
	}

	if (player.physics.downTouch)
	{
		player.wallGrabSide = 0;
		player.wallGrabHoldTimer = 0.f;
		player.wallJumpGraceSide = 0;
		player.wallJumpGraceTimer = 0.f;
		player.lastWallGrabSide = 0;
		return;
	}

	int inputWallSide = getMoveDirection(moveInput);
	int desiredWallSide = inputWallSide;
	if (desiredWallSide == 0 && player.rememberedWallInputTimer > 0.f)
	{
		desiredWallSide = player.rememberedWallInputSide;
	}

	if (player.wallGrabSide != 0)
	{
		bool stillTouchingGrabWall = playerTouchesWallOnSide(player, room, player.wallGrabSide);

		if (!stillTouchingGrabWall)
		{
			player.wallGrabSide = 0;
			player.wallGrabHoldTimer = 0.f;
		}
		else if (inputWallSide == -player.wallGrabSide)
		{
			// Let opposite input drop the latch, but keep a tiny wall-jump grace
			// window so releasing and then pressing jump still feels responsive.
			player.wallJumpGraceSide = player.wallGrabSide;
			player.wallJumpGraceTimer = kWallJumpReleaseGraceTime;
			player.wallGrabSide = 0;
			player.wallGrabHoldTimer = 0.f;
		}
		else
		{
			WallClimbTarget climbTarget = {};
			if (inputWallSide == player.wallGrabSide &&
				tuning.enableWallClimb &&
				findWallClimbTarget(player, room, player.wallGrabSide, climbTarget))
			{
				startWallClimb(player.wallGrabSide, climbTarget.cornerCenter, climbTarget.endCenter);
			}
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

	WallClimbTarget climbTarget = {};
	if (tuning.enableWallClimb && findWallClimbTarget(player, room, desiredWallSide, climbTarget))
	{
		// If a valid ledge climb exists, start it immediately instead of letting
		// wall grab steal the motion and pin the player to the wall.
		startWallClimb(desiredWallSide, climbTarget.cornerCenter, climbTarget.endCenter);
		return;
	}

	// Wall grabs snap the vertical speed back to a controlled slide so the player
	// can catch the wall cleanly before the slower fall ramps in again.
	player.wallGrabSide = canGrabLeftWall ? -1 : 1;
	player.wallGrabHoldTimer = tuning.wallGrabHoldTime;
	player.wallJumpCarryVelocity = 0.f;
	player.physics.velocity.y = 0.f;
}

void Gameplay::updatePlayer(float deltaTime, float moveInput, bool jumpPressed, bool jumpHeld, bool downPressed, bool dashPressed, bool sprintHeld)
{
	if (moveInput != 0.f)
	{
		player.lastMoveDirection = getMoveDirection(moveInput);
	}

	float jumpRiseGravity = tuning.gravity * std::max(tuning.jumpRiseGravityMultiplier, 0.01f);
	float jumpSpeed = std::sqrt(2.f * jumpRiseGravity * tuning.jumpHeight);
	float doubleJumpSpeed = std::sqrt(2.f * jumpRiseGravity * tuning.doubleJumpHeight);

	player.dashCooldownTimer -= deltaTime;
	player.dashCooldownTimer = std::max(player.dashCooldownTimer, 0.f);
	player.ziplineDetachTimer -= deltaTime;
	player.ziplineDetachTimer = std::max(player.ziplineDetachTimer, 0.f);

	if (player.dashActive && !tuning.enableDash)
	{
		player.dashActive = false;
	}

	if (player.ziplineActive)
	{
		if (updateZiplineRide(deltaTime, jumpPressed, downPressed, jumpSpeed))
		{
			return;
		}
	}

	if (player.wallClimbActive)
	{
		updateWallClimb(deltaTime);
		refreshDashAvailability();
		return;
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
		player.wallGrabHoldTimer = 0.f;
		player.lastWallGrabSide = 0;
		player.rememberedWallInputSide = 0;
		player.rememberedWallInputTimer = 0.f;
		player.wallRegrabTimer = 0.f;
		player.wallGrabLockTimer = 0.f;
	}

	if (grounded)
	{
		player.coyoteTimer = tuning.coyoteTime;
		player.doubleJumpAvailable = tuning.enableDoubleJump;
		player.glideArmedFromDoubleJump = false;
		player.wallJumpGraceSide = 0;
		player.wallJumpGraceTimer = 0.f;
		player.wallJumpCarryVelocity = 0.f;
		player.wallRegrabTimer = 0.f;
		player.wallGrabLockTimer = 0.f;
		player.wallGrabHoldTimer = 0.f;
	}
	else
	{
		player.coyoteTimer -= deltaTime;
		player.coyoteTimer = std::max(player.coyoteTimer, 0.f);
		player.wallRegrabTimer -= deltaTime;
		player.wallRegrabTimer = std::max(player.wallRegrabTimer, 0.f);
		player.wallGrabLockTimer -= deltaTime;
		player.wallGrabLockTimer = std::max(player.wallGrabLockTimer, 0.f);
		player.wallJumpGraceTimer -= deltaTime;
		player.wallJumpGraceTimer = std::max(player.wallJumpGraceTimer, 0.f);
		if (player.wallJumpGraceTimer <= 0.f)
		{
			player.wallJumpGraceSide = 0;
		}
	}

	if (!tuning.enableGlide)
	{
		player.glideActive = false;
		player.glideTimer = 0.f;
		player.glideArmedFromDoubleJump = false;
	}

	if (!tuning.enableDoubleJump)
	{
		player.doubleJumpAvailable = false;
		player.glideArmedFromDoubleJump = false;
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

	if (tryStartWallClimb(moveInput))
	{
		refreshDashAvailability();
		return;
	}

	if ((player.wallGrabSide != 0 || player.wallJumpGraceTimer > 0.f) && player.jumpBufferTimer > 0.f)
	{
		int previousWallGrabSide = player.wallGrabSide != 0
			? player.wallGrabSide
			: player.wallJumpGraceSide;
		player.wallGrabSide = 0;
		player.lastWallGrabSide = previousWallGrabSide;
		player.wallRegrabTimer = tuning.wallRegrabTime;
		player.wallJumpGraceSide = 0;
		player.wallJumpGraceTimer = 0.f;
		// Wall jumps use the normal jump height, but also nudge the player away
		// from the wall right away so the launch feels more like a ground jump.
		player.physics.transform.pos.x += -previousWallGrabSide * tuning.wallJumpDetachDistance;
		player.wallJumpCarryVelocity = -previousWallGrabSide * tuning.wallJumpPushSpeed;
		player.physics.velocity.y = -jumpSpeed;
		player.physics.downTouch = false;
		player.coyoteTimer = 0.f;
		player.jumpBufferTimer = 0.f;
		player.glideArmedFromDoubleJump = false;
	}

	if (player.wallGrabSide == 0 && player.jumpBufferTimer > 0.f && player.coyoteTimer > 0.f)
	{
		player.physics.velocity.y = -jumpSpeed;
		player.physics.downTouch = false;
		player.coyoteTimer = 0.f;
		player.jumpBufferTimer = 0.f;
		player.glideArmedFromDoubleJump = false;
		player.wallGrabHoldTimer = 0.f;
		player.wallGrabLockTimer = tuning.wallGrabDelayAfterGroundJump;
	}

	if (player.wallGrabSide != 0 && downPressed)
	{
		int releasedWallSide = player.wallGrabSide;
		player.wallGrabSide = 0;
		player.wallGrabHoldTimer = 0.f;
		player.wallJumpGraceSide = 0;
		player.wallJumpGraceTimer = 0.f;
		player.lastWallGrabSide = releasedWallSide;
		player.wallRegrabTimer = tuning.wallRegrabTime;
		player.rememberedWallInputSide = 0;
		player.rememberedWallInputTimer = 0.f;
	}

	bool canDoubleJump =
		tuning.enableDoubleJump &&
		player.doubleJumpAvailable &&
		player.wallGrabSide == 0 &&
		!grounded &&
		player.coyoteTimer <= 0.f &&
		!player.dashActive &&
		!player.wallClimbActive &&
		!player.ziplineActive &&
		player.jumpBufferTimer > 0.f;
	if (canDoubleJump)
	{
		// Double jump spends the buffered air jump before glide gets a chance so
		// the second jump press always prioritizes the extra jump.
		player.physics.velocity.y = -doubleJumpSpeed;
		player.physics.downTouch = false;
		player.jumpBufferTimer = 0.f;
		player.doubleJumpAvailable = false;
		player.glideArmedFromDoubleJump = true;
		player.glideActive = false;
		player.glideTimer = 0.f;
		player.wallGrabHoldTimer = 0.f;
		player.wallJumpGraceSide = 0;
		player.wallJumpGraceTimer = 0.f;
	}

	bool canStartGlide =
		tuning.enableGlide &&
		!grounded &&
		player.coyoteTimer <= 0.f &&
		player.wallGrabSide == 0 &&
		!player.dashActive &&
		!player.wallClimbActive &&
		!player.ziplineActive &&
		player.physics.velocity.y > 0.01f &&
		(
			(player.glideArmedFromDoubleJump && jumpHeld) ||
			(jumpPressed && !player.doubleJumpAvailable)
		);
	if (canStartGlide)
	{
		// Glide can come either from a fresh falling jump press or from holding
		// the same press that triggered the double jump once the player starts
		// falling again.
		player.glideActive = true;
		player.glideTimer = 0.f;
		player.glideArmedFromDoubleJump = false;
		player.jumpBufferTimer = 0.f;
	}

	bool keepGliding =
		player.glideActive &&
		tuning.enableGlide &&
		!grounded &&
		player.wallGrabSide == 0 &&
		!player.dashActive &&
		!player.wallClimbActive &&
		!player.ziplineActive &&
		jumpHeld &&
		player.physics.velocity.y > 0.01f;
	if (!keepGliding)
	{
		player.glideActive = false;
		player.glideTimer = 0.f;
	}
	if (!jumpHeld && player.glideArmedFromDoubleJump)
	{
		player.glideArmedFromDoubleJump = false;
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

		float currentMaxFallSpeed = tuning.maxFallSpeed;
		if (player.glideActive)
		{
			player.glideTimer += deltaTime;
			float glideBlend = easing::clamp01(
				player.glideTimer / std::max(tuning.glideEnterTime, 0.0001f));
			glideBlend = easing::easeOutCubic(glideBlend);
			currentMaxFallSpeed = glm::mix(tuning.maxFallSpeed, tuning.glideFallSpeed, glideBlend);
		}

		float appliedGravity = player.physics.velocity.y < 0.f
			? jumpRiseGravity
			: tuning.gravity;
		player.physics.velocity.y += appliedGravity * deltaTime;
		player.physics.velocity.y = std::min(player.physics.velocity.y, currentMaxFallSpeed);
	}
	else
	{
		player.glideActive = false;
		player.glideTimer = 0.f;
		player.wallJumpCarryVelocity = 0.f;
		int wallInputSide = getMoveDirection(moveInput);
		player.wallHoldActive =
			tuning.enableWallGrab &&
			tuning.enableWallHold &&
			wallInputSide == player.wallGrabSide;
		player.wallGrabHoldTimer = std::max(player.wallGrabHoldTimer - deltaTime, 0.f);

		// Fresh grabs can still do their short built-in hang, but when wall hold is
		// enabled the player can also actively freeze the slide again by pushing
		// back into the wall.
		bool shouldHoldWall =
			player.wallHoldActive ||
			(!tuning.enableWallHold && player.wallGrabHoldTimer > 0.f);

		if (shouldHoldWall)
		{
			player.physics.velocity.y = 0.f;
		}
		else
		{
			player.physics.velocity.y += tuning.wallSlideGravity * deltaTime;
			player.physics.velocity.y = std::min(player.physics.velocity.y, tuning.wallSlideSpeed);
		}
	}

	player.physics.velocity.x = 0.f;
	player.physics.updateForces(deltaTime, {0.f, 0.f});
	player.physics.resolveConstrains(room);
	player.physics.updateFinal();
	if (player.wallGrabSide == 0)
	{
		player.wallHoldActive = false;
	}
	if (player.physics.downTouch)
	{
		player.glideActive = false;
		player.glideTimer = 0.f;
		player.glideArmedFromDoubleJump = false;
	}
	if (tryStartZiplineRide())
	{
		return;
	}
	updateWallGrabState(moveInput);
	if (tryStartWallClimb(moveInput))
	{
		refreshDashAvailability();
		return;
	}
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
	const gl2d::Color4f ziplineLineColor = {0.76f, 0.78f, 0.80f, 0.82f};
	const gl2d::Color4f ziplinePointColor = {0.94f, 0.84f, 0.28f, 0.92f};
	constexpr float ziplineLineWidth = 0.05f;
	constexpr float ziplinePointSize = 0.32f;

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

	// Ziplines are pure room markers for now, so gameplay just renders their path and anchors.
	for (Zipline const &zipline : room.ziplines)
	{
		renderer.renderLine(
			zipline.getPointCenter(0),
			zipline.getPointCenter(1),
			ziplineLineColor,
			ziplineLineWidth);

		for (glm::ivec2 const &point : zipline.points)
		{
			renderer.renderRectangle(
				{
					point.x + 0.5f - ziplinePointSize * 0.5f,
					point.y + 0.5f - ziplinePointSize * 0.5f,
					ziplinePointSize,
					ziplinePointSize
				},
				ziplinePointColor);
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
	if (player.wallClimbActive)
	{
		playerColor = {0.88f, 0.92f, 1.0f, 1.f};
	}
	if (player.glideActive)
	{
		playerColor = {0.76f, 0.98f, 0.66f, 1.f};
	}
	if (player.ziplineActive)
	{
		playerColor = {1.0f, 0.94f, 0.34f, 1.f};
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
		ImGui::TextUnformatted("Jump again in air to double jump, then hold to glide");
		ImGui::TextUnformatted("Wall grab latches once started, Down drops it");
		ImGui::TextUnformatted("Hold toward the wall to freeze a wall slide in place");
		ImGui::TextUnformatted("Press jump again while falling to glide");
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
		ImGui::Checkbox("Enable Double Jump", &tuning.enableDoubleJump);
		ImGui::Checkbox("Enable Glide", &tuning.enableGlide);
		ImGui::Checkbox("Enable Wall Grab", &tuning.enableWallGrab);
		ImGui::Checkbox("Enable Wall Hold", &tuning.enableWallHold);
		ImGui::Checkbox("Enable Wall Climb", &tuning.enableWallClimb);
		ImGui::SliderFloat("Move Speed", &tuning.moveSpeed, 0.5f, 30.f, "%.2f");
		ImGui::SliderFloat("Sprint Speed", &tuning.sprintMoveSpeed, 0.5f, 40.f, "%.2f");
		ImGui::SliderFloat("Move Start Ease", &tuning.moveStartEaseTime, 0.001f, 0.15f, "%.3f");
		ImGui::SliderFloat("Move Stop Ease", &tuning.moveStopEaseTime, 0.001f, 0.15f, "%.3f");
		ImGui::SliderFloat("Dash Distance", &tuning.dashDistance, 0.5f, 24.f, "%.2f");
		ImGui::SliderFloat("Dash Time", &tuning.dashDuration, 0.02f, 0.40f, "%.3f");
		ImGui::SliderFloat("Dash Cooldown", &tuning.dashCooldownTime, 0.f, 1.f, "%.2f");
		ImGui::SliderFloat("Jump Height", &tuning.jumpHeight, 0.5f, 20.f, "%.2f");
		ImGui::SliderFloat("Double Jump Height", &tuning.doubleJumpHeight, 0.5f, 20.f, "%.2f");
		ImGui::SliderFloat("Jump Snappiness", &tuning.jumpRiseGravityMultiplier, 0.25f, 4.f, "%.2f");
		ImGui::SliderFloat("Glide Fall Speed", &tuning.glideFallSpeed, 0.5f, 30.f, "%.2f");
		ImGui::SliderFloat("Glide Enter Time", &tuning.glideEnterTime, 0.f, 0.20f, "%.3f");
		ImGui::SliderFloat("Wall Jump Detach", &tuning.wallJumpDetachDistance, 0.f, 3.f, "%.2f");
		ImGui::SliderFloat("Wall Jump Push", &tuning.wallJumpPushSpeed, 0.f, 40.f, "%.2f");
		ImGui::SliderFloat("Wall Jump Drag", &tuning.wallJumpCarryDrag, 1.f, 120.f, "%.2f");
		ImGui::SliderFloat("Gravity", &tuning.gravity, 1.f, 120.f, "%.2f");
		ImGui::SliderFloat("Max Fall Speed", &tuning.maxFallSpeed, 1.f, 80.f, "%.2f");
		ImGui::SliderFloat("Wall Grab Hold", &tuning.wallGrabHoldTime, 0.f, 0.30f, "%.2f");
		ImGui::SliderFloat("Wall Slide Gravity", &tuning.wallSlideGravity, 0.5f, 80.f, "%.2f");
		ImGui::SliderFloat("Wall Slide Speed", &tuning.wallSlideSpeed, 0.5f, 30.f, "%.2f");
		ImGui::SliderFloat("Wall Climb Time", &tuning.wallClimbDuration, 0.05f, 0.60f, "%.3f");
		ImGui::SliderFloat("Jump Cut", &tuning.jumpCutMultiplier, 0.05f, 1.f, "%.2f");
		ImGui::SliderFloat("Coyote Time", &tuning.coyoteTime, 0.f, 0.30f, "%.2f");
		ImGui::SliderFloat("Jump Buffer", &tuning.jumpBufferTime, 0.f, 0.30f, "%.2f");
		ImGui::SliderFloat("Wall Input Memory", &tuning.wallInputMemoryTime, 0.f, 0.30f, "%.2f");
		ImGui::SliderFloat("Wall Regrab Time", &tuning.wallRegrabTime, 0.f, 0.30f, "%.2f");
		ImGui::SliderFloat("Ground Jump Wall Delay", &tuning.wallGrabDelayAfterGroundJump, 0.f, 0.30f, "%.2f");
		float jumpRiseGravity = tuning.gravity * std::max(tuning.jumpRiseGravityMultiplier, 0.01f);
		float jumpSpeed = std::sqrt(2.f * jumpRiseGravity * tuning.jumpHeight);
		float doubleJumpSpeed = std::sqrt(2.f * jumpRiseGravity * tuning.doubleJumpHeight);
		float jumpApexTime = jumpSpeed / std::max(jumpRiseGravity, 0.001f);
		ImGui::Text("Derived Jump Speed: %.2f", jumpSpeed);
		ImGui::Text("Derived Double Jump Speed: %.2f", doubleJumpSpeed);
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
		ImGui::Text("Wall: grab %d  hold %.2f  active %d  regrab %.2f  lock %.2f",
			player.wallGrabSide,
			player.wallGrabHoldTimer,
			player.wallHoldActive ? 1 : 0,
			player.wallRegrabTimer,
			player.wallGrabLockTimer);
		ImGui::Text("Climb: active %d  timer %.2f / %.2f",
			player.wallClimbActive ? 1 : 0,
			player.wallClimbTimer,
			player.wallClimbDuration);
		ImGui::Text("Glide: active %d  timer %.2f",
			player.glideActive ? 1 : 0,
			player.glideTimer);
		ImGui::Text("Double Jump: available %d  glide arm %d",
			player.doubleJumpAvailable ? 1 : 0,
			player.glideArmedFromDoubleJump ? 1 : 0);
		ImGui::Text("Zipline: active %d  speed %.2f  detach %.2f",
			player.ziplineActive ? 1 : 0,
			player.ziplineSpeed,
			player.ziplineDetachTimer);
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
