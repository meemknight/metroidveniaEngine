#include "gameLayer.h"
#include "player.h"
#include "room.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <glm/glm.hpp>
#include <platformTools.h>
#include <logs.h>
#include <SDL3/SDL.h>
#include <gl2d/gl2d.h>

gl2d::Renderer2D renderer;

namespace
{
	constexpr float kGravity = 64.f;
	constexpr float kJumpSpeed = 20.5f;
	constexpr float kMaxFallSpeed = 30.f;
	constexpr float kGroundSpeed = 14.f;
	constexpr float kAirSpeed = 10.f;
	constexpr float kGroundAcceleration = 120.f;
	constexpr float kGroundDeceleration = 140.f;
	constexpr float kAirAcceleration = 48.f;
	constexpr float kAirDeceleration = 36.f;
	constexpr float kCoyoteTime = 0.08f;
	constexpr float kJumpBufferTime = 0.20f;
	constexpr float kJumpReleaseMultiplier = 0.4f;
	constexpr float kCameraFollowStrength = 10.f;

	constexpr float cameraZoom = 32;

	Room gRoom;
	Player gPlayer;
	gl2d::Camera gCamera;
	glm::vec2 gSpawnPoint = {};

	float approach(float current, float target, float maxDelta)
	{
		if (current < target)
		{
			return std::min(current + maxDelta, target);
		}

		return std::max(current - maxDelta, target);
	}

	bool isSolid(const Room &room, int x, int y)
	{
		if (x < 0 || x >= room.size.x || y >= room.size.y)
		{
			return true;
		}

		if (y < 0)
		{
			return false;
		}

		const Block *block = room.getBlockSafe(x, y);
		return block && block->solid;
	}

	void fillSolidRect(Room &room, int minX, int minY, int maxX, int maxY)
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

	void respawnPlayer()
	{
		gPlayer.position = gSpawnPoint;
		gPlayer.velocity = {};
		gPlayer.grounded = false;
		gPlayer.coyoteTimer = 0.f;
		gPlayer.jumpBufferTimer = 0.f;
	}

	void createStarterRoom()
	{
		gRoom.create(160, 48);

		fillSolidRect(gRoom, 0, 42, gRoom.size.x - 1, gRoom.size.y - 1);
		fillSolidRect(gRoom, 0, 0, 0, gRoom.size.y - 1);
		fillSolidRect(gRoom, gRoom.size.x - 1, 0, gRoom.size.x - 1, gRoom.size.y - 1);

		fillSolidRect(gRoom, 10, 32, 26, 32);
		fillSolidRect(gRoom, 34, 28, 48, 28);
		fillSolidRect(gRoom, 58, 24, 74, 24);
		fillSolidRect(gRoom, 82, 20, 96, 20);
		fillSolidRect(gRoom, 104, 26, 116, 26);
		fillSolidRect(gRoom, 124, 18, 138, 18);

		gSpawnPoint = {14.f, 32.f - gPlayer.size.y};

		gCamera = {};
		gCamera.zoom = cameraZoom;
		respawnPlayer();
	}

	bool isStandingOnGround(const Player &player, const Room &room)
	{
		const int minTileX = static_cast<int>(std::floor(player.position.x + 0.001f));
		const int maxTileX = static_cast<int>(std::floor(player.position.x + player.size.x - 0.001f));
		const int tileY = static_cast<int>(std::floor(player.position.y + player.size.y + 0.05f));

		for (int x = minTileX; x <= maxTileX; x++)
		{
			if (isSolid(room, x, tileY))
			{
				return true;
			}
		}

		return false;
	}

	void movePlayerHorizontal(Player &player, const Room &room, float deltaTime)
	{
		player.position.x += player.velocity.x * deltaTime;

		const int minTileY = static_cast<int>(std::floor(player.position.y));
		const int maxTileY = static_cast<int>(std::floor(player.position.y + player.size.y - 0.001f));

		if (player.velocity.x > 0.f)
		{
			const int rightTile = static_cast<int>(std::floor(player.position.x + player.size.x - 0.001f));
			for (int y = minTileY; y <= maxTileY; y++)
			{
				if (isSolid(room, rightTile, y))
				{
					player.position.x = rightTile - player.size.x;
					player.velocity.x = 0.f;
					return;
				}
			}
		}
		else if (player.velocity.x < 0.f)
		{
			const int leftTile = static_cast<int>(std::floor(player.position.x));
			for (int y = minTileY; y <= maxTileY; y++)
			{
				if (isSolid(room, leftTile, y))
				{
					player.position.x = leftTile + 1.f;
					player.velocity.x = 0.f;
					return;
				}
			}
		}
	}

	bool movePlayerVertical(Player &player, const Room &room, float deltaTime)
	{
		player.position.y += player.velocity.y * deltaTime;

		const int minTileX = static_cast<int>(std::floor(player.position.x + 0.001f));
		const int maxTileX = static_cast<int>(std::floor(player.position.x + player.size.x - 0.001f));

		if (player.velocity.y > 0.f)
		{
			const int bottomTile = static_cast<int>(std::floor(player.position.y + player.size.y - 0.001f));
			for (int x = minTileX; x <= maxTileX; x++)
			{
				if (isSolid(room, x, bottomTile))
				{
					player.position.y = bottomTile - player.size.y;
					player.velocity.y = 0.f;
					return true;
				}
			}
		}
		else if (player.velocity.y < 0.f)
		{
			const int topTile = static_cast<int>(std::floor(player.position.y));
			for (int x = minTileX; x <= maxTileX; x++)
			{
				if (isSolid(room, x, topTile))
				{
					player.position.y = topTile + 1.f;
					player.velocity.y = 0.f;
					return false;
				}
			}
		}

		return false;
	}

	void updatePlayer(float deltaTime, float moveInput, bool jumpPressed, bool jumpReleased)
	{
		if (gPlayer.grounded)
		{
			gPlayer.coyoteTimer = kCoyoteTime;
		}
		else
		{
			gPlayer.coyoteTimer = std::max(0.f, gPlayer.coyoteTimer - deltaTime);
		}

		if (jumpPressed)
		{
			gPlayer.jumpBufferTimer = kJumpBufferTime;
		}
		else
		{
			gPlayer.jumpBufferTimer = std::max(0.f, gPlayer.jumpBufferTimer - deltaTime);
		}

		const float desiredSpeed = moveInput * (gPlayer.grounded ? kGroundSpeed : kAirSpeed);
		const float acceleration = (moveInput != 0.f)
			? (gPlayer.grounded ? kGroundAcceleration : kAirAcceleration)
			: (gPlayer.grounded ? kGroundDeceleration : kAirDeceleration);

		gPlayer.velocity.x = approach(gPlayer.velocity.x, desiredSpeed, acceleration * deltaTime);

		if (gPlayer.jumpBufferTimer > 0.f && gPlayer.coyoteTimer > 0.f)
		{
			gPlayer.velocity.y = -kJumpSpeed;
			gPlayer.grounded = false;
			gPlayer.coyoteTimer = 0.f;
			gPlayer.jumpBufferTimer = 0.f;
		}

		gPlayer.velocity.y = std::min(gPlayer.velocity.y + kGravity * deltaTime, kMaxFallSpeed);

		if (jumpReleased && gPlayer.velocity.y < 0.f)
		{
			gPlayer.velocity.y *= kJumpReleaseMultiplier;
		}

		movePlayerHorizontal(gPlayer, gRoom, deltaTime);
		const bool landed = movePlayerVertical(gPlayer, gRoom, deltaTime);

		gPlayer.grounded = landed || isStandingOnGround(gPlayer, gRoom);
		if (gPlayer.grounded)
		{
			gPlayer.coyoteTimer = kCoyoteTime;
		}
	}

	void updateCamera(int width, int height, float deltaTime)
	{

		glm::vec2 roomSize = glm::vec2(gRoom.size);
		const glm::vec2 viewSize = glm::vec2(width, height) / gCamera.zoom;
		glm::vec2 maxCamera = roomSize - viewSize;
		maxCamera.x = std::max(maxCamera.x, 0.f);
		maxCamera.y = std::max(maxCamera.y, 0.f);

		gCamera.follow(gPlayer.getCenter(), deltaTime * (kCameraFollowStrength * 10.f),
			0, 0.f, width, height);
		//gCamera.position = glm::clamp(gCamera.position, glm::vec2(0.f), maxCamera);
	}

	void drawRoom()
	{
		const gl2d::Color4f roomBackground = {0.10f, 0.11f, 0.15f, 1.f};
		const gl2d::Color4f blockColor = {0.23f, 0.26f, 0.32f, 1.f};

		renderer.renderRectangle({0.f, 0.f, float(gRoom.size.x), float(gRoom.size.y)}, roomBackground);

		for (int y = 0; y < gRoom.size.y; y++)
		{
			for (int x = 0; x < gRoom.size.x; x++)
			{
				const Block &block = gRoom.getBlockUnsafe(x, y);
				if (!block.solid)
				{
					continue;
				}

				renderer.renderRectangle(
					{float(x), float(y), 1.f, 1.f},
					blockColor);
			}
		}
	}

	void drawPlayer()
	{
		const gl2d::Color4f playerColor = gPlayer.grounded
			? gl2d::Color4f{1.0f, 0.63f, 0.19f, 1.f}
			: gl2d::Color4f{1.0f, 0.84f, 0.30f, 1.f};

		renderer.renderRectangle({gPlayer.position.x, gPlayer.position.y, gPlayer.size.x, gPlayer.size.y}, playerColor);
	}
}

// Rebuilds shader binaries in development and reloads GPU shader objects.
static void tryHotReloadShaders()
{
#if defined(_WIN32) && defined(DEVELOPLEMT_BUILD) && (DEVELOPLEMT_BUILD == 1)
	std::string scriptPath = std::string(RESOURCES_PATH) + "shaders/compile_all_shaders.bat";
	for (char &c : scriptPath)
	{
		if (c == '/') { c = '\\'; }
	}

	std::ifstream scriptFile(scriptPath);
	if (!scriptFile.is_open())
	{
		platform::log("Shader reload failed: missing resources\\shaders\\compile_all_shaders.bat",
			LogManager::logError);
		return;
	}

	const std::string command = std::string("cmd.exe /C call \"") + scriptPath + "\"";
	int result = std::system(command.c_str());
	if (result != 0)
	{
		platform::log("Shader reload failed: compile_all_shaders.bat returned an error",
			LogManager::logError);
		return;
	}

	renderer.reloadGpuShaders();

	platform::log("Hot reloaded shaders", LogManager::logNormal);
#else
	platform::log("Shader reload is only enabled on Windows development builds", LogManager::logWarning);
#endif
}

gl2d::Renderer2D &getRenderer()
{
	return renderer;
}

bool initGame(SDL_Renderer *sdlRenderer)
{
	gl2d::init();

	renderer.create(sdlRenderer);
#if GL2D_USE_SDL_GPU
	if (!renderer.gpuDevice)
	{
		platform::log("SDL_gpu device unavailable on this platform; GPU post-process effects are disabled.",
			LogManager::logWarning);
	}
#endif

	createStarterRoom();

	return true;
}

bool gameLogic(float deltaTime, platform::Input &input, SDL_Renderer *sdlRenderer)
{
#pragma region init stuff
	int w = platform::getFrameBufferSizeX();
	int h = platform::getFrameBufferSizeY();

	renderer.updateWindowMetrics(w, h);

	if (input.isButtonPressed(platform::Button::F1))
	{
		platform::setFullScreen(!platform::isFullScreen());
	}

	if (input.isButtonPressed(platform::Button::F5))
	{
		tryHotReloadShaders();
	}

	if (input.isButtonPressed(platform::Button::R))
	{
		respawnPlayer();
	}

	renderer.clearScreen();


	const bool moveLeft = input.isButtonHeld(platform::Button::A) || input.isButtonHeld(platform::Button::Left);
	const bool moveRight = input.isButtonHeld(platform::Button::D) || input.isButtonHeld(platform::Button::Right);
	const bool jumpPressed =
		input.isButtonPressed(platform::Button::Space) ||
		input.isButtonPressed(platform::Button::W) ||
		input.isButtonPressed(platform::Button::Up);
	const bool jumpReleased =
		input.isButtonReleased(platform::Button::Space) ||
		input.isButtonReleased(platform::Button::W) ||
		input.isButtonReleased(platform::Button::Up);

	float moveInput = 0.f;
	if (moveLeft) { moveInput -= 1.f; }
	if (moveRight) { moveInput += 1.f; }

	//renderer.renderRectangle({0.f, 0.f, float(w), float(h)},
	//	{0.05f, 0.07f, 0.10f, 1.f});

	updatePlayer(deltaTime, moveInput, jumpPressed, jumpReleased);
	updateCamera(w, h, deltaTime);
	renderer.setCamera(gCamera);


	drawRoom();
	drawPlayer();

#if GL2D_USE_SDL_GPU
	if (!renderer.gpuDevice)
#endif
	{
		renderer.flush();
	}

	return true;
}

//This function might not be be called if the program is forced closed
void closeGame()
{
}
