#include "gameLayer.h"
#include "imguiTools.h"
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
	constexpr float kPlayerWidth = 3.f;
	constexpr float kPlayerHeight = 6.f;

	struct DebugTuning
	{
		float moveSpeed = 25.f;
		float jumpHeight = 11.f;
		float minJumpHeight = 3.f;
		float jumpApexTime = 0.42f;
		float fallGravity = 140.f;
		float maxFallSpeed = 38.f;
		float coyoteTime = 0.08f;
		float jumpBufferTime = 0.10f;
		float cameraZoom = 32.f;
		bool showGrid = true;
		float gridAlpha = 0.20f;
		float gridLineWidth = 0.05f;
	};

	Room gRoom;
	Player gPlayer;
	gl2d::Camera gCamera;
	glm::vec2 gSpawnPoint = {};
	DebugTuning gTuning;

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
		gPlayer = {};
		gPlayer.physics.transform.w = kPlayerWidth;
		gPlayer.physics.transform.h = kPlayerHeight;
		gPlayer.physics.teleport(gSpawnPoint);
		gPlayer.moveSpeed = gTuning.moveSpeed;

		gCamera = {};
		gCamera.zoom = gTuning.cameraZoom;
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

		gSpawnPoint = {14.f + kPlayerWidth * 0.5f, 32.f - kPlayerHeight * 0.5f};

		respawnPlayer();
	}

	void updatePlayer(float deltaTime, float moveInput, bool jumpPressed, bool jumpHeld)
	{
		gPlayer.moveSpeed = gTuning.moveSpeed;
		bool grounded = gPlayer.physics.downTouch;

		if (grounded)
		{
			gPlayer.physics.transform.pos.x += moveInput * gPlayer.moveSpeed * deltaTime;
			gPlayer.coyoteTimer = gTuning.coyoteTime;
		}
		else
		{
			gPlayer.physics.transform.pos.x += moveInput * gPlayer.moveSpeed * deltaTime;
			gPlayer.coyoteTimer -= deltaTime;
			gPlayer.coyoteTimer = std::max(gPlayer.coyoteTimer, 0.f);
		}

		if (jumpPressed)
		{
			gPlayer.jumpBufferTimer = gTuning.jumpBufferTime;
		}
		else
		{
			gPlayer.jumpBufferTimer -= deltaTime;
			gPlayer.jumpBufferTimer = std::max(gPlayer.jumpBufferTimer, 0.f);
		}

		float jumpApexTime = std::max(gTuning.jumpApexTime, 0.01f);
		float maxJumpHeight = std::max(gTuning.jumpHeight, 0.f);
		float minJumpHeight = std::clamp(gTuning.minJumpHeight, 0.f, maxJumpHeight);
		float riseGravity = (2.f * maxJumpHeight) / (jumpApexTime * jumpApexTime);
		float jumpSpeed = riseGravity * jumpApexTime;
		float minJumpSpeed = std::sqrt(2.f * riseGravity * minJumpHeight);

		if (gPlayer.jumpBufferTimer > 0.f && gPlayer.coyoteTimer > 0.f)
		{
			gPlayer.physics.velocity.y = -jumpSpeed;
			gPlayer.physics.downTouch = false;
			gPlayer.coyoteTimer = 0.f;
			gPlayer.jumpBufferTimer = 0.f;
		}

		if (!jumpHeld && gPlayer.physics.velocity.y < -minJumpSpeed)
		{
			gPlayer.physics.velocity.y = -minJumpSpeed;
		}

		float gravity = gPlayer.physics.velocity.y < 0.f ? riseGravity : gTuning.fallGravity;
		gPlayer.physics.velocity.y += gravity * deltaTime;
		gPlayer.physics.velocity.y = std::min(gPlayer.physics.velocity.y, gTuning.maxFallSpeed);

		gPlayer.physics.velocity.x = 0.f;
		gPlayer.physics.updateForces(deltaTime, {0.f, 0.f});
		gPlayer.physics.resolveConstrains(gRoom);
		gPlayer.physics.updateFinal();
	}

	//DON'T TOUCH THIS CODE!!!!!
	void updateCamera(int width, int height, float deltaTime)
	{
		gCamera.zoom = gTuning.cameraZoom;

		glm::vec2 roomSize = glm::vec2(gRoom.size);
		glm::vec2 viewSize = glm::vec2(width, height) / gCamera.zoom;
		glm::vec2 maxCamera = roomSize - viewSize;
		maxCamera.x = std::max(maxCamera.x, 0.f);
		maxCamera.y = std::max(maxCamera.y, 0.f);

		//DON'T TOUCH THIS CODE!!!!!
		gCamera.follow(gPlayer.getCenter(), 10,
			0.f, 0.f, width, height);
		//gCamera.position = glm::clamp(gCamera.position, glm::vec2(0.f), maxCamera);
	}

	void drawRoom()
	{
		const gl2d::Color4f roomBackground = {0.10f, 0.11f, 0.15f, 1.f};
		const gl2d::Color4f blockColor = {0.23f, 0.26f, 0.32f, 1.f};

		renderer.renderRectangle({0.f, 0.f, static_cast<float>(gRoom.size.x), static_cast<float>(gRoom.size.y)}, roomBackground);

		for (int y = 0; y < gRoom.size.y; y++)
		{
			for (int x = 0; x < gRoom.size.x; x++)
			{
				if (!gRoom.getBlockUnsafe(x, y).solid)
				{
					continue;
				}

				renderer.renderRectangle({static_cast<float>(x), static_cast<float>(y), 1.f, 1.f}, blockColor);
			}
		}
	}

	void drawGrid()
	{
		if (!gTuning.showGrid)
		{
			return;
		}

		const gl2d::Color4f gridColor = {0.80f, 0.84f, 0.90f, gTuning.gridAlpha};

		for (int x = 0; x <= gRoom.size.x; x++)
		{
			renderer.renderLine(
				{static_cast<float>(x), 0.f},
				{static_cast<float>(x), static_cast<float>(gRoom.size.y)},
				gridColor,
				gTuning.gridLineWidth);
		}

		for (int y = 0; y <= gRoom.size.y; y++)
		{
			renderer.renderLine(
				{0.f, static_cast<float>(y)},
				{static_cast<float>(gRoom.size.x), static_cast<float>(y)},
				gridColor,
				gTuning.gridLineWidth);
		}
	}

	void drawPlayer()
	{
		const gl2d::Color4f playerColor = gPlayer.physics.downTouch
			? gl2d::Color4f{1.0f, 0.63f, 0.19f, 1.f}
			: gl2d::Color4f{1.0f, 0.84f, 0.30f, 1.f};

		renderer.renderRectangle(gPlayer.physics.transform.getAABB(), playerColor);
	}

	void drawDebugWindow()
	{
#if REMOVE_IMGUI == 0
		ImGui::SetNextWindowBgAlpha(0.88f);
		ImGui::SetNextWindowSize({320.f, 0.f}, ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Movement / Camera"))
		{
			if (ImGui::Button("Respawn"))
			{
				respawnPlayer();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset Tunables"))
			{
				gTuning = {};
				respawnPlayer();
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Player");
			ImGui::SliderFloat("Move Speed", &gTuning.moveSpeed, 0.5f, 30.f, "%.2f");
			ImGui::SliderFloat("Jump Height", &gTuning.jumpHeight, 0.5f, 20.f, "%.2f");
			ImGui::SliderFloat("Min Jump Height", &gTuning.minJumpHeight, 0.f, std::max(gTuning.jumpHeight, 0.5f), "%.2f");
			ImGui::SliderFloat("Jump Apex Time", &gTuning.jumpApexTime, 0.05f, 1.20f, "%.2f");
			ImGui::SliderFloat("Fall Gravity", &gTuning.fallGravity, 1.f, 250.f, "%.2f");
			ImGui::SliderFloat("Max Fall Speed", &gTuning.maxFallSpeed, 1.f, 80.f, "%.2f");
			ImGui::SliderFloat("Coyote Time", &gTuning.coyoteTime, 0.f, 0.30f, "%.2f");
			ImGui::SliderFloat("Jump Buffer", &gTuning.jumpBufferTime, 0.f, 0.30f, "%.2f");
			float jumpApexTime = std::max(gTuning.jumpApexTime, 0.01f);
			float maxJumpHeight = std::max(gTuning.jumpHeight, 0.f);
			float minJumpHeight = std::clamp(gTuning.minJumpHeight, 0.f, maxJumpHeight);
			float riseGravity = (2.f * maxJumpHeight) / (jumpApexTime * jumpApexTime);
			float jumpSpeed = riseGravity * jumpApexTime;
			float minJumpSpeed = std::sqrt(2.f * riseGravity * minJumpHeight);
			ImGui::Text("Derived Jump Speed: %.2f", jumpSpeed);
			ImGui::Text("Derived Rise Gravity: %.2f", riseGravity);
			ImGui::Text("Derived Min Cut Speed: %.2f", minJumpSpeed);

			ImGui::Separator();
			ImGui::TextUnformatted("Camera");
			ImGui::SliderFloat("Zoom", &gTuning.cameraZoom, 4.f, 96.f, "%.1f");

			ImGui::Separator();
			ImGui::TextUnformatted("World");
			ImGui::Checkbox("Show Grid", &gTuning.showGrid);
			if (gTuning.showGrid)
			{
				ImGui::SliderFloat("Grid Alpha", &gTuning.gridAlpha, 0.02f, 1.f, "%.2f");
				ImGui::SliderFloat("Grid Width", &gTuning.gridLineWidth, 0.005f, 0.15f, "%.3f");
			}

			ImGui::Separator();
			ImGui::Text("Pos: %.2f, %.2f", gPlayer.physics.transform.pos.x, gPlayer.physics.transform.pos.y);
			ImGui::Text("Vel: %.2f, %.2f", gPlayer.physics.velocity.x, gPlayer.physics.velocity.y);
			ImGui::Text("Timers: coyote %.2f  buffer %.2f", gPlayer.coyoteTimer, gPlayer.jumpBufferTimer);
			ImGui::Text("Touch: D%d U%d L%d R%d",
				gPlayer.physics.downTouch ? 1 : 0,
				gPlayer.physics.upTouch ? 1 : 0,
				gPlayer.physics.leftTouch ? 1 : 0,
				gPlayer.physics.rightTouch ? 1 : 0);
		}
		ImGui::End();
#endif
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

	deltaTime = std::min(deltaTime, 0.05f);

	bool moveLeft = input.isButtonHeld(platform::Button::A) || input.isButtonHeld(platform::Button::Left);
	bool moveRight = input.isButtonHeld(platform::Button::D) || input.isButtonHeld(platform::Button::Right);
	bool jumpPressed =
		input.isButtonPressed(platform::Button::Space) ||
		input.isButtonPressed(platform::Button::W) ||
		input.isButtonPressed(platform::Button::Up);
	bool jumpHeld =
		input.isButtonHeld(platform::Button::Space) ||
		input.isButtonHeld(platform::Button::W) ||
		input.isButtonHeld(platform::Button::Up);

	float moveInput = 0.f;
	if (moveLeft) { moveInput -= 1.f; }
	if (moveRight) { moveInput += 1.f; }

	updatePlayer(deltaTime, moveInput, jumpPressed, jumpHeld);
	updateCamera(w, h, deltaTime);
	renderer.setCamera(gCamera);

	drawRoom();
	drawGrid();
	drawPlayer();
	drawDebugWindow();

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
