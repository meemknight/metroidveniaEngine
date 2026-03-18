#include "Gameplay.h"
#include "gameLayer.h"

#include "imguiTools.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
namespace
{
	constexpr float kPlayerWidth = 3.f;
	constexpr float kPlayerHeight = 6.f;
}

void Gameplay::init()
{
	createStarterRoom();
}

void Gameplay::update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer)
{
	deltaTime = std::min(deltaTime, 0.05f);

	if (input.isButtonPressed(platform::Button::R))
	{
		respawnPlayer();
	}

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
	updateCamera(renderer.windowW, renderer.windowH);
	renderer.setCamera(camera);

	drawRoom(renderer);
	drawGrid(renderer);
	drawPlayer(renderer);
	drawDebugWindow();
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
}

void Gameplay::updatePlayer(float deltaTime, float moveInput, bool jumpPressed, bool jumpHeld)
{
	player.moveSpeed = tuning.moveSpeed;
	bool grounded = player.physics.downTouch;

	player.physics.transform.pos.x += moveInput * player.moveSpeed * deltaTime;

	if (grounded)
	{
		player.coyoteTimer = tuning.coyoteTime;
	}
	else
	{
		player.coyoteTimer -= deltaTime;
		player.coyoteTimer = std::max(player.coyoteTimer, 0.f);
	}

	if (jumpPressed)
	{
		player.jumpBufferTimer = tuning.jumpBufferTime;
	}
	else
	{
		player.jumpBufferTimer -= deltaTime;
		player.jumpBufferTimer = std::max(player.jumpBufferTimer, 0.f);
	}

	float jumpSpeed = std::sqrt(2.f * tuning.gravity * tuning.jumpHeight);

	if (player.jumpBufferTimer > 0.f && player.coyoteTimer > 0.f)
	{
		player.physics.velocity.y = -jumpSpeed;
		player.physics.downTouch = false;
		player.coyoteTimer = 0.f;
		player.jumpBufferTimer = 0.f;
	}

	if (!jumpHeld && player.physics.velocity.y < 0.f)
	{
		player.physics.velocity.y *= tuning.jumpCutMultiplier;
	}

	player.physics.velocity.y += tuning.gravity * deltaTime;
	player.physics.velocity.y = std::min(player.physics.velocity.y, tuning.maxFallSpeed);

	player.physics.velocity.x = 0.f;
	player.physics.updateForces(deltaTime, {0.f, 0.f});
	player.physics.resolveConstrains(room);
	player.physics.updateFinal();
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
	const gl2d::Color4f playerColor = player.physics.downTouch
		? gl2d::Color4f{1.0f, 0.63f, 0.19f, 1.f}
		: gl2d::Color4f{1.0f, 0.84f, 0.30f, 1.f};

	renderer.renderRectangle(player.physics.transform.getAABB(), playerColor);
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

	if (ImGui::Begin("Movement / Camera"))
	{
		ImGui::TextUnformatted("F10 hides / shows ImGui");

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
		ImGui::SliderFloat("Move Speed", &tuning.moveSpeed, 0.5f, 30.f, "%.2f");
		ImGui::SliderFloat("Jump Height", &tuning.jumpHeight, 0.5f, 20.f, "%.2f");
		ImGui::SliderFloat("Gravity", &tuning.gravity, 1.f, 120.f, "%.2f");
		ImGui::SliderFloat("Max Fall Speed", &tuning.maxFallSpeed, 1.f, 80.f, "%.2f");
		ImGui::SliderFloat("Jump Cut", &tuning.jumpCutMultiplier, 0.05f, 1.f, "%.2f");
		ImGui::SliderFloat("Coyote Time", &tuning.coyoteTime, 0.f, 0.30f, "%.2f");
		ImGui::SliderFloat("Jump Buffer", &tuning.jumpBufferTime, 0.f, 0.30f, "%.2f");
		float jumpSpeed = std::sqrt(2.f * tuning.gravity * tuning.jumpHeight);
		float jumpApexTime = jumpSpeed / std::max(tuning.gravity, 0.001f);
		ImGui::Text("Derived Jump Speed: %.2f", jumpSpeed);
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
		ImGui::Text("Touch: D%d U%d L%d R%d",
			player.physics.downTouch ? 1 : 0,
			player.physics.upTouch ? 1 : 0,
			player.physics.leftTouch ? 1 : 0,
			player.physics.rightTouch ? 1 : 0);
	}
	ImGui::End();
#endif
}
