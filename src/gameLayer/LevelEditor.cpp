#include "LevelEditor.h"

#include "RoomIo.h"
#include "WorldIo.h"
#include "imguiTools.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <glm/glm.hpp>
#include <string>

namespace
{
	const glm::ivec2 kDefaultDoorSize = {2, 3};
	const gl2d::Color4f kDoorFillColor = {1.0f, 0.56f, 0.12f, 0.14f};
	const gl2d::Color4f kDoorOutlineColor = {1.0f, 0.64f, 0.18f, 0.72f};
	const gl2d::Color4f kSelectedDoorFillColor = {1.0f, 0.62f, 0.12f, 0.22f};
	const gl2d::Color4f kSelectedDoorOutlineColor = {1.0f, 0.78f, 0.28f, 1.0f};
	const gl2d::Color4f kDoorSpawnFillColor = {1.0f, 0.88f, 0.18f, 0.24f};
	const gl2d::Color4f kDoorSpawnOutlineColor = {1.0f, 0.92f, 0.32f, 0.96f};

	template<size_t N>
	void copyStringToBuffer(char (&buffer)[N], std::string const &text)
	{
		std::snprintf(buffer, N, "%s", text.c_str());
	}

	std::string trimWhitespace(std::string text)
	{
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
		{
			text.erase(text.begin());
		}

		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
		{
			text.pop_back();
		}

		return text;
	}

	bool roomHasDoorNamed(Room const &room, std::string const &doorName)
	{
		for (Door const &door : room.doors)
		{
			if (door.name == doorName)
			{
				return true;
			}
		}

		return false;
	}

	void queuePendingDoorRename(std::vector<LevelEditor::PendingDoorRename> &pendingDoorRenames,
		std::string const &oldName, std::string const &newName)
	{
		if (oldName.empty() || newName.empty() || oldName == newName)
		{
			return;
		}

		bool mergedRename = false;
		for (auto &rename : pendingDoorRenames)
		{
			if (rename.newName == oldName || rename.oldName == oldName)
			{
				rename.newName = newName;
				mergedRename = true;
				break;
			}
		}

		if (!mergedRename)
		{
			pendingDoorRenames.push_back({oldName, newName});
		}

		pendingDoorRenames.erase(
			std::remove_if(pendingDoorRenames.begin(), pendingDoorRenames.end(),
				[](LevelEditor::PendingDoorRename const &rename)
				{
					return rename.oldName.empty() ||
						rename.newName.empty() ||
						rename.oldName == rename.newName;
				}),
			pendingDoorRenames.end());
	}

#if REMOVE_IMGUI == 0
	bool imguiBlocksEditorMouse(bool gameViewHovered)
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

	bool editorModalPopupOpen()
	{
		return
			ImGui::IsPopupOpen("Unsaved Changes") ||
			ImGui::IsPopupOpen("Delete Level") ||
			ImGui::IsPopupOpen("Discard Current Changes") ||
			ImGui::IsPopupOpen("Resize Level");
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
	ensureRoomFilesFolder();
}

void LevelEditor::cleanup()
{
	measureFont.cleanup();
}

void LevelEditor::enter(Room &room, gl2d::Renderer2D &renderer)
{
	if (currentLevelName.empty())
	{
		camera.zoom = tuning.cameraZoom;
		if (!cameraInitialized)
		{
			setViewCenter({}, renderer);
		}
		return;
	}

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

void LevelEditor::update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer, Room &room,
	bool gameViewHovered, bool gameViewFocused)
{
	deltaTime = std::min(deltaTime, 0.05f);

	// File/load actions can replace editor state, so run them at the start of a frame
	// before new room draw data gets queued.
	if (pendingReloadCurrentLevel)
	{
		pendingReloadCurrentLevel = false;
		reloadCurrentLevel(room, renderer);
	}

	if (pendingApplyPendingFileAction)
	{
		pendingApplyPendingFileAction = false;

		if (pendingFileAction == loadSelectedFileAction)
		{
			loadSelectedLevel(room, renderer);
		}
		else if (pendingFileAction == createNewFileAction)
		{
			createNewLevel(room, renderer);
		}

		pendingFileAction = noPendingFileAction;
	}

#if REMOVE_IMGUI == 0
	// Modal popups should fully own input so the world view doesn't keep reacting underneath them.
	if (editorModalPopupOpen())
	{
		input = {};
	}
#endif

	if (!currentLevelName.empty())
	{
		syncPendingRoomSize(room);
		if (selectedDoorIndex >= static_cast<int>(room.doors.size()))
		{
			clearDoorSelection();
		}

		if (!cameraInitialized)
		{
			focusRoom(room, renderer);
		}
	}
	else if (!cameraInitialized)
	{
		camera.zoom = tuning.cameraZoom;
		setViewCenter({}, renderer);
		clearDoorSelection();
	}

	updateShortcuts(input, room, gameViewFocused);
	updateCamera(deltaTime, input, renderer, room);
	updateHoveredTile(input, renderer, room);
	updateTools(input, room, gameViewHovered);

	renderer.setCamera(camera);

	if (!currentLevelName.empty())
	{
		drawRoom(room, renderer);
		drawGrid(room, renderer);
		drawDoors(room, renderer);
		drawRectPreview(renderer);
		drawHoveredTile(renderer);
		drawMeasureText(renderer);
	}

	drawWindow(room, renderer);
	drawLevelFilesWindow(room, renderer);
	drawUnsavedChangesWindow(room, renderer);
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

glm::vec2 LevelEditor::worldToScreen(glm::vec2 worldPos, gl2d::Renderer2D &renderer)
{
	glm::vec2 screenCenter = {
		static_cast<float>(renderer.windowW) * 0.5f,
		static_cast<float>(renderer.windowH) * 0.5f
	};

	glm::vec2 cameraCenter = camera.position + screenCenter;
	return screenCenter + (worldPos - cameraCenter) * camera.zoom;
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
	mouseWorldPosition = screenToWorld(mouseScreenPosition, renderer);
	if (currentLevelName.empty())
	{
		hoveredTileValid = false;
		return;
	}

	hoveredTile = {
		static_cast<int>(std::floor(mouseWorldPosition.x)),
		static_cast<int>(std::floor(mouseWorldPosition.y))
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
	bool allowCameraMove = !input.isButtonHeld(platform::Button::LeftCtrl);

	if (allowCameraMove)
	{
		if (input.isButtonHeld(platform::Button::A) || input.isButtonHeld(platform::Button::Left)) { center.x -= moveSpeed * deltaTime; }
		if (input.isButtonHeld(platform::Button::D) || input.isButtonHeld(platform::Button::Right)) { center.x += moveSpeed * deltaTime; }
		if (input.isButtonHeld(platform::Button::W) || input.isButtonHeld(platform::Button::Up)) { center.y -= moveSpeed * deltaTime; }
		if (input.isButtonHeld(platform::Button::S) || input.isButtonHeld(platform::Button::Down)) { center.y += moveSpeed * deltaTime; }
	}

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
	if (!currentLevelName.empty())
	{
		clampCamera(room, renderer);
	}
}

void LevelEditor::updateShortcuts(platform::Input &input, Room &room, bool gameViewFocused)
{
	if (input.isButtonPressed(platform::Button::Escape))
	{
		rectDragActive = false;
		doorDragActive = false;
		doorResizeActive = false;
		doorSpawnDragActive = false;
	}

	bool saveShortcut = !currentLevelName.empty() &&
		input.isButtonHeld(platform::Button::LeftCtrl) &&
		input.isButtonPressed(platform::Button::S);
	if (saveShortcut)
	{
		saveCurrentLevel(room);
	}

#if REMOVE_IMGUI == 0
	if (!ImGui::isImguiWindowOpen())
	{
		if (input.isButtonPressed(platform::Button::NR1)) { tool = noneTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR2)) { tool = brushTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR3)) { tool = rectTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR4)) { tool = measureTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR5)) { tool = doorTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (input.isButtonPressed(platform::Button::G)) { tuning.showGrid = !tuning.showGrid; }
		return;
	}

	ImGuiIO &io = ImGui::GetIO();
	if (gameViewFocused || !io.WantCaptureKeyboard)
	{
		if (input.isButtonPressed(platform::Button::NR1)) { tool = noneTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR2)) { tool = brushTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR3)) { tool = rectTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR4)) { tool = measureTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (input.isButtonPressed(platform::Button::NR5)) { tool = doorTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (input.isButtonPressed(platform::Button::G)) { tuning.showGrid = !tuning.showGrid; }
	}
#else
	if (input.isButtonPressed(platform::Button::NR1)) { tool = noneTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
	if (input.isButtonPressed(platform::Button::NR2)) { tool = brushTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
	if (input.isButtonPressed(platform::Button::NR3)) { tool = rectTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
	if (input.isButtonPressed(platform::Button::NR4)) { tool = measureTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
	if (input.isButtonPressed(platform::Button::NR5)) { tool = doorTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
	if (input.isButtonPressed(platform::Button::G)) { tuning.showGrid = !tuning.showGrid; }
#endif
}

void LevelEditor::updateTools(platform::Input &input, Room &room, bool gameViewHovered)
{
	if (currentLevelName.empty())
	{
		rectDragActive = false;
		doorDragActive = false;
		doorResizeActive = false;
		doorSpawnDragActive = false;
		return;
	}

#if REMOVE_IMGUI == 0
	if (imguiBlocksEditorMouse(gameViewHovered))
	{
		return;
	}
#endif

	if (tool == doorTool)
	{
		if (selectedDoorIndex >= static_cast<int>(room.doors.size()))
		{
			clearDoorSelection();
		}

		if (doorSpawnDragActive)
		{
			if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
			{
				doorSpawnDragActive = false;
				return;
			}

			if (!input.isLMouseHeld())
			{
				doorSpawnDragActive = false;
				return;
			}

			if (hoveredTileValid)
			{
				moveSelectedDoorSpawnPosition(room, hoveredTile);
			}

			return;
		}

		glm::vec2 doorMousePoint = mouseWorldPosition;

		if (doorDragActive || doorResizeActive)
		{
			if (!input.isLMouseHeld())
			{
				doorDragActive = false;
				doorResizeActive = false;
				return;
			}

			if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
			{
				clearDoorSelection();
				return;
			}

			if (doorResizeActive)
			{
				glm::ivec2 newSize = {
					static_cast<int>(std::floor(doorMousePoint.x)) - room.doors[selectedDoorIndex].position.x + 1,
					static_cast<int>(std::floor(doorMousePoint.y)) - room.doors[selectedDoorIndex].position.y + 1
				};
				resizeSelectedDoor(room, newSize);
			}
			else if (hoveredTileValid)
			{
				moveSelectedDoor(room, hoveredTile - doorDragGrabOffset);
			}

			return;
		}

		if (!input.isLMousePressed())
		{
			return;
		}

		int hoveredDoorSpawnIndex = getHoveredDoorSpawnIndex(room, doorMousePoint);
		if (hoveredDoorSpawnIndex >= 0)
		{
			selectedDoorIndex = hoveredDoorSpawnIndex;
			syncSelectedDoorBuffer(room);
			doorActionMessage.clear();
			doorActionHasError = false;
			doorDragActive = false;
			doorResizeActive = false;
			doorSpawnDragActive = true;
			return;
		}

		int hoveredDoorIndex = getHoveredDoorIndex(room, doorMousePoint);
		if (hoveredDoorIndex >= 0)
		{
			selectedDoorIndex = hoveredDoorIndex;
			syncSelectedDoorBuffer(room);
			doorActionMessage.clear();
			doorActionHasError = false;

			if (hoveredSelectedDoorResizeHandle(room, doorMousePoint))
			{
				doorResizeActive = true;
			}
			else if (hoveredTileValid)
			{
				doorDragActive = true;
				doorDragGrabOffset = hoveredTile - room.doors[selectedDoorIndex].position;
			}

			return;
		}

		if (!hoveredTileValid)
		{
			clearDoorSelection();
			return;
		}

		if (!input.isButtonHeld(platform::Button::LeftCtrl))
		{
			clearDoorSelection();
			return;
		}

		createDoorAtHoveredTile(room);
		doorDragActive = true;
		doorDragGrabOffset = {};
		return;
	}

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
		if (block->solid == solid)
		{
			return;
		}

		block->solid = solid;
		levelDirty = true;
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

	if (room.size.x == newSizeX && room.size.y == newSizeY)
	{
		return;
	}

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

	resizedRoom.doors = room.doors;
	for (Door &door : resizedRoom.doors)
	{
		clampDoorToRoom(door, resizedRoom);
	}

	room = resizedRoom;
	pendingRoomSize = room.size;
	hoveredTileValid = false;
	rectDragActive = false;
	doorDragActive = false;
	doorResizeActive = false;
	if (selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		clearDoorSelection();
	}
	else
	{
		syncSelectedDoorBuffer(room);
	}
	levelDirty = true;
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

void LevelEditor::clearDoorSelection()
{
	selectedDoorIndex = -1;
	doorDragActive = false;
	doorResizeActive = false;
	doorSpawnDragActive = false;
	doorDragGrabOffset = {};
	selectedDoorName[0] = 0;
}

int LevelEditor::getHoveredDoorIndex(Room &room, glm::vec2 mouseWorld)
{
	if (selectedDoorIndex >= 0 && selectedDoorIndex < static_cast<int>(room.doors.size()) &&
		room.doors[selectedDoorIndex].contains(mouseWorld))
	{
		return selectedDoorIndex;
	}

	for (int i = static_cast<int>(room.doors.size()) - 1; i >= 0; i--)
	{
		if (room.doors[i].contains(mouseWorld))
		{
			return i;
		}
	}

	return -1;
}

int LevelEditor::getHoveredDoorSpawnIndex(Room &room, glm::vec2 mouseWorld)
{
	auto doorSpawnContains = [&](Door const &door)
	{
		return
			mouseWorld.x >= door.playerSpawnPosition.x &&
			mouseWorld.y >= door.playerSpawnPosition.y &&
			mouseWorld.x <= door.playerSpawnPosition.x + 1 &&
			mouseWorld.y <= door.playerSpawnPosition.y + 1;
	};

	if (selectedDoorIndex >= 0 && selectedDoorIndex < static_cast<int>(room.doors.size()) &&
		doorSpawnContains(room.doors[selectedDoorIndex]))
	{
		return selectedDoorIndex;
	}

	for (int i = static_cast<int>(room.doors.size()) - 1; i >= 0; i--)
	{
		if (doorSpawnContains(room.doors[i]))
		{
			return i;
		}
	}

	return -1;
}

bool LevelEditor::hoveredSelectedDoorResizeHandle(Room &room, glm::vec2 mouseWorld)
{
	if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		return false;
	}

	Door const &door = room.doors[selectedDoorIndex];
	glm::vec2 handleMin = glm::vec2(door.position) + glm::vec2(door.size) - glm::vec2(0.60f);
	glm::vec2 handleMax = glm::vec2(door.position) + glm::vec2(door.size) + glm::vec2(0.25f);

	return
		mouseWorld.x >= handleMin.x &&
		mouseWorld.y >= handleMin.y &&
		mouseWorld.x <= handleMax.x &&
		mouseWorld.y <= handleMax.y;
}

std::string LevelEditor::getNextDoorName(Room const &room)
{
	for (int index = 1; ; index++)
	{
		std::string candidate = "door " + std::to_string(index);
		if (doorNameIsUnique(room, candidate.c_str(), -1))
		{
			return candidate;
		}
	}
}

bool LevelEditor::doorNameIsUnique(Room const &room, char const *name, int ignoreIndex)
{
	std::string trimmedName = trimWhitespace(name ? name : "");
	if (trimmedName.empty())
	{
		return false;
	}

	for (int i = 0; i < static_cast<int>(room.doors.size()); i++)
	{
		if (i == ignoreIndex)
		{
			continue;
		}

		if (trimWhitespace(room.doors[i].name) == trimmedName)
		{
			return false;
		}
	}

	return true;
}

void LevelEditor::createDoorAtHoveredTile(Room &room)
{
	if (!hoveredTileValid)
	{
		return;
	}

	Door door = {};
	door.name = getNextDoorName(room);
	door.position = hoveredTile;
	door.size = kDefaultDoorSize;
	door.playerSpawnPosition = door.position;
	clampDoorToRoom(door, room);

	room.doors.push_back(door);
	selectedDoorIndex = static_cast<int>(room.doors.size()) - 1;
	syncSelectedDoorBuffer(room);
	doorActionMessage.clear();
	doorActionHasError = false;
	levelDirty = true;
}

void LevelEditor::moveSelectedDoor(Room &room, glm::ivec2 position)
{
	if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		return;
	}

	Door &door = room.doors[selectedDoorIndex];
	if (door.position == position)
	{
		return;
	}

	// Doors are allowed to sit outside the room bounds so exits can hang off map edges.
	door.position = position;
	levelDirty = true;
}

void LevelEditor::resizeSelectedDoor(Room &room, glm::ivec2 size)
{
	if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		return;
	}

	Door &door = room.doors[selectedDoorIndex];
	glm::ivec2 clampedSize = {
		std::max(size.x, 1),
		std::max(size.y, 1)
	};

	if (door.size == clampedSize)
	{
		return;
	}

	door.size = clampedSize;
	levelDirty = true;
}

void LevelEditor::deleteSelectedDoor(Room &room)
{
	if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		return;
	}

	room.doors.erase(room.doors.begin() + selectedDoorIndex);
	clearDoorSelection();
	doorActionHasError = false;
	doorActionMessage = "Deleted selected door";
	levelDirty = true;
}

void LevelEditor::moveSelectedDoorSpawnPosition(Room &room, glm::ivec2 position)
{
	if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		return;
	}

	Door &door = room.doors[selectedDoorIndex];
	glm::ivec2 clampedPosition = {
		std::clamp(position.x, 0, std::max(room.size.x - 1, 0)),
		std::clamp(position.y, 0, std::max(room.size.y - 1, 0))
	};

	if (door.playerSpawnPosition == clampedPosition)
	{
		doorActionHasError = false;
		doorActionMessage = "Player spawn unchanged";
		return;
	}

	door.playerSpawnPosition = clampedPosition;
	doorActionHasError = false;
	doorActionMessage = "Moved selected door player spawn";
	levelDirty = true;
}

void LevelEditor::syncSelectedDoorBuffer(Room &room)
{
	if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		selectedDoorName[0] = 0;
		return;
	}

	copyStringToBuffer(selectedDoorName, room.doors[selectedDoorIndex].name);
}

void LevelEditor::applySelectedDoorName(Room &room)
{
	if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		return;
	}

	std::string trimmedName = trimWhitespace(selectedDoorName);
	if (trimmedName.empty())
	{
		doorActionHasError = true;
		doorActionMessage = "Door names can't be empty";
		return;
	}

	if (!doorNameIsUnique(room, trimmedName.c_str(), selectedDoorIndex))
	{
		doorActionHasError = true;
		doorActionMessage = "Door names must stay unique";
		return;
	}

	Door &door = room.doors[selectedDoorIndex];
	if (door.name == trimmedName)
	{
		doorActionHasError = false;
		doorActionMessage = "Door name unchanged";
		return;
	}

	queuePendingDoorRename(pendingDoorRenames, door.name, trimmedName);
	door.name = trimmedName;
	copyStringToBuffer(selectedDoorName, door.name);
	doorActionHasError = false;
	doorActionMessage = "Renamed selected door";
	levelDirty = true;
}

void LevelEditor::clampDoorToRoom(Door &door, Room const &room)
{
	door.size.x = std::max(door.size.x, 1);
	door.size.y = std::max(door.size.y, 1);
	door.playerSpawnPosition.x = std::clamp(door.playerSpawnPosition.x, 0, std::max(room.size.x - 1, 0));
	door.playerSpawnPosition.y = std::clamp(door.playerSpawnPosition.y, 0, std::max(room.size.y - 1, 0));
}

void LevelEditor::clampCamera(Room &room, gl2d::Renderer2D &renderer)
{
	// Let the editor camera drift a bit outside the room so editing near edges feels less cramped.
	constexpr float cameraPadding = 30.f;

	glm::vec2 center = getViewCenter(renderer);
	glm::vec2 viewSize = getViewSize(renderer);

	float minX = viewSize.x * 0.5f - cameraPadding;
	float maxX = room.size.x - viewSize.x * 0.5f + cameraPadding;
	if (minX > maxX)
	{
		center.x = room.size.x * 0.5f;
	}
	else
	{
		center.x = std::clamp(center.x, minX, maxX);
	}

	float minY = viewSize.y * 0.5f - cameraPadding;
	float maxY = room.size.y - viewSize.y * 0.5f + cameraPadding;
	if (minY > maxY)
	{
		center.y = room.size.y * 0.5f;
	}
	else
	{
		center.y = std::clamp(center.y, minY, maxY);
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

void LevelEditor::drawDoors(Room &room, gl2d::Renderer2D &renderer)
{
	// Keep doors visible at all times so connections are easy to line up while editing tiles.
	for (int i = 0; i < static_cast<int>(room.doors.size()); i++)
	{
		Door const &door = room.doors[i];
		bool selected = i == selectedDoorIndex;
		gl2d::Color4f fillColor = selected ? kSelectedDoorFillColor : kDoorFillColor;
		gl2d::Color4f outlineColor = selected ? kSelectedDoorOutlineColor : kDoorOutlineColor;
		gl2d::Color4f spawnFillColor = selected ? gl2d::Color4f{1.0f, 0.92f, 0.24f, 0.34f} : kDoorSpawnFillColor;
		gl2d::Color4f spawnOutlineColor = selected ? gl2d::Color4f{1.0f, 0.96f, 0.34f, 1.f} : kDoorSpawnOutlineColor;

		renderer.renderRectangle(door.getRectF(), fillColor);
		renderer.renderRectangleOutline(door.getRectF(), outlineColor, selected ? 0.12f : 0.08f);
		renderer.renderLine(
			glm::vec2(door.position) + glm::vec2(door.size) * 0.5f,
			glm::vec2(door.playerSpawnPosition) + glm::vec2(0.5f, 0.5f),
			spawnOutlineColor,
			selected ? 0.10f : 0.07f);
		renderer.renderRectangle(door.getPlayerSpawnRectF(), spawnFillColor);
		renderer.renderRectangleOutline(door.getPlayerSpawnRectF(), spawnOutlineColor, selected ? 0.10f : 0.08f);

		if (selected && tool == doorTool)
		{
			renderer.renderRectangle(
				{
					door.position.x + door.size.x - 0.46f,
					door.position.y + door.size.y - 0.46f,
					0.34f,
					0.34f
				},
				kSelectedDoorOutlineColor);
		}

		if (selected && doorSpawnDragActive)
		{
			renderer.renderRectangleOutline(door.getPlayerSpawnRectF(), {1.0f, 0.98f, 0.40f, 1.f}, 0.14f);
		}
	}

	if (!measureFont.texture.isValid())
	{
		return;
	}

	renderer.pushCamera();

	for (int i = 0; i < static_cast<int>(room.doors.size()); i++)
	{
		Door const &door = room.doors[i];
		glm::vec2 screenPos = worldToScreen(glm::vec2(door.position), renderer) + glm::vec2(8.f, 8.f);

		if (screenPos.x < -240.f || screenPos.y < -80.f ||
			screenPos.x > renderer.windowW + 240.f || screenPos.y > renderer.windowH + 80.f)
		{
			continue;
		}

		char text[128] = {};
		std::snprintf(text, sizeof(text), "%s\n%d x %d", door.name.c_str(), door.size.x, door.size.y);
		char spawnText[128] = {};
		std::snprintf(spawnText, sizeof(spawnText), "%s", door.name.c_str());

		gl2d::Color4f textColor = (i == selectedDoorIndex)
			? gl2d::Color4f{1.0f, 0.88f, 0.40f, 1.f}
			: gl2d::Color4f{1.0f, 0.74f, 0.42f, 0.82f};
		gl2d::Color4f spawnTextColor = (i == selectedDoorIndex)
			? gl2d::Color4f{1.0f, 0.96f, 0.44f, 1.f}
			: gl2d::Color4f{1.0f, 0.90f, 0.36f, 0.84f};

		renderer.renderText(
			screenPos,
			text,
			measureFont,
			textColor,
			18.f,
			4.f,
			3.f,
			false,
			{0.10f, 0.08f, 0.04f, 0.90f});

		glm::vec2 spawnScreenPos =
			worldToScreen(glm::vec2(door.playerSpawnPosition) + glm::vec2(1.f, 0.f), renderer) +
			glm::vec2(8.f, -6.f);

		renderer.renderText(
			spawnScreenPos,
			spawnText,
			measureFont,
			spawnTextColor,
			16.f,
			4.f,
			3.f,
			false,
			{0.10f, 0.09f, 0.04f, 0.86f});
	}

	renderer.popCamera();
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
	if (tool == doorTool)
	{
		hoverColor = doorSpawnDragActive
			? gl2d::Color4f{1.0f, 0.94f, 0.28f, 0.98f}
			: gl2d::Color4f{1.0f, 0.74f, 0.22f, 0.95f};
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

	//level editor
	if (ImGui::Begin("Editor"))
	{
		bool hasLoadedLevel = !currentLevelName.empty();
		bool hasSelectedDoor = hasLoadedLevel &&
			selectedDoorIndex >= 0 &&
			selectedDoorIndex < static_cast<int>(room.doors.size());

		ImGui::TextUnformatted("F10 hides / shows ImGui");
		ImGui::TextUnformatted("F6 Game, F7 Level Editor, F8 World Editor");
		ImGui::TextUnformatted("WASD / Arrows move camera, Q/E zoom");
		ImGui::TextUnformatted("Ctrl+S saves the current level");
		ImGui::TextUnformatted("Escape cancels rect, measure, or door drag input");
		if (!hasLoadedLevel)
		{
			ImGui::TextColored({1.f, 0.88f, 0.35f, 1.f}, "Load or create a level file before editing.");
		}

		ImGui::Separator();
		if (!hasLoadedLevel) { ImGui::BeginDisabled(); }
		ImGui::TextUnformatted("Tools");
		if (ImGui::RadioButton("None (1)", tool == noneTool)) { tool = noneTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (ImGui::RadioButton("Brush (2)", tool == brushTool)) { tool = brushTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (ImGui::RadioButton("Rect (3)", tool == rectTool)) { tool = rectTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (ImGui::RadioButton("Measure (4)", tool == measureTool)) { tool = measureTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }
		if (ImGui::RadioButton("Door (5)", tool == doorTool)) { tool = doorTool; rectDragActive = false; doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; }

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
		else if (tool == doorTool)
		{
			ImGui::TextUnformatted("Ctrl+LMB empty tile adds, drag door moves, drag yellow spawn, drag corner resizes");
		}
		else
		{
			ImGui::TextUnformatted("Drag LMB to fill, drag RMB to clear");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Room");
		if (hasLoadedLevel)
		{
			ImGui::Text("Current Size: %d x %d", room.size.x, room.size.y);
		}
		else
		{
			ImGui::TextUnformatted("Current Size: -");
		}
		ImGui::InputInt("Width", &pendingRoomSize.x);
		ImGui::InputInt("Height", &pendingRoomSize.y);
		if (ImGui::Button("Resize Level"))
		{
			pendingResizeConfirmSize = pendingRoomSize;
			ImGui::OpenPopup("Resize Level");
		}
		ImGui::SameLine();
		if (ImGui::Button("Focus Room"))
		{
			focusRoom(room, renderer);
		}
		if (!hasLoadedLevel) { ImGui::EndDisabled(); }

		ImGui::Separator();
		if (!hasLoadedLevel) { ImGui::BeginDisabled(); }
		ImGui::TextUnformatted("Doors");
		ImGui::Text("Count: %d", static_cast<int>(room.doors.size()));
		if (hasSelectedDoor)
		{
			Door const &selectedDoor = room.doors[selectedDoorIndex];
			ImGui::Text("Selected: %s", selectedDoor.name.c_str());
			ImGui::InputText("Door Name", selectedDoorName, sizeof(selectedDoorName));
			if (ImGui::Button("Apply Door Name"))
			{
				applySelectedDoorName(room);
			}

			int doorX = selectedDoor.position.x;
			int doorY = selectedDoor.position.y;
			int doorW = selectedDoor.size.x;
			int doorH = selectedDoor.size.y;
			bool movedDoor = false;
			bool resizedDoor = false;

			if (ImGui::InputInt("Door X", &doorX)) { movedDoor = true; }
			if (ImGui::InputInt("Door Y", &doorY)) { movedDoor = true; }
			if (ImGui::InputInt("Door Width", &doorW)) { resizedDoor = true; }
			if (ImGui::InputInt("Door Height", &doorH)) { resizedDoor = true; }

			if (movedDoor)
			{
				moveSelectedDoor(room, {doorX, doorY});
			}
			if (resizedDoor)
			{
				resizeSelectedDoor(room, {doorW, doorH});
			}

			ImGui::Text("Player Spawn: %d, %d",
				selectedDoor.playerSpawnPosition.x,
				selectedDoor.playerSpawnPosition.y);
			ImGui::TextUnformatted("Drag the yellow spawn marker to move it.");

			if (ImGui::Button("Delete Selected Door"))
			{
				deleteSelectedDoor(room);
			}
		}
		else
		{
			ImGui::TextUnformatted("Selected: none");
			ImGui::TextUnformatted("Pick Door (5) and Ctrl+click inside the room to add one.");
		}

		if (!doorActionMessage.empty())
		{
			ImVec4 doorColor = doorActionHasError
				? ImVec4(1.f, 0.45f, 0.35f, 1.f)
				: ImVec4(1.0f, 0.84f, 0.32f, 1.f);
			ImGui::TextColored(doorColor, "%s", doorActionMessage.c_str());
		}
		if (!hasLoadedLevel) { ImGui::EndDisabled(); }

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
		if (!hasLoadedLevel)
		{
			ImGui::TextUnformatted("Hover Tile: no level loaded");
		}
		else if (hoveredTileValid)
		{
			ImGui::Text("Hover Tile: %d, %d", hoveredTile.x, hoveredTile.y);
			ImGui::Text("Solid: %d", room.getBlockUnsafe(hoveredTile.x, hoveredTile.y).solid ? 1 : 0);
		}
		else
		{
			ImGui::TextUnformatted("Hover Tile: outside room");
		}

		ImGui::Text("Camera: %.2f, %.2f", camera.position.x, camera.position.y);

		if (ImGui::BeginPopupModal("Resize Level", 0, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Resize \"%s\" from %d x %d to %d x %d?",
				currentLevelName.c_str(),
				room.size.x,
				room.size.y,
				std::max(pendingResizeConfirmSize.x, 1),
				std::max(pendingResizeConfirmSize.y, 1));

			if (ImGui::Button("Apply", {120.f, 0.f}))
			{
				resizeRoom(room, pendingResizeConfirmSize.x, pendingResizeConfirmSize.y);
				clampCamera(room, renderer);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", {120.f, 0.f}))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();
#endif
}

void LevelEditor::drawUnsavedChangesWindow(Room &room, gl2d::Renderer2D &renderer)
{
#if REMOVE_IMGUI == 0
	if (ImGui::BeginPopupModal("Unsaved Changes", 0, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("The current level has unsaved changes.");
		ImGui::TextUnformatted("Press Save Current or Ctrl+S first, or discard and continue.");

		if (ImGui::Button("Discard", {120.f, 0.f}))
		{
			pendingApplyPendingFileAction = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", {120.f, 0.f}))
		{
			pendingFileAction = noPendingFileAction;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
#endif
}

void LevelEditor::saveCurrentLevel(Room &room)
{
	if (currentLevelName.empty())
	{
		return;
	}

	RoomIoResult result = saveRoomToFile(room, currentLevelName.c_str());
	fileActionHasError = !result.success;
	fileActionMessage = result.message;

	if (result.success)
	{
		for (auto const &rename : pendingDoorRenames)
		{
			if (!roomHasDoorNamed(room, rename.newName))
			{
				continue;
			}

			WorldIoResult worldResult = renameDoorReferencesInWorld(
				currentLevelName.c_str(),
				rename.oldName.c_str(),
				rename.newName.c_str());

			if (!worldResult.success)
			{
				fileActionHasError = true;
				fileActionMessage = "Saved level, but couldn't update world door links: " + worldResult.message;
				levelDirty = false;
				return;
			}
		}

		pendingDoorRenames.clear();
		levelDirty = false;
	}
}

void LevelEditor::loadSelectedLevel(Room &room, gl2d::Renderer2D &renderer)
{
	if (selectedLevelName.empty())
	{
		return;
	}

	Room loadedRoom = {};
	RoomIoResult result = loadRoomFromFile(loadedRoom, selectedLevelName.c_str());
	fileActionHasError = !result.success;
	fileActionMessage = result.message;

	if (result.success)
	{
		room = loadedRoom;
		currentLevelName = result.levelName;
		selectedLevelName = result.levelName;
		pendingRoomSize = room.size;
		newLevelSize = room.size;
		hoveredTileValid = false;
		rectDragActive = false;
		clearDoorSelection();
		doorActionMessage.clear();
		doorActionHasError = false;
		pendingDoorRenames.clear();
		levelDirty = false;
		copyStringToBuffer(renameName, result.levelName);
		focusRoom(room, renderer);
	}
}

void LevelEditor::reloadCurrentLevel(Room &room, gl2d::Renderer2D &renderer)
{
	if (currentLevelName.empty())
	{
		return;
	}

	std::string previousSelection = selectedLevelName;
	selectedLevelName = currentLevelName;
	loadSelectedLevel(room, renderer);
	selectedLevelName = currentLevelName.empty() ? previousSelection : currentLevelName;
}

void LevelEditor::createNewLevel(Room &room, gl2d::Renderer2D &renderer)
{
	if (newLevelName[0] == 0)
	{
		fileActionHasError = true;
		fileActionMessage = "Type a name for the new level first";
		return;
	}

	if (roomFileExists(newLevelName))
	{
		fileActionHasError = true;
		fileActionMessage = "A level with that name already exists";
		return;
	}

	Room newRoom = {};
	newRoom.create(std::max(newLevelSize.x, 1), std::max(newLevelSize.y, 1));

	RoomIoResult result = saveRoomToFile(newRoom, newLevelName);
	fileActionHasError = !result.success;
	fileActionMessage = result.message;

	if (result.success)
	{
		room = newRoom;
		currentLevelName = result.levelName;
		selectedLevelName = result.levelName;
		pendingRoomSize = room.size;
		newLevelSize = room.size;
		hoveredTileValid = false;
		rectDragActive = false;
		clearDoorSelection();
		doorActionMessage.clear();
		doorActionHasError = false;
		pendingDoorRenames.clear();
		levelDirty = false;
		newLevelName[0] = 0;
		copyStringToBuffer(renameName, result.levelName);
		focusRoom(room, renderer);
	}
}

void LevelEditor::drawLevelFilesWindow(Room &room, gl2d::Renderer2D &renderer)
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
		renameName[0] = 0;
		hoveredTileValid = false;
		rectDragActive = false;
		clearDoorSelection();
		doorActionMessage.clear();
		doorActionHasError = false;
		levelDirty = false;
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
		ImGui::RadioButton("Level Editor", true);
		ImGui::SameLine();
		if (ImGui::RadioButton("World Editor", false))
		{
			requestWorldEditorMode = true;
		}

		ImGui::Separator();
		ImGui::Text("Folder: %s", getRoomFilesFolder().c_str());
		if (currentLevelName.empty())
		{
			ImGui::TextUnformatted("Current Level: None loaded");
		}
		else if (levelDirty)
		{
			ImGui::TextColored({1.0f, 0.90f, 0.30f, 1.f}, "Current Level: %s*", currentLevelName.c_str());
		}
		else
		{
			ImGui::TextColored({0.35f, 1.f, 0.55f, 1.f}, "Current Level: %s", currentLevelName.c_str());
		}
		if (!currentLevelName.empty())
		{
			ImGui::Text("Room Size: %d x %d", room.size.x, room.size.y);
		}
		else
		{
			ImGui::TextUnformatted("Room Size: -");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Create New");
		ImGui::InputText("New Level Name", newLevelName, sizeof(newLevelName));
		ImGui::InputInt("New Width", &newLevelSize.x);
		ImGui::InputInt("New Height", &newLevelSize.y);

		if (ImGui::Button("Create New Level"))
		{
			if (levelDirty)
			{
				pendingFileAction = createNewFileAction;
				ImGui::OpenPopup("Unsaved Changes");
			}
			else
			{
				createNewLevel(room, renderer);
			}
		}

		ImGui::Separator();
		ImGui::Text("Existing Levels (%d)", static_cast<int>(levelFiles.files.size()));
		//ImGui::TextColored({0.35f, 1.f, 0.55f, 1.f}, "Green = loaded"); //dont need this
		//ImGui::TextColored({1.0f, 0.90f, 0.30f, 1.f}, "Yellow = loaded with unsaved changes"); //dont need this
		if (!levelFiles.error.empty())
		{
			ImGui::TextColored({1.f, 0.45f, 0.35f, 1.f}, "%s", levelFiles.error.c_str());
		}

		if (ImGui::BeginChild("LevelFileList", {0.f, 220.f}, true))
		{
			for (auto const &file : levelFiles.files)
			{
				bool selected = file.name == selectedLevelName;
				bool loaded = file.name == currentLevelName;
				std::string label = file.name;
				if (loaded)
				{
					label += levelDirty ? "  [loaded*]" : "  [loaded]";
					ImGui::PushStyleColor(ImGuiCol_Text,
						levelDirty ? ImVec4(1.0f, 0.90f, 0.30f, 1.f) : ImVec4(0.35f, 1.f, 0.55f, 1.f));
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
		bool canSaveCurrent = !currentLevelName.empty();
		bool canRenameSelected = !selectedLevelName.empty();
		bool canDeleteSelected = !selectedLevelName.empty();

		if (!canLoadSelected) { ImGui::BeginDisabled(); }
		if (ImGui::Button("Load Selected"))
		{
			if (levelDirty)
			{
				pendingFileAction = loadSelectedFileAction;
				ImGui::OpenPopup("Unsaved Changes");
			}
			else
			{
				loadSelectedLevel(room, renderer);
			}
		}
		if (!canLoadSelected) { ImGui::EndDisabled(); }

		if (!canSaveCurrent) { ImGui::BeginDisabled(); }
		ImGui::SameLine();
		if (ImGui::Button("Save Current"))
		{
			saveCurrentLevel(room);
		}
		if (!canSaveCurrent) { ImGui::EndDisabled(); }

		bool canDiscardCurrent = canSaveCurrent && levelDirty;
		if (!canDiscardCurrent) { ImGui::BeginDisabled(); }
		ImGui::SameLine();
		if (ImGui::Button("Discard Changes"))
		{
			ImGui::OpenPopup("Discard Current Changes");
		}
		if (!canDiscardCurrent) { ImGui::EndDisabled(); }

		ImGui::InputText("Rename To", renameName, sizeof(renameName));

		if (!canRenameSelected) { ImGui::BeginDisabled(); }
		if (ImGui::Button("Rename Selected"))
		{
			RoomIoResult result = renameRoomFile(selectedLevelName.c_str(), renameName);
			fileActionHasError = !result.success;
			fileActionMessage = result.message;

			if (result.success)
			{
				if (currentLevelName == selectedLevelName)
				{
					currentLevelName = result.levelName;
				}

				selectedLevelName = result.levelName;
				copyStringToBuffer(renameName, result.levelName);
			}
		}
		if (!canRenameSelected) { ImGui::EndDisabled(); }

		if (!canDeleteSelected) { ImGui::BeginDisabled(); }
		ImGui::SameLine();
		if (ImGui::Button("Delete Selected"))
		{
			pendingDeleteLevelName = selectedLevelName;
			ImGui::OpenPopup("Delete Level");
		}
		if (!canDeleteSelected) { ImGui::EndDisabled(); }

		if (ImGui::BeginPopupModal("Delete Level", 0, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Delete \"%s\"?", pendingDeleteLevelName.c_str());
			ImGui::TextUnformatted("This can't be undone.");

			if (ImGui::Button("Delete", {120.f, 0.f}))
			{
				RoomIoResult result = deleteRoomFile(pendingDeleteLevelName.c_str());
				fileActionHasError = !result.success;
				fileActionMessage = result.message;

				if (result.success)
				{
					if (currentLevelName == pendingDeleteLevelName)
					{
						currentLevelName.clear();
						renameName[0] = 0;
						hoveredTileValid = false;
						rectDragActive = false;
						clearDoorSelection();
						doorActionMessage.clear();
						doorActionHasError = false;
						pendingDoorRenames.clear();
						levelDirty = false;
					}

					if (selectedLevelName == pendingDeleteLevelName)
					{
						selectedLevelName.clear();
					}
				}

				pendingDeleteLevelName.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", {120.f, 0.f}))
			{
				pendingDeleteLevelName.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal("Discard Current Changes", 0, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Reload \"%s\" from disk and discard your edits?", currentLevelName.c_str());

			if (ImGui::Button("Discard", {120.f, 0.f}))
			{
				pendingReloadCurrentLevel = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", {120.f, 0.f}))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (!fileActionMessage.empty())
		{
			ImGui::Separator();
			ImVec4 color = fileActionHasError
				? ImVec4(1.f, 0.45f, 0.35f, 1.f)
				: ImVec4(0.35f, 1.f, 0.55f, 1.f);
			ImGui::TextColored(color, "%s", fileActionMessage.c_str());
		}
	}
	ImGui::End();
#endif
}
