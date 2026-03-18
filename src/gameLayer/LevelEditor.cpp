#include "LevelEditor.h"

#include "imguiTools.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <glm/glm.hpp>
#include <string>

namespace
{
#if REMOVE_IMGUI == 0
	bool imguiBlocksEditorMouse()
	{
		if (!ImGui::isImguiWindowOpen())
		{
			return false;
		}

		return ImGui::IsAnyItemActive() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
	}
#endif
}

void LevelEditor::init()
{
	cleanup();

	*this = {};
	camera.zoom = tuning.cameraZoom;

	std::string fontPath = std::string(RESOURCES_PATH) + "arial.ttf";
	measureFont.createFromFile(fontPath.c_str());
}

void LevelEditor::cleanup()
{
	measureFont.cleanup();
}

void LevelEditor::enter(Room &room, gl2d::Renderer2D &renderer)
{
	syncPendingRoomSize(room);

	if (!cameraInitialized)
	{
		focusRoom(room, renderer);
	}
	else
	{
		camera.zoom = tuning.cameraZoom;
		clampCamera(room, renderer);
	}
}

void LevelEditor::update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer, Room &room)
{
	deltaTime = std::min(deltaTime, 0.05f);

	syncPendingRoomSize(room);

	if (!cameraInitialized)
	{
		focusRoom(room, renderer);
	}

	updateShortcuts(input);
	updateCamera(deltaTime, input, renderer, room);
	updateHoveredTile(input, renderer, room);
	updateTools(input, room);

	renderer.setCamera(camera);

	drawRoom(room, renderer);
	drawGrid(room, renderer);
	drawRectPreview(renderer);
	drawHoveredTile(renderer);
	drawMeasureText(renderer);
	drawWindow(room, renderer);
}

glm::vec2 LevelEditor::getViewSize(gl2d::Renderer2D &renderer)
{
	glm::vec2 topLeft = gl2d::internal::convertPoint(
		camera,
		{0.f, 0.f},
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));

	glm::vec2 bottomRight = gl2d::internal::convertPoint(
		camera,
		{static_cast<float>(renderer.windowW), static_cast<float>(renderer.windowH)},
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));

	return bottomRight - topLeft;
}

glm::vec2 LevelEditor::getViewCenter(gl2d::Renderer2D &renderer)
{
	return gl2d::internal::convertPoint(
		camera,
		{static_cast<float>(renderer.windowW) * 0.5f, static_cast<float>(renderer.windowH) * 0.5f},
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));
}

void LevelEditor::setViewCenter(glm::vec2 center, gl2d::Renderer2D &renderer)
{
	camera.follow(
		center,
		1.f,
		0.f,
		0.f,
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));

	cameraInitialized = true;
}

glm::vec2 LevelEditor::screenToWorld(glm::vec2 screenPos, gl2d::Renderer2D &renderer)
{
	return gl2d::internal::convertPoint(
		camera,
		screenPos,
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));
}

void LevelEditor::syncPendingRoomSize(Room &room)
{
	if (pendingRoomSize.x <= 0 || pendingRoomSize.y <= 0)
	{
		pendingRoomSize = room.size;
	}
}

void LevelEditor::focusRoom(Room &room, gl2d::Renderer2D &renderer)
{
	camera.zoom = tuning.cameraZoom;
	glm::vec2 roomCenter = glm::vec2(room.size) * 0.5f;
	setViewCenter(roomCenter, renderer);
	clampCamera(room, renderer);
}

void LevelEditor::updateHoveredTile(platform::Input &input, gl2d::Renderer2D &renderer, Room &room)
{
	mouseScreenPosition = {static_cast<float>(input.mouseX), static_cast<float>(input.mouseY)};

	glm::vec2 mouseWorld = screenToWorld({static_cast<float>(input.mouseX), static_cast<float>(input.mouseY)}, renderer);

	hoveredTile = {
		static_cast<int>(std::floor(mouseWorld.x)),
		static_cast<int>(std::floor(mouseWorld.y))
	};

	hoveredTileValid = room.getBlockSafe(hoveredTile.x, hoveredTile.y) != nullptr;
}

void LevelEditor::updateCamera(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer, Room &room)
{
	float moveSpeed = tuning.cameraMoveSpeed;
	if (input.isButtonHeld(platform::Button::LeftShift))
	{
		moveSpeed *= 2.5f;
	}

	glm::vec2 center = getViewCenter(renderer);

	if (input.isButtonHeld(platform::Button::A) || input.isButtonHeld(platform::Button::Left)) { center.x -= moveSpeed * deltaTime; }
	if (input.isButtonHeld(platform::Button::D) || input.isButtonHeld(platform::Button::Right)) { center.x += moveSpeed * deltaTime; }
	if (input.isButtonHeld(platform::Button::W) || input.isButtonHeld(platform::Button::Up)) { center.y -= moveSpeed * deltaTime; }
	if (input.isButtonHeld(platform::Button::S) || input.isButtonHeld(platform::Button::Down)) { center.y += moveSpeed * deltaTime; }

	if (input.isButtonHeld(platform::Button::Q))
	{
		tuning.cameraZoom -= 40.f * deltaTime;
	}

	if (input.isButtonHeld(platform::Button::E))
	{
		tuning.cameraZoom += 40.f * deltaTime;
	}

	tuning.cameraZoom = std::clamp(tuning.cameraZoom, 4.f, 128.f);
	camera.zoom = tuning.cameraZoom;

	setViewCenter(center, renderer);
	clampCamera(room, renderer);
}

void LevelEditor::updateShortcuts(platform::Input &input)
{
	if (input.isButtonPressed(platform::Button::Escape))
	{
		rectDragActive = false;
	}

#if REMOVE_IMGUI == 0
	if (!ImGui::isImguiWindowOpen())
	{
		if (input.isButtonPressed(platform::Button::NR1)) { tool = noneTool; rectDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR2)) { tool = brushTool; rectDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR3)) { tool = rectTool; rectDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR4)) { tool = measureTool; rectDragActive = false; }
		if (input.isButtonPressed(platform::Button::G)) { tuning.showGrid = !tuning.showGrid; }
		return;
	}

	ImGuiIO &io = ImGui::GetIO();
	if (!io.WantCaptureKeyboard)
	{
		if (input.isButtonPressed(platform::Button::NR1)) { tool = noneTool; rectDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR2)) { tool = brushTool; rectDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR3)) { tool = rectTool; rectDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR4)) { tool = measureTool; rectDragActive = false; }
		if (input.isButtonPressed(platform::Button::G)) { tuning.showGrid = !tuning.showGrid; }
	}
#else
	if (input.isButtonPressed(platform::Button::NR1)) { tool = noneTool; rectDragActive = false; }
	if (input.isButtonPressed(platform::Button::NR2)) { tool = brushTool; rectDragActive = false; }
	if (input.isButtonPressed(platform::Button::NR3)) { tool = rectTool; rectDragActive = false; }
	if (input.isButtonPressed(platform::Button::NR4)) { tool = measureTool; rectDragActive = false; }
	if (input.isButtonPressed(platform::Button::G)) { tuning.showGrid = !tuning.showGrid; }
#endif
}

void LevelEditor::updateTools(platform::Input &input, Room &room)
{
#if REMOVE_IMGUI == 0
	if (imguiBlocksEditorMouse())
	{
		return;
	}
#endif

	if (tool == brushTool)
	{
		rectDragActive = false;

		if (input.isLMouseHeld() && hoveredTileValid)
		{
			setBlock(room, hoveredTile.x, hoveredTile.y, true);
		}

		if (input.isRMouseHeld() && hoveredTileValid)
		{
			setBlock(room, hoveredTile.x, hoveredTile.y, false);
		}
	}
	else if (tool == noneTool)
	{
		rectDragActive = false;
	}
	else if (tool == measureTool)
	{
		if ((input.isLMousePressed() || input.isRMousePressed()) && hoveredTileValid)
		{
			rectDragActive = true;
			rectDragStart = hoveredTile;
			rectDragEnd = hoveredTile;
		}

		if (rectDragActive && hoveredTileValid)
		{
			rectDragEnd = hoveredTile;
		}

		if (rectDragActive && (input.isLMouseReleased() || input.isRMouseReleased()))
		{
			rectDragActive = false;
		}
	}
	else if (tool == rectTool)
	{
		if ((input.isLMousePressed() || input.isRMousePressed()) && hoveredTileValid)
		{
			rectDragActive = true;
			rectDragPlacesSolid = input.isLMousePressed();
			rectDragStart = hoveredTile;
			rectDragEnd = hoveredTile;
		}

		if (rectDragActive && hoveredTileValid)
		{
			rectDragEnd = hoveredTile;
		}

		bool released = false;
		if (rectDragActive && rectDragPlacesSolid && input.isLMouseReleased()) { released = true; }
		if (rectDragActive && !rectDragPlacesSolid && input.isRMouseReleased()) { released = true; }

		if (released)
		{
			fillRect(room, rectDragStart, rectDragEnd, rectDragPlacesSolid);
			rectDragActive = false;
		}
	}
}

void LevelEditor::setBlock(Room &room, int x, int y, bool solid)
{
	if (Block *block = room.getBlockSafe(x, y))
	{
		block->solid = solid;
	}
}

void LevelEditor::fillRect(Room &room, glm::ivec2 a, glm::ivec2 b, bool solid)
{
	int minX = std::min(a.x, b.x);
	int minY = std::min(a.y, b.y);
	int maxX = std::max(a.x, b.x);
	int maxY = std::max(a.y, b.y);

	for (int y = minY; y <= maxY; y++)
	{
		for (int x = minX; x <= maxX; x++)
		{
			setBlock(room, x, y, solid);
		}
	}
}

void LevelEditor::resizeRoom(Room &room, int newSizeX, int newSizeY)
{
	newSizeX = std::max(newSizeX, 1);
	newSizeY = std::max(newSizeY, 1);

	Room resizedRoom = {};
	resizedRoom.create(newSizeX, newSizeY);

	int copySizeX = std::min(room.size.x, resizedRoom.size.x);
	int copySizeY = std::min(room.size.y, resizedRoom.size.y);

	for (int y = 0; y < copySizeY; y++)
	{
		for (int x = 0; x < copySizeX; x++)
		{
			resizedRoom.getBlockUnsafe(x, y) = room.getBlockUnsafe(x, y);
		}
	}

	room = resizedRoom;
	pendingRoomSize = room.size;
	hoveredTileValid = false;
	rectDragActive = false;
}

glm::vec4 LevelEditor::getRectPreview(glm::ivec2 a, glm::ivec2 b)
{
	int minX = std::min(a.x, b.x);
	int minY = std::min(a.y, b.y);
	int maxX = std::max(a.x, b.x);
	int maxY = std::max(a.y, b.y);

	return {
		static_cast<float>(minX),
		static_cast<float>(minY),
		static_cast<float>(maxX - minX + 1),
		static_cast<float>(maxY - minY + 1)
	};
}

void LevelEditor::clampCamera(Room &room, gl2d::Renderer2D &renderer)
{
	glm::vec2 center = getViewCenter(renderer);
	glm::vec2 viewSize = getViewSize(renderer);

	if (room.size.x <= viewSize.x)
	{
		center.x = room.size.x * 0.5f;
	}
	else
	{
		center.x = std::clamp(center.x, viewSize.x * 0.5f, room.size.x - viewSize.x * 0.5f);
	}

	if (room.size.y <= viewSize.y)
	{
		center.y = room.size.y * 0.5f;
	}
	else
	{
		center.y = std::clamp(center.y, viewSize.y * 0.5f, room.size.y - viewSize.y * 0.5f);
	}

	setViewCenter(center, renderer);
}

void LevelEditor::drawRoom(Room &room, gl2d::Renderer2D &renderer)
{
	const gl2d::Color4f roomBackground = {0.08f, 0.10f, 0.13f, 1.f};
	const gl2d::Color4f blockColor = {0.32f, 0.40f, 0.50f, 1.f};

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

void LevelEditor::drawGrid(Room &room, gl2d::Renderer2D &renderer)
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

void LevelEditor::drawHoveredTile(gl2d::Renderer2D &renderer)
{
	if (!hoveredTileValid)
	{
		return;
	}

	gl2d::Color4f hoverColor = {0.12f, 0.95f, 0.38f, 0.9f};
	if (tool == noneTool)
	{
		hoverColor = {0.92f, 0.92f, 0.92f, 0.85f};
	}
	if (tool == rectTool)
	{
		hoverColor = {0.20f, 0.75f, 1.0f, 0.9f};
	}
	if (tool == measureTool)
	{
		hoverColor = {0.28f, 0.68f, 1.0f, 0.9f};
	}

	renderer.renderRectangleOutline(
		{static_cast<float>(hoveredTile.x), static_cast<float>(hoveredTile.y), 1.f, 1.f},
		hoverColor,
		0.08f);
}

void LevelEditor::drawRectPreview(gl2d::Renderer2D &renderer)
{
	if (!rectDragActive)
	{
		return;
	}

	glm::vec4 rect = getRectPreview(rectDragStart, rectDragEnd);
	gl2d::Color4f previewColor = rectDragPlacesSolid
		? gl2d::Color4f{0.15f, 1.0f, 0.35f, 0.9f}
		: gl2d::Color4f{1.0f, 0.28f, 0.22f, 0.9f};

	if (tool == measureTool)
	{
		previewColor = {0.28f, 0.68f, 1.0f, 0.95f};
	}

	renderer.renderRectangleOutline(rect, previewColor, 0.10f);
}

void LevelEditor::drawMeasureText(gl2d::Renderer2D &renderer)
{
	if ((tool != measureTool && tool != rectTool) || !rectDragActive || !measureFont.texture.isValid())
	{
		return;
	}

	glm::ivec2 size = {
		std::abs(rectDragEnd.x - rectDragStart.x) + 1,
		std::abs(rectDragEnd.y - rectDragStart.y) + 1
	};

	char text[64] = {};
	std::snprintf(text, sizeof(text), "%d x %d", size.x, size.y);

	gl2d::Color4f textColor = {0.30f, 0.76f, 1.0f, 1.f};
	if (tool == rectTool)
	{
		textColor = rectDragPlacesSolid
			? gl2d::Color4f{0.18f, 1.0f, 0.40f, 1.f}
			: gl2d::Color4f{1.0f, 0.38f, 0.30f, 1.f};
	}

	renderer.pushCamera();
	renderer.renderText(
		mouseScreenPosition + glm::vec2(18.f, 18.f),
		text,
		measureFont,
		textColor,
		36.f,
		4.f,
		3.f,
		false,
		{0.03f, 0.06f, 0.10f, 0.95f});
	renderer.popCamera();
}

void LevelEditor::drawWindow(Room &room, gl2d::Renderer2D &renderer)
{
#if REMOVE_IMGUI == 0
	if (!ImGui::isImguiWindowOpen())
	{
		return;
	}

	ImGui::SetNextWindowBgAlpha(0.90f);
	ImGui::SetNextWindowSize({380.f, 0.f}, ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Level Editor"))
	{
		ImGui::TextUnformatted("F10 hides / shows ImGui");
		ImGui::TextUnformatted("F6 switches between gameplay and editor");
		ImGui::TextUnformatted("WASD / Arrows move camera, Q/E zoom");
		ImGui::TextUnformatted("Escape cancels an active rect or measure selection");

		ImGui::Separator();
		ImGui::TextUnformatted("Tools");
		if (ImGui::RadioButton("None (1)", tool == noneTool)) { tool = noneTool; rectDragActive = false; }
		if (ImGui::RadioButton("Brush (2)", tool == brushTool)) { tool = brushTool; rectDragActive = false; }
		if (ImGui::RadioButton("Rect (3)", tool == rectTool)) { tool = rectTool; rectDragActive = false; }
		if (ImGui::RadioButton("Measure (4)", tool == measureTool)) { tool = measureTool; rectDragActive = false; }

		if (tool == noneTool)
		{
			ImGui::TextUnformatted("No editing input");
		}
		else if (tool == brushTool)
		{
			ImGui::TextUnformatted("LMB place, RMB erase");
		}
		else if (tool == measureTool)
		{
			ImGui::TextUnformatted("Drag to measure without editing");
		}
		else
		{
			ImGui::TextUnformatted("Drag LMB to fill, drag RMB to clear");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Room");
		ImGui::Text("Current Size: %d x %d", room.size.x, room.size.y);
		ImGui::InputInt("Width", &pendingRoomSize.x);
		ImGui::InputInt("Height", &pendingRoomSize.y);
		if (ImGui::Button("Apply Resize"))
		{
			resizeRoom(room, pendingRoomSize.x, pendingRoomSize.y);
			clampCamera(room, renderer);
		}
		ImGui::SameLine();
		if (ImGui::Button("Focus Room"))
		{
			focusRoom(room, renderer);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("View");
		ImGui::SliderFloat("Zoom", &tuning.cameraZoom, 4.f, 128.f, "%.1f");
		ImGui::SliderFloat("Camera Speed", &tuning.cameraMoveSpeed, 4.f, 80.f, "%.1f");
		ImGui::Checkbox("Show Grid", &tuning.showGrid);
		if (tuning.showGrid)
		{
			ImGui::SliderFloat("Grid Alpha", &tuning.gridAlpha, 0.02f, 1.f, "%.2f");
			ImGui::SliderFloat("Grid Width", &tuning.gridLineWidth, 0.005f, 0.15f, "%.3f");
		}

		ImGui::Separator();
		if (hoveredTileValid)
		{
			ImGui::Text("Hover Tile: %d, %d", hoveredTile.x, hoveredTile.y);
			ImGui::Text("Solid: %d", room.getBlockUnsafe(hoveredTile.x, hoveredTile.y).solid ? 1 : 0);
		}
		else
		{
			ImGui::TextUnformatted("Hover Tile: outside room");
		}

		ImGui::Text("Camera: %.2f, %.2f", camera.position.x, camera.position.y);
	}
	ImGui::End();
#endif
}
