#include "WorldEditor.h"

#include "RoomIo.h"
#include "imguiTools.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

namespace
{
	constexpr float kMinWorldZoom = 0.1f;
	constexpr float kMaxWorldZoom = 28.f;

	std::vector<std::string> getSortedPlacedLevelNames(WorldData const &world)
	{
		std::vector<std::string> names;
		names.reserve(world.levels.size());
		for (auto const &it : world.levels)
		{
			names.push_back(it.first);
		}
		std::sort(names.begin(), names.end());
		return names;
	}

#if REMOVE_IMGUI == 0
	bool imguiBlocksWorldEditorMouse(bool gameViewHovered)
	{
		if (gameViewHovered)
		{
			return false;
		}

		if (!ImGui::isImguiWindowOpen())
		{
			return false;
		}

		return ImGui::IsAnyItemActive() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
	}

	bool worldEditorModalPopupOpen()
	{
		return ImGui::IsPopupOpen("Discard World Changes");
	}
#endif
}

void WorldEditor::init()
{
	cleanup();

	*this = {};
	camera.zoom = tuning.cameraZoom;

	std::string fontPath = std::string(RESOURCES_PATH) + "arial.ttf";
	labelFont.createFromFile(fontPath.c_str());
	loadWorld();
}

void WorldEditor::cleanup()
{
	labelFont.cleanup();
}

void WorldEditor::enter(gl2d::Renderer2D &renderer)
{
	camera.zoom = tuning.cameraZoom;
	if (!cameraInitialized)
	{
		focusWorld(renderer);
	}
	else
	{
		clampCamera(renderer);
	}
}

void WorldEditor::update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer,
	bool gameViewHovered, bool gameViewFocused)
{
	deltaTime = std::min(deltaTime, 0.05f);

#if REMOVE_IMGUI == 0
	if (worldEditorModalPopupOpen())
	{
		input = {};
	}
#endif

	if (!cameraInitialized)
	{
		focusWorld(renderer);
	}

	updateShortcuts(input, gameViewFocused);
	updateCamera(deltaTime, input, renderer);
	updateDragging(input, renderer, gameViewHovered);

	renderer.setCamera(camera);
	drawWorld(renderer);
	drawGrid(renderer);
	drawLevels(renderer);
	drawLevelLabels(renderer);

	drawWindow(renderer);
	drawLevelFilesWindow(renderer);
	drawDiscardWindow(renderer);
}

glm::vec2 WorldEditor::getViewSize(gl2d::Renderer2D &renderer)
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

glm::vec2 WorldEditor::getViewCenter(gl2d::Renderer2D &renderer)
{
	return gl2d::internal::convertPoint(
		camera,
		{static_cast<float>(renderer.windowW) * 0.5f, static_cast<float>(renderer.windowH) * 0.5f},
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));
}

void WorldEditor::setViewCenter(glm::vec2 center, gl2d::Renderer2D &renderer)
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

glm::vec2 WorldEditor::screenToWorld(glm::vec2 screenPos, gl2d::Renderer2D &renderer)
{
	return gl2d::internal::convertPoint(
		camera,
		screenPos,
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));
}

glm::vec2 WorldEditor::worldToScreen(glm::vec2 worldPos, gl2d::Renderer2D &renderer)
{
	glm::vec2 screenCenter = {
		static_cast<float>(renderer.windowW) * 0.5f,
		static_cast<float>(renderer.windowH) * 0.5f
	};

	glm::vec2 cameraCenter = camera.position + screenCenter;
	return screenCenter + (worldPos - cameraCenter) * camera.zoom;
}

void WorldEditor::focusWorld(gl2d::Renderer2D &renderer)
{
	tuning.cameraZoom = std::clamp(tuning.cameraZoom, kMinWorldZoom, kMaxWorldZoom);
	camera.zoom = tuning.cameraZoom;
	setViewCenter({
		world.bounds.x + world.bounds.z * 0.5f,
		world.bounds.y + world.bounds.w * 0.5f
	}, renderer);
	clampCamera(renderer);
}

void WorldEditor::focusPlacedLevel(std::string const &levelName, gl2d::Renderer2D &renderer)
{
	auto found = world.levels.find(levelName);
	if (found == world.levels.end())
	{
		worldHasError = true;
		worldMessage = "That level is not placed in the world";
		return;
	}

	selectedPlacedLevelName = levelName;
	selectedLevelName = levelName;

	glm::vec4 rect = found->second.getRect();
	setViewCenter({rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f}, renderer);
	clampCamera(renderer);
}

void WorldEditor::ensureBoundsForPlacement(WorldLevelPlacement const &placement)
{
	constexpr float expandStep = 500.f;
	constexpr float expandMargin = 100.f;

	glm::vec4 rect = placement.getRect();
	float rectRight = rect.x + rect.z;
	float rectBottom = rect.y + rect.w;
	float boundsRight = world.bounds.x + world.bounds.z;
	float boundsBottom = world.bounds.y + world.bounds.w;

	if (rect.x < world.bounds.x + expandMargin)
	{
		float expand = std::max(expandStep, world.bounds.x + expandMargin - rect.x);
		world.bounds.x -= expand;
		world.bounds.z += expand;
	}

	if (rect.y < world.bounds.y + expandMargin)
	{
		float expand = std::max(expandStep, world.bounds.y + expandMargin - rect.y);
		world.bounds.y -= expand;
		world.bounds.w += expand;
	}

	if (rectRight > boundsRight - expandMargin)
	{
		float expand = std::max(expandStep, rectRight - (boundsRight - expandMargin));
		world.bounds.z += expand;
	}

	if (rectBottom > boundsBottom - expandMargin)
	{
		float expand = std::max(expandStep, rectBottom - (boundsBottom - expandMargin));
		world.bounds.w += expand;
	}
}

void WorldEditor::clampCamera(gl2d::Renderer2D &renderer)
{
	constexpr float cameraPadding = 120.f;

	glm::vec2 center = getViewCenter(renderer);
	glm::vec2 viewSize = getViewSize(renderer);

	float minX = world.bounds.x + viewSize.x * 0.5f - cameraPadding;
	float maxX = world.bounds.x + world.bounds.z - viewSize.x * 0.5f + cameraPadding;
	if (minX > maxX)
	{
		center.x = world.bounds.x + world.bounds.z * 0.5f;
	}
	else
	{
		center.x = std::clamp(center.x, minX, maxX);
	}

	float minY = world.bounds.y + viewSize.y * 0.5f - cameraPadding;
	float maxY = world.bounds.y + world.bounds.w - viewSize.y * 0.5f + cameraPadding;
	if (minY > maxY)
	{
		center.y = world.bounds.y + world.bounds.w * 0.5f;
	}
	else
	{
		center.y = std::clamp(center.y, minY, maxY);
	}

	setViewCenter(center, renderer);
}

void WorldEditor::updateCamera(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer)
{
	float moveSpeed = tuning.cameraMoveSpeed;
	// Scale camera pan with zoom so wide overview navigation is faster and close work stays easier to control.
	float zoomMoveMultiplier = std::clamp(
		std::cbrt(1.f / std::max(tuning.cameraZoom, 0.001f)),
		0.35f,
		2.5f);
	moveSpeed *= zoomMoveMultiplier;
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
		tuning.cameraZoom -= 20.0f * deltaTime;
	}

	if (input.isButtonHeld(platform::Button::E))
	{
		tuning.cameraZoom += 20.0f * deltaTime;
	}

	tuning.cameraZoom = std::clamp(tuning.cameraZoom, kMinWorldZoom, kMaxWorldZoom);
	camera.zoom = tuning.cameraZoom;

	setViewCenter(center, renderer);
	clampCamera(renderer);
}

void WorldEditor::updateShortcuts(platform::Input &input, bool gameViewFocused)
{
	bool saveShortcut =
		input.isButtonHeld(platform::Button::LeftCtrl) &&
		input.isButtonPressed(platform::Button::S);
	if (saveShortcut)
	{
		saveWorld();
	}

#if REMOVE_IMGUI == 0
	if (!ImGui::isImguiWindowOpen())
	{
		if (input.isButtonPressed(platform::Button::NR1)) { tool = selectTool; dragActive = false; }
		if (input.isButtonPressed(platform::Button::NR2)) { tool = dragTool; dragActive = false; }
		return;
	}

	ImGuiIO &io = ImGui::GetIO();
	if (gameViewFocused || !io.WantCaptureKeyboard)
	{
		if (input.isButtonPressed(platform::Button::NR1)) { tool = selectTool; dragActive = false; }
		if (input.isButtonPressed(platform::Button::NR2)) { tool = dragTool; dragActive = false; }
	}
#else
	if (input.isButtonPressed(platform::Button::NR1)) { tool = selectTool; dragActive = false; }
	if (input.isButtonPressed(platform::Button::NR2)) { tool = dragTool; dragActive = false; }
#endif
}

void WorldEditor::updateDragging(platform::Input &input, gl2d::Renderer2D &renderer, bool gameViewHovered)
{
#if REMOVE_IMGUI == 0
	if (imguiBlocksWorldEditorMouse(gameViewHovered))
	{
		return;
	}
#endif

	glm::vec2 mouseWorld = screenToWorld({static_cast<float>(input.mouseX), static_cast<float>(input.mouseY)}, renderer);

	if (dragActive)
	{
		if (tool != dragTool || !input.isLMouseHeld())
		{
			dragActive = false;
			return;
		}

		auto found = world.levels.find(selectedPlacedLevelName);
		if (found != world.levels.end())
		{
			glm::vec2 newPosition = mouseWorld - dragGrabOffset;
			if (found->second.position != newPosition)
			{
				found->second.position = newPosition;
				ensureBoundsForPlacement(found->second);
				worldDirty = true;
			}
		}
		return;
	}

	if (!input.isLMousePressed())
	{
		return;
	}

	std::string hoveredLevel = getPlacedLevelAt(mouseWorld);
	if (hoveredLevel.empty())
	{
		selectedPlacedLevelName.clear();
		return;
	}

	selectedPlacedLevelName = hoveredLevel;
	selectedLevelName = hoveredLevel;

	if (tool == dragTool)
	{
		dragActive = true;
		dragGrabOffset = mouseWorld - world.levels[hoveredLevel].position;
	}
}

void WorldEditor::loadWorld()
{
	WorldIoResult result = loadWorldData(world);
	worldHasError = !result.success;
	worldMessage = result.message;
	worldDirty = false;
	selectedPlacedLevelName.clear();

	for (auto const &it : world.levels)
	{
		ensureBoundsForPlacement(it.second);
	}
}

void WorldEditor::saveWorld()
{
	WorldIoResult result = saveWorldData(world);
	worldHasError = !result.success;
	worldMessage = result.message;

	if (result.success)
	{
		worldDirty = false;
	}
}

void WorldEditor::discardWorldChanges()
{
	loadWorld();
}

void WorldEditor::spawnSelectedLevel(gl2d::Renderer2D &renderer)
{
	if (selectedLevelName.empty())
	{
		return;
	}

	if (world.levels.find(selectedLevelName) != world.levels.end())
	{
		worldHasError = true;
		worldMessage = "That level is already in the world";
		selectedPlacedLevelName = selectedLevelName;
		return;
	}

	Room room = {};
	RoomIoResult result = loadRoomFromFile(room, selectedLevelName.c_str());
	worldHasError = !result.success;
	worldMessage = result.message;

	if (!result.success)
	{
		return;
	}

	WorldLevelPlacement placement = {};
	placement.name = result.levelName;
	placement.size = room.size;
	placement.position = getViewCenter(renderer) - glm::vec2(room.size) * 0.5f;

	world.levels[placement.name] = placement;
	selectedLevelName = placement.name;
	selectedPlacedLevelName = placement.name;
	ensureBoundsForPlacement(placement);
	worldDirty = true;
	worldHasError = false;
	worldMessage = "Spawned level into world";
}

std::string WorldEditor::getPlacedLevelAt(glm::vec2 worldPoint)
{
	if (!selectedPlacedLevelName.empty())
	{
		auto found = world.levels.find(selectedPlacedLevelName);
		if (found != world.levels.end() && found->second.getRect().x <= worldPoint.x &&
			found->second.getRect().y <= worldPoint.y &&
			found->second.getRect().x + found->second.getRect().z >= worldPoint.x &&
			found->second.getRect().y + found->second.getRect().w >= worldPoint.y)
		{
			return selectedPlacedLevelName;
		}
	}

	for (auto const &name : getSortedPlacedLevelNames(world))
	{
		auto const &placement = world.levels.at(name);
		glm::vec4 rect = placement.getRect();
		if (rect.x <= worldPoint.x && rect.y <= worldPoint.y &&
			rect.x + rect.z >= worldPoint.x && rect.y + rect.w >= worldPoint.y)
		{
			return name;
		}
	}

	return {};
}

void WorldEditor::drawWorld(gl2d::Renderer2D &renderer)
{
	renderer.renderRectangle(world.bounds, {0.07f, 0.08f, 0.10f, 1.f});
	renderer.renderRectangleOutline(world.bounds, {0.14f, 0.18f, 0.24f, 1.f}, 2.0f);
}

void WorldEditor::drawGrid(gl2d::Renderer2D &renderer)
{
	if (!tuning.showGrid || tuning.gridStep <= 1.f)
	{
		return;
	}

	const gl2d::Color4f gridColor = {0.70f, 0.76f, 0.84f, tuning.gridAlpha};
	float startX = std::floor(world.bounds.x / tuning.gridStep) * tuning.gridStep;
	float endX = world.bounds.x + world.bounds.z;
	for (float x = startX; x <= endX; x += tuning.gridStep)
	{
		renderer.renderLine(
			{x, world.bounds.y},
			{x, world.bounds.y + world.bounds.w},
			gridColor,
			tuning.gridLineWidth);
	}

	float startY = std::floor(world.bounds.y / tuning.gridStep) * tuning.gridStep;
	float endY = world.bounds.y + world.bounds.w;
	for (float y = startY; y <= endY; y += tuning.gridStep)
	{
		renderer.renderLine(
			{world.bounds.x, y},
			{world.bounds.x + world.bounds.z, y},
			gridColor,
			tuning.gridLineWidth);
	}
}

void WorldEditor::drawLevels(gl2d::Renderer2D &renderer)
{
	for (auto const &name : getSortedPlacedLevelNames(world))
	{
		auto const &placement = world.levels.at(name);
		bool selected = name == selectedPlacedLevelName;

		gl2d::Color4f fillColor = selected
			? gl2d::Color4f{0.18f, 0.52f, 1.0f, 0.28f}
			: gl2d::Color4f{0.22f, 0.30f, 0.42f, 0.24f};
		gl2d::Color4f outlineColor = selected
			? gl2d::Color4f{0.25f, 0.70f, 1.0f, 1.f}
			: gl2d::Color4f{0.80f, 0.86f, 0.94f, 0.95f};

		renderer.renderRectangle(placement.getRect(), fillColor);
		renderer.renderRectangleOutline(placement.getRect(), outlineColor, 1.0f);
	}
}

void WorldEditor::drawLevelLabels(gl2d::Renderer2D &renderer)
{
	if (!labelFont.texture.isValid())
	{
		return;
	}

	renderer.pushCamera();

	for (auto const &name : getSortedPlacedLevelNames(world))
	{
		auto const &placement = world.levels.at(name);
		glm::vec2 screenPos = worldToScreen(placement.position, renderer) + glm::vec2(10.f, 12.f);
		if (screenPos.x < -250.f || screenPos.y < -80.f ||
			screenPos.x > renderer.windowW + 250.f || screenPos.y > renderer.windowH + 80.f)
		{
			continue;
		}

		char text[256] = {};
		std::snprintf(text, sizeof(text), "%s\n%d x %d", placement.name.c_str(), placement.size.x, placement.size.y);

		gl2d::Color4f color = name == selectedPlacedLevelName
			? gl2d::Color4f{0.98f, 0.96f, 0.50f, 1.f}
			: gl2d::Color4f{0.90f, 0.94f, 0.98f, 1.f};

		renderer.renderText(
			screenPos,
			text,
			labelFont,
			color,
			18.f,
			4.f,
			3.f,
			false,
			{0.03f, 0.06f, 0.10f, 0.95f});
	}

	renderer.popCamera();
}

void WorldEditor::drawWindow(gl2d::Renderer2D &renderer)
{
#if REMOVE_IMGUI == 0
	if (!ImGui::isImguiWindowOpen())
	{
		return;
	}

	ImGui::SetNextWindowBgAlpha(0.90f);
	ImGui::SetNextWindowSize({360.f, 0.f}, ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Editor"))
	{
		ImGui::TextUnformatted("F10 hides / shows ImGui");
		ImGui::TextUnformatted("F6 Game, F7 Level Editor, F8 World Editor");
		ImGui::TextUnformatted("WASD / Arrows move camera, Q/E zoom");
		ImGui::TextUnformatted("1 selects levels, 2 selects + drags levels");
		ImGui::TextUnformatted("Ctrl+S saves world.json");

		ImGui::Separator();
		ImGui::TextUnformatted("Tools");
		if (ImGui::RadioButton("Select (1)", tool == selectTool))
		{
			tool = selectTool;
			dragActive = false;
		}
		if (ImGui::RadioButton("Move (2)", tool == dragTool))
		{
			tool = dragTool;
			dragActive = false;
		}

		if (tool == selectTool)
		{
			ImGui::TextUnformatted("LMB selects a placed level");
		}
		else
		{
			ImGui::TextUnformatted("LMB selects and drags a placed level");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("World");
		ImGui::Text("Placed Levels: %d", static_cast<int>(world.levels.size()));
		ImGui::Text("Bounds: %.0f, %.0f, %.0f, %.0f",
			world.bounds.x, world.bounds.y, world.bounds.z, world.bounds.w);
		if (ImGui::Button("Focus World"))
		{
			focusWorld(renderer);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("View");
		ImGui::SliderFloat("Zoom", &tuning.cameraZoom, kMinWorldZoom, kMaxWorldZoom, "%.2f");
		ImGui::SliderFloat("Camera Speed", &tuning.cameraMoveSpeed, 20.f, 1200.f, "%.0f");
		ImGui::Checkbox("Show Grid", &tuning.showGrid);
		if (tuning.showGrid)
		{
			ImGui::SliderFloat("Grid Step", &tuning.gridStep, 20.f, 400.f, "%.0f");
			ImGui::SliderFloat("Grid Alpha", &tuning.gridAlpha, 0.02f, 1.f, "%.2f");
			ImGui::SliderFloat("Grid Width", &tuning.gridLineWidth, 0.05f, 4.f, "%.2f");
		}

		ImGui::Separator();
		if (!selectedPlacedLevelName.empty() && world.levels.find(selectedPlacedLevelName) != world.levels.end())
		{
			auto const &placement = world.levels.at(selectedPlacedLevelName);
			ImGui::Text("Selected: %s", placement.name.c_str());
			ImGui::Text("Pos: %.1f, %.1f", placement.position.x, placement.position.y);
			ImGui::Text("Size: %d x %d", placement.size.x, placement.size.y);
		}
		else
		{
			ImGui::TextUnformatted("Selected: none");
		}
	}
	ImGui::End();
#endif
}

void WorldEditor::drawLevelFilesWindow(gl2d::Renderer2D &renderer)
{
#if REMOVE_IMGUI == 0
	if (!ImGui::isImguiWindowOpen())
	{
		return;
	}

	RoomFilesListing levelFiles = listRoomFiles();
	bool selectedLevelStillExists = selectedLevelName.empty();
	for (auto const &file : levelFiles.files)
	{
		if (file.name == selectedLevelName)
		{
			selectedLevelStillExists = true;
			break;
		}
	}

	if (!selectedLevelStillExists)
	{
		selectedLevelName.clear();
	}

	if (selectedLevelName.empty() && !levelFiles.files.empty())
	{
		selectedLevelName = levelFiles.files.front().name;
	}

	ImGui::SetNextWindowBgAlpha(0.90f);
	ImGui::SetNextWindowSize({420.f, 0.f}, ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Level Files"))
	{
		ImGui::TextUnformatted("View");
		if (ImGui::RadioButton("Game", false))
		{
			requestGameplayMode = true;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Level Editor", false))
		{
			requestLevelEditorMode = true;
		}
		ImGui::SameLine();
		ImGui::RadioButton("World Editor", true);

		ImGui::Separator();
		if (worldDirty)
		{
			ImGui::TextColored({1.0f, 0.90f, 0.30f, 1.f}, "Loaded World: world.json*");
		}
		else
		{
			ImGui::TextColored({0.35f, 1.f, 0.55f, 1.f}, "Loaded World: world.json");
		}
		ImGui::Text("World File: %s", getWorldFilePath().c_str());
		ImGui::Text("World State: %s", worldDirty ? "Unsaved changes" : "Saved");
		ImGui::Text("Placed Levels: %d", static_cast<int>(world.levels.size()));

		ImGui::Separator();
		ImGui::Text("Existing Levels (%d)", static_cast<int>(levelFiles.files.size()));
		if (!levelFiles.error.empty())
		{
			ImGui::TextColored({1.f, 0.45f, 0.35f, 1.f}, "%s", levelFiles.error.c_str());
		}

		if (ImGui::BeginChild("WorldLevelFileList", {0.f, 220.f}, true))
		{
			for (auto const &file : levelFiles.files)
			{
				bool selected = file.name == selectedLevelName;
				bool placed = world.levels.find(file.name) != world.levels.end();
				std::string label = file.name;
				if (placed)
				{
					label += "  [in world]";
					ImGui::PushStyleColor(ImGuiCol_Text, {0.35f, 1.f, 0.55f, 1.f});
				}

				if (ImGui::Selectable(label.c_str(), selected))
				{
					selectedLevelName = file.name;
				}

				if (placed)
				{
					ImGui::PopStyleColor();
				}
			}
		}
		ImGui::EndChild();

		bool selectedIsPlaced = !selectedLevelName.empty() && world.levels.find(selectedLevelName) != world.levels.end();
		bool canSpawnSelected = !selectedLevelName.empty() && !selectedIsPlaced;
		bool canFocusSelected = selectedIsPlaced;

		if (!canSpawnSelected) { ImGui::BeginDisabled(); }
		if (ImGui::Button("Spawn Selected In World"))
		{
			spawnSelectedLevel(renderer);
		}
		if (!canSpawnSelected) { ImGui::EndDisabled(); }

		if (!canFocusSelected) { ImGui::BeginDisabled(); }
		ImGui::SameLine();
		if (ImGui::Button("Focus Selected In World"))
		{
			focusPlacedLevel(selectedLevelName, renderer);
		}
		if (!canFocusSelected) { ImGui::EndDisabled(); }

		ImGui::Separator();
		if (ImGui::Button("Save World"))
		{
			saveWorld();
		}
		ImGui::SameLine();
		if (!worldDirty) { ImGui::BeginDisabled(); }
		if (ImGui::Button("Discard Changes"))
		{
			ImGui::OpenPopup("Discard World Changes");
		}
		if (!worldDirty) { ImGui::EndDisabled(); }

		if (!worldMessage.empty())
		{
			ImGui::Separator();
			ImVec4 color = worldHasError
				? ImVec4(1.f, 0.45f, 0.35f, 1.f)
				: ImVec4(0.35f, 1.f, 0.55f, 1.f);
			ImGui::TextColored(color, "%s", worldMessage.c_str());
		}
	}
	ImGui::End();
#endif
}

void WorldEditor::drawDiscardWindow(gl2d::Renderer2D &renderer)
{
	(void)renderer;
#if REMOVE_IMGUI == 0
	if (ImGui::BeginPopupModal("Discard World Changes", 0, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Reload world.json and discard your unsaved changes?");

		if (ImGui::Button("Discard", {120.f, 0.f}))
		{
			discardWorldChanges();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", {120.f, 0.f}))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
#endif
}
