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
	constexpr int kMaxUndoSteps = 100;
	const glm::ivec2 kDefaultDoorSize = {2, 3};
	const gl2d::Color4f kDoorFillColor = {1.0f, 0.56f, 0.12f, 0.14f};
	const gl2d::Color4f kDoorOutlineColor = {1.0f, 0.64f, 0.18f, 0.72f};
	const gl2d::Color4f kSelectedDoorFillColor = {1.0f, 0.62f, 0.12f, 0.22f};
	const gl2d::Color4f kSelectedDoorOutlineColor = {1.0f, 0.78f, 0.28f, 1.0f};
	const gl2d::Color4f kDoorSpawnFillColor = {1.0f, 0.88f, 0.18f, 0.24f};
	const gl2d::Color4f kDoorSpawnOutlineColor = {1.0f, 0.92f, 0.32f, 0.96f};
	const gl2d::Color4f kSpawnRegionFillColor = {0.26f, 0.56f, 1.0f, 0.08f};
	const gl2d::Color4f kSpawnRegionOutlineColor = {0.38f, 0.68f, 1.0f, 0.34f};
	const gl2d::Color4f kSelectedSpawnRegionFillColor = {0.32f, 0.62f, 1.0f, 0.14f};
	const gl2d::Color4f kSelectedSpawnRegionOutlineColor = {0.58f, 0.82f, 1.0f, 0.92f};
	const gl2d::Color4f kSpawnRegionPointFillColor = {0.44f, 0.74f, 1.0f, 0.20f};
	const gl2d::Color4f kSpawnRegionPointOutlineColor = {0.62f, 0.86f, 1.0f, 0.92f};
	const gl2d::Color4f kZiplineLineColor = {0.76f, 0.78f, 0.80f, 0.82f};
	const gl2d::Color4f kSelectedZiplineLineColor = {0.92f, 0.94f, 0.98f, 1.0f};
	const gl2d::Color4f kZiplinePointFillColor = {0.94f, 0.82f, 0.22f, 0.24f};
	const gl2d::Color4f kZiplinePointOutlineColor = {0.98f, 0.88f, 0.34f, 0.94f};
	const gl2d::Color4f kSelectedZiplinePointFillColor = {1.0f, 0.90f, 0.26f, 0.34f};
	const gl2d::Color4f kSelectedZiplinePointOutlineColor = {1.0f, 0.96f, 0.42f, 1.0f};
	constexpr float kZiplineLineWidth = 0.06f;
	constexpr float kZiplinePointSize = 0.46f;
	constexpr float kSpawnRegionPointSize = 0.42f;

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

	SpawnRegionRect makeSpawnRegionRect(glm::ivec2 a, glm::ivec2 b)
	{
		glm::ivec2 minCorner = {
			std::min(a.x, b.x),
			std::min(a.y, b.y)
		};
		glm::ivec2 maxCorner = {
			std::max(a.x, b.x),
			std::max(a.y, b.y)
		};

		SpawnRegionRect rect = {};
		rect.position = minCorner;
		rect.size = maxCorner - minCorner + glm::ivec2(1, 1);
		return rect;
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

	void queuePendingDoorDelete(
		std::vector<LevelEditor::PendingDoorRename> &pendingDoorRenames,
		std::vector<std::string> &pendingDoorDeletes,
		std::string const &doorName)
	{
		if (doorName.empty())
		{
			return;
		}

		std::string deletedDoorName = doorName;

		for (auto it = pendingDoorRenames.begin(); it != pendingDoorRenames.end(); )
		{
			if (it->newName == doorName || it->oldName == doorName)
			{
				deletedDoorName = it->oldName.empty() ? deletedDoorName : it->oldName;
				it = pendingDoorRenames.erase(it);
			}
			else
			{
				++it;
			}
		}

		if (std::find(pendingDoorDeletes.begin(), pendingDoorDeletes.end(), deletedDoorName) ==
			pendingDoorDeletes.end())
		{
			pendingDoorDeletes.push_back(deletedDoorName);
		}
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
			ImGui::IsPopupOpen("Resize Level") ||
			ImGui::IsPopupOpen("Delete Door") ||
			ImGui::IsPopupOpen("Delete Spawn Region") ||
			ImGui::IsPopupOpen("Delete Zipline");
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
		resetUndoHistory(room);
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

	resetUndoHistory(room);
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
		if (selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
		{
			clearSpawnRegionSelection();
		}
		if (selectedZiplineIndex >= static_cast<int>(room.ziplines.size()))
		{
			clearZiplineSelection();
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
		clearMoveSelection();
		clearDoorSelection();
		clearSpawnRegionSelection();
		clearZiplineSelection();
	}

	updateShortcuts(input, renderer, room, gameViewFocused);
	updateCamera(deltaTime, input, renderer, room);
	updateHoveredTile(input, renderer, room);
	updateTools(input, room, gameViewHovered);

	renderer.setCamera(camera);

	if (!currentLevelName.empty())
	{
		drawRoom(room, renderer);
		drawGrid(room, renderer);
		drawMoveSelection(renderer);
		drawSpawnRegions(room, renderer);
		drawDoors(room, renderer);
		drawZiplines(room, renderer);
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

void LevelEditor::updateShortcuts(platform::Input &input, gl2d::Renderer2D &renderer, Room &room, bool gameViewFocused)
{
	bool hadMoveOperation = tool == moveTool &&
		(rectDragActive || moveSelection.active || moveSelection.previewActive || moveSelection.dragging);

	if (input.isButtonPressed(platform::Button::Escape))
	{
		rectDragActive = false;
		doorDragActive = false;
		doorResizeActive = false;
		doorSpawnDragActive = false;
		spawnRegionDragActive = false;
		spawnRegionResizeActive = false;
		spawnRegionSpawnDragActive = false;
		spawnRegionCreateDragActive = false;
		spawnRegionAddRectArmed = false;
		ziplineCreateDragActive = false;
		ziplinePointDragActive = false;
		if (hadMoveOperation)
		{
			clearMoveSelection();
			return;
		}
	}

	bool saveShortcut = !currentLevelName.empty() &&
		input.isButtonHeld(platform::Button::LeftCtrl) &&
		input.isButtonPressed(platform::Button::S);
	if (saveShortcut)
	{
		saveCurrentLevel(room);
	}

#if REMOVE_IMGUI == 0
	auto resetToolState = [&]()
	{
		rectDragActive = false;
		doorDragActive = false;
		doorResizeActive = false;
		doorSpawnDragActive = false;
		spawnRegionDragActive = false;
		spawnRegionResizeActive = false;
		spawnRegionSpawnDragActive = false;
		spawnRegionCreateDragActive = false;
		spawnRegionAddRectArmed = false;
		ziplineCreateDragActive = false;
		ziplinePointDragActive = false;
		clearMoveSelection();
	};

	bool allowUndoRedoShortcut = !editorModalPopupOpen();
	if (allowUndoRedoShortcut)
	{
		ImGuiIO &io = ImGui::GetIO();
		allowUndoRedoShortcut = gameViewFocused || (!io.WantTextInput && !ImGui::IsAnyItemActive());
	}

	bool undoShortcut =
		allowUndoRedoShortcut &&
		input.isButtonHeld(platform::Button::LeftCtrl) &&
		input.isButtonPressed(platform::Button::Z) &&
		!input.isButtonHeld(platform::Button::LeftShift);
	bool redoShortcut =
		allowUndoRedoShortcut &&
		(
			(input.isButtonHeld(platform::Button::LeftCtrl) &&
			 input.isButtonPressed(platform::Button::Y)) ||
			(input.isButtonHeld(platform::Button::LeftCtrl) &&
			 input.isButtonHeld(platform::Button::LeftShift) &&
			 input.isButtonPressed(platform::Button::Z))
		);

	if (undoShortcut)
	{
		undo(room, renderer);
		return;
	}

	if (redoShortcut)
	{
		redo(room, renderer);
		return;
	}

	bool allowTabShortcut = !editorModalPopupOpen();
	if (allowTabShortcut)
	{
		ImGuiIO &io = ImGui::GetIO();
		allowTabShortcut = gameViewFocused || (!io.WantTextInput && !ImGui::IsAnyItemActive());
	}

	if (allowTabShortcut && input.isButtonPressed(platform::Button::Tab))
	{
		requestWorldEditorMode = true;
	}

	bool allowMoveShortcut = !editorModalPopupOpen();
	if (allowMoveShortcut)
	{
		ImGuiIO &io = ImGui::GetIO();
		allowMoveShortcut = gameViewFocused || (!io.WantTextInput && !ImGui::IsAnyItemActive());
	}

	if (allowMoveShortcut &&
		tool == moveTool &&
		moveSelection.active &&
		moveSelection.previewActive &&
		!moveSelection.dragging &&
		input.isButtonPressed(platform::Button::Enter))
	{
		commitMoveSelection(room);
		return;
	}

	bool allowDeleteShortcut = !editorModalPopupOpen();
	if (allowDeleteShortcut)
	{
		ImGuiIO &io = ImGui::GetIO();
		allowDeleteShortcut = gameViewFocused || (!io.WantTextInput && !ImGui::IsAnyItemActive());
	}

	if (allowDeleteShortcut && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		if (tool == ziplineTool)
		{
			requestDeleteSelectedZipline(room);
		}
		else if (tool == spawnRegionTool)
		{
			requestDeleteSelectedSpawnRegion(room);
		}
		else
		{
			requestDeleteSelectedDoor(room);
		}
	}

	if (!ImGui::isImguiWindowOpen())
	{
		if (input.isButtonPressed(platform::Button::NR1)) { tool = noneTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR2)) { tool = brushTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR3)) { tool = rectTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR4)) { tool = measureTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR5)) { tool = doorTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR6)) { tool = moveTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR7)) { tool = ziplineTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR8)) { tool = spawnRegionTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR9)) { tool = spikeTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::G)) { tuning.showGrid = !tuning.showGrid; }
		return;
	}

	ImGuiIO &io = ImGui::GetIO();
	if (gameViewFocused || !io.WantCaptureKeyboard)
	{
		if (input.isButtonPressed(platform::Button::NR1)) { tool = noneTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR2)) { tool = brushTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR3)) { tool = rectTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR4)) { tool = measureTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR5)) { tool = doorTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR6)) { tool = moveTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR7)) { tool = ziplineTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR8)) { tool = spawnRegionTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::NR9)) { tool = spikeTool; resetToolState(); }
		if (input.isButtonPressed(platform::Button::G)) { tuning.showGrid = !tuning.showGrid; }
	}
#else
	auto resetToolState = [&]()
	{
		rectDragActive = false;
		doorDragActive = false;
		doorResizeActive = false;
		doorSpawnDragActive = false;
		spawnRegionDragActive = false;
		spawnRegionResizeActive = false;
		spawnRegionSpawnDragActive = false;
		spawnRegionCreateDragActive = false;
		spawnRegionAddRectArmed = false;
		ziplineCreateDragActive = false;
		ziplinePointDragActive = false;
		clearMoveSelection();
	};

	bool undoShortcut =
		input.isButtonHeld(platform::Button::LeftCtrl) &&
		input.isButtonPressed(platform::Button::Z) &&
		!input.isButtonHeld(platform::Button::LeftShift);
	bool redoShortcut =
		(input.isButtonHeld(platform::Button::LeftCtrl) &&
		 input.isButtonPressed(platform::Button::Y)) ||
		(input.isButtonHeld(platform::Button::LeftCtrl) &&
		 input.isButtonHeld(platform::Button::LeftShift) &&
		 input.isButtonPressed(platform::Button::Z));

	if (undoShortcut)
	{
		undo(room, renderer);
		return;
	}

	if (redoShortcut)
	{
		redo(room, renderer);
		return;
	}

	if (input.isButtonPressed(platform::Button::Tab))
	{
		requestWorldEditorMode = true;
	}

	if (tool == moveTool &&
		moveSelection.active &&
		moveSelection.previewActive &&
		!moveSelection.dragging &&
		input.isButtonPressed(platform::Button::Enter))
	{
		commitMoveSelection(room);
		return;
	}

	if (input.isButtonPressed(platform::Button::NR1)) { tool = noneTool; resetToolState(); }
	if (input.isButtonPressed(platform::Button::NR2)) { tool = brushTool; resetToolState(); }
	if (input.isButtonPressed(platform::Button::NR3)) { tool = rectTool; resetToolState(); }
	if (input.isButtonPressed(platform::Button::NR4)) { tool = measureTool; resetToolState(); }
	if (input.isButtonPressed(platform::Button::NR5)) { tool = doorTool; resetToolState(); }
	if (input.isButtonPressed(platform::Button::NR6)) { tool = moveTool; resetToolState(); }
	if (input.isButtonPressed(platform::Button::NR7)) { tool = ziplineTool; resetToolState(); }
	if (input.isButtonPressed(platform::Button::NR8)) { tool = spawnRegionTool; resetToolState(); }
	if (input.isButtonPressed(platform::Button::NR9)) { tool = spikeTool; resetToolState(); }
	if (input.isButtonPressed(platform::Button::G)) { tuning.showGrid = !tuning.showGrid; }
#endif
}

void LevelEditor::updateTools(platform::Input &input, Room &room, bool gameViewHovered)
{
	if (currentLevelName.empty())
	{
		rectDragActive = false;
		clearMoveSelection();
		doorDragActive = false;
		doorResizeActive = false;
		doorSpawnDragActive = false;
		clearSpawnRegionSelection();
		clearZiplineSelection();
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
				pushUndoSnapshot(room);
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
				if (doorDragActive || doorResizeActive)
				{
					pushUndoSnapshot(room);
				}
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

	if (tool == spawnRegionTool)
	{
		if (selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
		{
			clearSpawnRegionSelection();
		}
		else if (selectedSpawnRegionIndex >= 0)
		{
			SpawnRegion const &selectedRegion = room.spawnRegions[selectedSpawnRegionIndex];
			if (selectedRegion.rects.empty())
			{
				clearSpawnRegionSelection();
			}
			else if (selectedSpawnRegionRectIndex < 0 ||
				selectedSpawnRegionRectIndex >= static_cast<int>(selectedRegion.rects.size()))
			{
				selectedSpawnRegionRectIndex = 0;
			}
		}

		if (spawnRegionSpawnDragActive)
		{
			if (selectedSpawnRegionIndex < 0 ||
				selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
			{
				clearSpawnRegionSelection();
				return;
			}

			if (!input.isLMouseHeld())
			{
				spawnRegionSpawnDragActive = false;
				pushUndoSnapshot(room);
				return;
			}

			if (hoveredTileValid)
			{
				moveSelectedSpawnRegionSpawn(room, hoveredTile);
			}

			return;
		}

		if (spawnRegionDragActive || spawnRegionResizeActive)
		{
			if (!input.isLMouseHeld())
			{
				if (spawnRegionDragActive || spawnRegionResizeActive)
				{
					pushUndoSnapshot(room);
				}
				spawnRegionDragActive = false;
				spawnRegionResizeActive = false;
				return;
			}

			if (selectedSpawnRegionIndex < 0 ||
				selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
			{
				clearSpawnRegionSelection();
				return;
			}

			SpawnRegion const &selectedRegion = room.spawnRegions[selectedSpawnRegionIndex];
			if (selectedSpawnRegionRectIndex < 0 ||
				selectedSpawnRegionRectIndex >= static_cast<int>(selectedRegion.rects.size()))
			{
				clearSpawnRegionSelection();
				return;
			}

			if (spawnRegionResizeActive)
			{
				SpawnRegionRect const &selectedRect = selectedRegion.rects[selectedSpawnRegionRectIndex];
				glm::ivec2 newSize = {
					static_cast<int>(std::floor(mouseWorldPosition.x)) - selectedRect.position.x + 1,
					static_cast<int>(std::floor(mouseWorldPosition.y)) - selectedRect.position.y + 1
				};
				resizeSelectedSpawnRegionRect(room, newSize);
			}
			else if (hoveredTileValid)
			{
				moveSelectedSpawnRegion(room, hoveredTile - spawnRegionDragGrabOffset);
			}

			return;
		}

		if (spawnRegionCreateDragActive)
		{
			if (hoveredTileValid)
			{
				spawnRegionCreateEnd = hoveredTile;
			}

			if (input.isLMouseReleased())
			{
				spawnRegionCreateDragActive = false;

				if (hoveredTileValid)
				{
					if (spawnRegionAddRectArmed &&
						selectedSpawnRegionIndex >= 0 &&
						selectedSpawnRegionIndex < static_cast<int>(room.spawnRegions.size()))
					{
						addRectToSelectedSpawnRegion(room, spawnRegionCreateStart, spawnRegionCreateEnd);
					}
					else
					{
						createSpawnRegion(room, spawnRegionCreateStart, spawnRegionCreateEnd);
					}
					pushUndoSnapshot(room);
				}

				spawnRegionAddRectArmed = false;
			}

			return;
		}

		if (!input.isLMousePressed())
		{
			return;
		}

		int hoveredSpawnRegionIndex = getHoveredSpawnRegionSpawnIndex(room, mouseWorldPosition);
		if (hoveredSpawnRegionIndex >= 0)
		{
			selectedSpawnRegionIndex = hoveredSpawnRegionIndex;
			if (selectedSpawnRegionRectIndex < 0 ||
				selectedSpawnRegionRectIndex >= static_cast<int>(room.spawnRegions[selectedSpawnRegionIndex].rects.size()))
			{
				selectedSpawnRegionRectIndex = 0;
			}
			spawnRegionActionMessage.clear();
			spawnRegionActionHasError = false;
			spawnRegionSpawnDragActive = true;
			return;
		}

		int hoveredRegionIndex = -1;
		int hoveredRegionRectIndex = -1;
		if (getHoveredSpawnRegionRect(room, mouseWorldPosition, hoveredRegionIndex, hoveredRegionRectIndex))
		{
			selectedSpawnRegionIndex = hoveredRegionIndex;
			selectedSpawnRegionRectIndex = hoveredRegionRectIndex;
			spawnRegionActionMessage.clear();
			spawnRegionActionHasError = false;

			if (hoveredSelectedSpawnRegionResizeHandle(room, mouseWorldPosition))
			{
				spawnRegionResizeActive = true;
			}
			else if (hoveredTileValid)
			{
				spawnRegionDragActive = true;
				spawnRegionDragGrabOffset =
					hoveredTile - room.spawnRegions[selectedSpawnRegionIndex].rects[selectedSpawnRegionRectIndex].position;
			}

			return;
		}

		if (!hoveredTileValid)
		{
			clearSpawnRegionSelection();
			spawnRegionAddRectArmed = false;
			return;
		}

		if (spawnRegionAddRectArmed &&
			selectedSpawnRegionIndex >= 0 &&
			selectedSpawnRegionIndex < static_cast<int>(room.spawnRegions.size()))
		{
			spawnRegionCreateDragActive = true;
			spawnRegionCreateStart = hoveredTile;
			spawnRegionCreateEnd = hoveredTile;
			return;
		}

		if (input.isButtonHeld(platform::Button::LeftCtrl))
		{
			clearSpawnRegionSelection();
			spawnRegionCreateDragActive = true;
			spawnRegionCreateStart = hoveredTile;
			spawnRegionCreateEnd = hoveredTile;
			spawnRegionAddRectArmed = false;
			return;
		}

		clearSpawnRegionSelection();
		spawnRegionAddRectArmed = false;
		return;
	}

	if (tool == moveTool)
	{
		if (moveSelection.dragging)
		{
			if (!input.isLMouseHeld())
			{
				moveSelection.dragging = false;
				return;
			}

			glm::ivec2 moveTile = {
				static_cast<int>(std::floor(mouseWorldPosition.x)),
				static_cast<int>(std::floor(mouseWorldPosition.y))
			};
			moveSelection.previewActive = true;
			moveSelection.previewPosition = moveTile - moveSelection.dragGrabOffset;
			return;
		}

		if (rectDragActive)
		{
			if (hoveredTileValid)
			{
				rectDragEnd = hoveredTile;
			}

			if (input.isLMouseReleased())
			{
				rectDragActive = false;
				createMoveSelection(room, rectDragStart, rectDragEnd);
			}

			return;
		}

		if (!input.isLMousePressed())
		{
			return;
		}

		glm::ivec2 moveTile = {
			static_cast<int>(std::floor(mouseWorldPosition.x)),
			static_cast<int>(std::floor(mouseWorldPosition.y))
		};

		if (input.isButtonHeld(platform::Button::LeftCtrl) && moveSelection.active)
		{
			bool hitSelection = moveSelectionPreviewContainsTile(moveTile);
			if (!moveSelection.previewActive)
			{
				hitSelection = moveSelectionContainsTile(moveTile);
			}

			if (hitSelection)
			{
				glm::ivec2 dragStart = moveSelection.previewActive
					? moveSelection.previewPosition
					: moveSelection.sourceStart;
				moveSelection.dragging = true;
				moveSelection.previewActive = true;
				moveSelection.previewPosition = dragStart;
				moveSelection.dragGrabOffset = moveTile - dragStart;
				return;
			}
		}

		if (!hoveredTileValid)
		{
			return;
		}

		clearMoveSelection();
		rectDragActive = true;
		rectDragStart = hoveredTile;
		rectDragEnd = hoveredTile;
		return;
	}

	if (tool == ziplineTool)
	{
		if (selectedZiplineIndex >= static_cast<int>(room.ziplines.size()))
		{
			clearZiplineSelection();
		}

		if (ziplinePointDragActive)
		{
			if (selectedZiplineIndex < 0 || selectedZiplineIndex >= static_cast<int>(room.ziplines.size()) ||
				selectedZiplinePoint < 0 || selectedZiplinePoint > 1)
			{
				clearZiplineSelection();
				return;
			}

			if (!input.isLMouseHeld())
			{
				ziplinePointDragActive = false;
				pushUndoSnapshot(room);
				return;
			}

			if (hoveredTileValid)
			{
				moveSelectedZiplinePoint(room, hoveredTile);
			}
			return;
		}

		if (ziplineCreateDragActive)
		{
			if (hoveredTileValid)
			{
				ziplineCreateEnd = hoveredTile;
			}

			if (input.isLMouseReleased())
			{
				ziplineCreateDragActive = false;
				if (hoveredTileValid && ziplineCreateStart != ziplineCreateEnd)
				{
					createZipline(room, ziplineCreateStart, ziplineCreateEnd);
					pushUndoSnapshot(room);
				}
			}
			return;
		}

		if (!input.isLMousePressed())
		{
			return;
		}

		int hoveredZiplineIndex = -1;
		int hoveredZiplinePoint = -1;
		if (getHoveredZiplinePoint(room, mouseWorldPosition, hoveredZiplineIndex, hoveredZiplinePoint))
		{
			selectedZiplineIndex = hoveredZiplineIndex;
			selectedZiplinePoint = hoveredZiplinePoint;
			ziplinePointDragActive = true;
			doorActionMessage.clear();
			doorActionHasError = false;
			return;
		}

		if (!hoveredTileValid)
		{
			clearZiplineSelection();
			return;
		}

		if (input.isButtonHeld(platform::Button::LeftCtrl))
		{
			clearZiplineSelection();
			ziplineCreateDragActive = true;
			ziplineCreateStart = hoveredTile;
			ziplineCreateEnd = hoveredTile;
			return;
		}

		clearZiplineSelection();
		return;
	}

	if (tool == brushTool)
	{
		rectDragActive = false;

		if (input.isLMousePressed() || input.isRMousePressed())
		{
			brushPaintActive = true;
		}

		if (input.isLMouseHeld() && hoveredTileValid)
		{
			setBlock(room, hoveredTile.x, hoveredTile.y, solidBlock);
		}

		if (input.isRMouseHeld() && hoveredTileValid)
		{
			setBlock(room, hoveredTile.x, hoveredTile.y, emptyBlock);
		}

		if (brushPaintActive && (input.isLMouseReleased() || input.isRMouseReleased()))
		{
			brushPaintActive = false;
			pushUndoSnapshot(room);
		}
	}
	else if (tool == noneTool)
	{
		rectDragActive = false;
		brushPaintActive = false;
	}
	else if (tool == measureTool)
	{
		brushPaintActive = false;
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
		brushPaintActive = false;
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
			fillRect(room, rectDragStart, rectDragEnd, rectDragPlacesSolid ? solidBlock : emptyBlock);
			rectDragActive = false;
			pushUndoSnapshot(room);
		}
	}
	else if (tool == spikeTool)
	{
		brushPaintActive = false;
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
			fillRect(room, rectDragStart, rectDragEnd, rectDragPlacesSolid ? spikeBlock : emptyBlock);
			rectDragActive = false;
			pushUndoSnapshot(room);
		}
	}
}

void LevelEditor::setBlock(Room &room, int x, int y, BlockType type)
{
	if (Block *block = room.getBlockSafe(x, y))
	{
		if (block->type == type)
		{
			return;
		}

		block->type = type;
		levelDirty = true;
	}
}

void LevelEditor::fillRect(Room &room, glm::ivec2 a, glm::ivec2 b, BlockType type)
{
	int minX = std::min(a.x, b.x);
	int minY = std::min(a.y, b.y);
	int maxX = std::max(a.x, b.x);
	int maxY = std::max(a.y, b.y);

	for (int y = minY; y <= maxY; y++)
	{
		for (int x = minX; x <= maxX; x++)
		{
			setBlock(room, x, y, type);
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
	resizedRoom.spawnRegions = room.spawnRegions;
	for (SpawnRegion &spawnRegion : resizedRoom.spawnRegions)
	{
		clampSpawnRegionToRoom(spawnRegion, resizedRoom);
	}
	resizedRoom.ziplines = room.ziplines;
	for (Zipline &zipline : resizedRoom.ziplines)
	{
		clampZiplineToRoom(zipline, resizedRoom);
	}

	room = resizedRoom;
	pendingRoomSize = room.size;
	hoveredTileValid = false;
	rectDragActive = false;
	clearMoveSelection();
	doorDragActive = false;
	doorResizeActive = false;
	clearSpawnRegionSelection();
	ziplineCreateDragActive = false;
	ziplinePointDragActive = false;
	if (selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		clearDoorSelection();
	}
	else
	{
		syncSelectedDoorBuffer(room);
	}
	if (selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
	{
		clearSpawnRegionSelection();
	}
	if (selectedZiplineIndex >= static_cast<int>(room.ziplines.size()))
	{
		clearZiplineSelection();
	}
	levelDirty = true;
	pushUndoSnapshot(room);
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

void LevelEditor::clearMoveSelection()
{
	moveSelection = {};
}

void LevelEditor::createMoveSelection(Room &room, glm::ivec2 a, glm::ivec2 b)
{
	clearMoveSelection();

	int minX = std::min(a.x, b.x);
	int minY = std::min(a.y, b.y);
	int maxX = std::max(a.x, b.x);
	int maxY = std::max(a.y, b.y);

	moveSelection.active = true;
	moveSelection.sourceStart = {minX, minY};
	moveSelection.size = {maxX - minX + 1, maxY - minY + 1};
	moveSelection.previewPosition = moveSelection.sourceStart;
	moveSelection.blockTypes.resize(moveSelection.size.x * moveSelection.size.y, emptyBlock);

	for (int y = 0; y < moveSelection.size.y; y++)
	{
		for (int x = 0; x < moveSelection.size.x; x++)
		{
			Block const *block = room.getBlockSafe(moveSelection.sourceStart.x + x, moveSelection.sourceStart.y + y);
			if (!block || block->isEmpty())
			{
				continue;
			}

			moveSelection.blockTypes[x + y * moveSelection.size.x] = block->type;
		}
	}
}

bool LevelEditor::moveSelectionContainsTile(glm::ivec2 tile) const
{
	if (!moveSelection.active)
	{
		return false;
	}

	return
		tile.x >= moveSelection.sourceStart.x &&
		tile.y >= moveSelection.sourceStart.y &&
		tile.x < moveSelection.sourceStart.x + moveSelection.size.x &&
		tile.y < moveSelection.sourceStart.y + moveSelection.size.y;
}

bool LevelEditor::moveSelectionPreviewContainsTile(glm::ivec2 tile) const
{
	if (!moveSelection.active || !moveSelection.previewActive)
	{
		return false;
	}

	return
		tile.x >= moveSelection.previewPosition.x &&
		tile.y >= moveSelection.previewPosition.y &&
		tile.x < moveSelection.previewPosition.x + moveSelection.size.x &&
		tile.y < moveSelection.previewPosition.y + moveSelection.size.y;
}

void LevelEditor::commitMoveSelection(Room &room)
{
	if (!moveSelection.active || !moveSelection.previewActive)
	{
		return;
	}

	struct Placement
	{
		glm::ivec2 source = {};
		glm::ivec2 destination = {};
		BlockType type = emptyBlock;
	};

	std::vector<Placement> placements = {};

	for (int y = 0; y < moveSelection.size.y; y++)
	{
		for (int x = 0; x < moveSelection.size.x; x++)
		{
			BlockType type = moveSelection.blockTypes[x + y * moveSelection.size.x];
			if (type == emptyBlock)
			{
				continue;
			}

			glm::ivec2 sourcePosition = moveSelection.sourceStart + glm::ivec2(x, y);
			glm::ivec2 destinationPosition = moveSelection.previewPosition + glm::ivec2(x, y);

			if (room.getBlockSafe(destinationPosition.x, destinationPosition.y))
			{
				placements.push_back({sourcePosition, destinationPosition, type});
			}
		}
	}

	auto containsDestination = [&](glm::ivec2 tile)
	{
		for (Placement const &placement : placements)
		{
			if (placement.destination == tile)
			{
				return true;
			}
		}

		return false;
	};

	for (Placement const &placement : placements)
	{
		if (!containsDestination(placement.source))
		{
			setBlock(room, placement.source.x, placement.source.y, emptyBlock);
		}
	}

	for (Placement const &placement : placements)
	{
		setBlock(room, placement.destination.x, placement.destination.y, placement.type);
	}

	clearMoveSelection();
	pushUndoSnapshot(room);
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

void LevelEditor::clearSpawnRegionSelection()
{
	selectedSpawnRegionIndex = -1;
	selectedSpawnRegionRectIndex = -1;
	spawnRegionDragActive = false;
	spawnRegionResizeActive = false;
	spawnRegionSpawnDragActive = false;
	spawnRegionCreateDragActive = false;
	spawnRegionAddRectArmed = false;
	spawnRegionDragGrabOffset = {};
	pendingDeleteSpawnRegionIndex = -1;
}

void LevelEditor::clearZiplineSelection()
{
	selectedZiplineIndex = -1;
	selectedZiplinePoint = -1;
	ziplineCreateDragActive = false;
	ziplinePointDragActive = false;
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

bool LevelEditor::getHoveredSpawnRegionRect(Room &room, glm::vec2 mouseWorld, int &regionIndex, int &rectIndex)
{
	if (selectedSpawnRegionIndex >= 0 && selectedSpawnRegionIndex < static_cast<int>(room.spawnRegions.size()))
	{
		SpawnRegion const &selectedRegion = room.spawnRegions[selectedSpawnRegionIndex];
		if (selectedSpawnRegionRectIndex >= 0 &&
			selectedSpawnRegionRectIndex < static_cast<int>(selectedRegion.rects.size()) &&
			selectedRegion.rects[selectedSpawnRegionRectIndex].contains(mouseWorld))
		{
			regionIndex = selectedSpawnRegionIndex;
			rectIndex = selectedSpawnRegionRectIndex;
			return true;
		}

		for (int i = static_cast<int>(selectedRegion.rects.size()) - 1; i >= 0; i--)
		{
			if (selectedRegion.rects[i].contains(mouseWorld))
			{
				regionIndex = selectedSpawnRegionIndex;
				rectIndex = i;
				return true;
			}
		}
	}

	for (int region = static_cast<int>(room.spawnRegions.size()) - 1; region >= 0; region--)
	{
		for (int rect = static_cast<int>(room.spawnRegions[region].rects.size()) - 1; rect >= 0; rect--)
		{
			if (room.spawnRegions[region].rects[rect].contains(mouseWorld))
			{
				regionIndex = region;
				rectIndex = rect;
				return true;
			}
		}
	}

	return false;
}

int LevelEditor::getHoveredSpawnRegionSpawnIndex(Room &room, glm::vec2 mouseWorld)
{
	auto spawnContains = [&](SpawnRegion const &spawnRegion)
	{
		return
			mouseWorld.x >= spawnRegion.spawnPosition.x &&
			mouseWorld.y >= spawnRegion.spawnPosition.y &&
			mouseWorld.x <= spawnRegion.spawnPosition.x + 1 &&
			mouseWorld.y <= spawnRegion.spawnPosition.y + 1;
	};

	if (selectedSpawnRegionIndex >= 0 &&
		selectedSpawnRegionIndex < static_cast<int>(room.spawnRegions.size()) &&
		spawnContains(room.spawnRegions[selectedSpawnRegionIndex]))
	{
		return selectedSpawnRegionIndex;
	}

	for (int i = static_cast<int>(room.spawnRegions.size()) - 1; i >= 0; i--)
	{
		if (spawnContains(room.spawnRegions[i]))
		{
			return i;
		}
	}

	return -1;
}

bool LevelEditor::hoveredSelectedSpawnRegionResizeHandle(Room &room, glm::vec2 mouseWorld)
{
	if (selectedSpawnRegionIndex < 0 ||
		selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
	{
		return false;
	}

	SpawnRegion const &spawnRegion = room.spawnRegions[selectedSpawnRegionIndex];
	if (selectedSpawnRegionRectIndex < 0 ||
		selectedSpawnRegionRectIndex >= static_cast<int>(spawnRegion.rects.size()))
	{
		return false;
	}

	SpawnRegionRect const &rect = spawnRegion.rects[selectedSpawnRegionRectIndex];
	glm::vec2 handleMin = glm::vec2(rect.position) + glm::vec2(rect.size) - glm::vec2(0.60f);
	glm::vec2 handleMax = glm::vec2(rect.position) + glm::vec2(rect.size) + glm::vec2(0.25f);

	return
		mouseWorld.x >= handleMin.x &&
		mouseWorld.y >= handleMin.y &&
		mouseWorld.x <= handleMax.x &&
		mouseWorld.y <= handleMax.y;
}

bool LevelEditor::getHoveredZiplinePoint(Room &room, glm::vec2 mouseWorld, int &ziplineIndex, int &pointIndex)
{
	auto pointContains = [&](Zipline const &zipline, int index)
	{
		glm::vec2 center = zipline.getPointCenter(index);
		glm::vec2 half = glm::vec2(kZiplinePointSize * 0.75f);
		return
			mouseWorld.x >= center.x - half.x &&
			mouseWorld.y >= center.y - half.y &&
			mouseWorld.x <= center.x + half.x &&
			mouseWorld.y <= center.y + half.y;
	};

	if (selectedZiplineIndex >= 0 && selectedZiplineIndex < static_cast<int>(room.ziplines.size()))
	{
		for (int index = 0; index < 2; index++)
		{
			if (pointContains(room.ziplines[selectedZiplineIndex], index))
			{
				ziplineIndex = selectedZiplineIndex;
				pointIndex = index;
				return true;
			}
		}
	}

	for (int i = static_cast<int>(room.ziplines.size()) - 1; i >= 0; i--)
	{
		for (int index = 0; index < 2; index++)
		{
			if (pointContains(room.ziplines[i], index))
			{
				ziplineIndex = i;
				pointIndex = index;
				return true;
			}
		}
	}

	return false;
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

void LevelEditor::requestDeleteSelectedDoor(Room &room)
{
	if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		return;
	}

	pendingDeleteDoorName = room.doors[selectedDoorIndex].name;
	pendingOpenDeleteDoorPopup = true;
}

void LevelEditor::deleteSelectedDoor(Room &room)
{
	if (selectedDoorIndex < 0 || selectedDoorIndex >= static_cast<int>(room.doors.size()))
	{
		return;
	}

	queuePendingDoorDelete(pendingDoorRenames, pendingDoorDeletes, room.doors[selectedDoorIndex].name);
	room.doors.erase(room.doors.begin() + selectedDoorIndex);
	clearDoorSelection();
	pendingDeleteDoorName.clear();
	doorActionHasError = false;
	doorActionMessage = "Deleted selected door";
	levelDirty = true;
	pushUndoSnapshot(room);
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

void LevelEditor::createSpawnRegion(Room &room, glm::ivec2 a, glm::ivec2 b)
{
	SpawnRegionRect rect = makeSpawnRegionRect(a, b);
	clampSpawnRegionRectToRoom(rect, room);

	SpawnRegion spawnRegion = {};
	spawnRegion.spawnPosition = rect.position;
	spawnRegion.rects.push_back(rect);
	clampSpawnRegionToRoom(spawnRegion, room);

	room.spawnRegions.push_back(spawnRegion);
	selectedSpawnRegionIndex = static_cast<int>(room.spawnRegions.size()) - 1;
	selectedSpawnRegionRectIndex = 0;
	spawnRegionActionHasError = false;
	spawnRegionActionMessage = "Created spawn region";
	levelDirty = true;
}

void LevelEditor::addRectToSelectedSpawnRegion(Room &room, glm::ivec2 a, glm::ivec2 b)
{
	if (selectedSpawnRegionIndex < 0 ||
		selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
	{
		return;
	}

	SpawnRegionRect rect = makeSpawnRegionRect(a, b);
	clampSpawnRegionRectToRoom(rect, room);

	SpawnRegion &spawnRegion = room.spawnRegions[selectedSpawnRegionIndex];
	spawnRegion.rects.push_back(rect);
	selectedSpawnRegionRectIndex = static_cast<int>(spawnRegion.rects.size()) - 1;
	spawnRegionActionHasError = false;
	spawnRegionActionMessage = "Added rectangle to spawn region";
	levelDirty = true;
}

void LevelEditor::moveSelectedSpawnRegion(Room &room, glm::ivec2 position)
{
	if (selectedSpawnRegionIndex < 0 ||
		selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
	{
		return;
	}

	SpawnRegion &spawnRegion = room.spawnRegions[selectedSpawnRegionIndex];
	if (selectedSpawnRegionRectIndex < 0 ||
		selectedSpawnRegionRectIndex >= static_cast<int>(spawnRegion.rects.size()))
	{
		return;
	}

	// Multi-rect spawn regions share one spawn point, but dragging should only
	// reposition the rectangle that was actually selected.
	SpawnRegionRect &selectedRect = spawnRegion.rects[selectedSpawnRegionRectIndex];
	glm::ivec2 clampedPosition = {
		std::clamp(position.x, 0, std::max(room.size.x - selectedRect.size.x, 0)),
		std::clamp(position.y, 0, std::max(room.size.y - selectedRect.size.y, 0))
	};

	if (selectedRect.position == clampedPosition)
	{
		return;
	}

	selectedRect.position = clampedPosition;
	spawnRegionActionHasError = false;
	spawnRegionActionMessage = "Moved selected spawn region rectangle";
	levelDirty = true;
}

void LevelEditor::resizeSelectedSpawnRegionRect(Room &room, glm::ivec2 size)
{
	if (selectedSpawnRegionIndex < 0 ||
		selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
	{
		return;
	}

	SpawnRegion &spawnRegion = room.spawnRegions[selectedSpawnRegionIndex];
	if (selectedSpawnRegionRectIndex < 0 ||
		selectedSpawnRegionRectIndex >= static_cast<int>(spawnRegion.rects.size()))
	{
		return;
	}

	SpawnRegionRect &rect = spawnRegion.rects[selectedSpawnRegionRectIndex];
	glm::ivec2 clampedSize = {
		std::clamp(size.x, 1, std::max(room.size.x - rect.position.x, 1)),
		std::clamp(size.y, 1, std::max(room.size.y - rect.position.y, 1))
	};

	if (rect.size == clampedSize)
	{
		return;
	}

	rect.size = clampedSize;
	levelDirty = true;
}

void LevelEditor::moveSelectedSpawnRegionSpawn(Room &room, glm::ivec2 position)
{
	if (selectedSpawnRegionIndex < 0 ||
		selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
	{
		return;
	}

	glm::ivec2 clampedPosition = {
		std::clamp(position.x, 0, std::max(room.size.x - 1, 0)),
		std::clamp(position.y, 0, std::max(room.size.y - 1, 0))
	};

	SpawnRegion &spawnRegion = room.spawnRegions[selectedSpawnRegionIndex];
	if (spawnRegion.spawnPosition == clampedPosition)
	{
		return;
	}

	spawnRegion.spawnPosition = clampedPosition;
	spawnRegionActionHasError = false;
	spawnRegionActionMessage = "Moved spawn region point";
	levelDirty = true;
}

void LevelEditor::removeSelectedSpawnRegionRect(Room &room)
{
	if (selectedSpawnRegionIndex < 0 ||
		selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
	{
		return;
	}

	SpawnRegion &spawnRegion = room.spawnRegions[selectedSpawnRegionIndex];
	if (selectedSpawnRegionRectIndex < 0 ||
		selectedSpawnRegionRectIndex >= static_cast<int>(spawnRegion.rects.size()))
	{
		return;
	}

	if (spawnRegion.rects.size() <= 1)
	{
		spawnRegionActionHasError = true;
		spawnRegionActionMessage = "Use delete to remove the last rectangle";
		return;
	}

	spawnRegion.rects.erase(spawnRegion.rects.begin() + selectedSpawnRegionRectIndex);
	selectedSpawnRegionRectIndex = std::clamp(
		selectedSpawnRegionRectIndex,
		0,
		static_cast<int>(spawnRegion.rects.size()) - 1);
	spawnRegionActionHasError = false;
	spawnRegionActionMessage = "Removed rectangle from spawn region";
	levelDirty = true;
	pushUndoSnapshot(room);
}

void LevelEditor::requestDeleteSelectedSpawnRegion(Room &room)
{
	if (selectedSpawnRegionIndex < 0 ||
		selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
	{
		return;
	}

	pendingDeleteSpawnRegionIndex = selectedSpawnRegionIndex;
	pendingOpenDeleteSpawnRegionPopup = true;
}

void LevelEditor::deleteSelectedSpawnRegion(Room &room)
{
	if (selectedSpawnRegionIndex < 0 ||
		selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()))
	{
		return;
	}

	room.spawnRegions.erase(room.spawnRegions.begin() + selectedSpawnRegionIndex);
	clearSpawnRegionSelection();
	spawnRegionActionHasError = false;
	spawnRegionActionMessage = "Deleted selected spawn region";
	levelDirty = true;
	pushUndoSnapshot(room);
}

void LevelEditor::createZipline(Room &room, glm::ivec2 firstPoint, glm::ivec2 secondPoint)
{
	Zipline zipline = {};
	zipline.points[0] = firstPoint;
	zipline.points[1] = secondPoint;
	clampZiplineToRoom(zipline, room);

	room.ziplines.push_back(zipline);
	selectedZiplineIndex = static_cast<int>(room.ziplines.size()) - 1;
	selectedZiplinePoint = 1;
	doorActionHasError = false;
	doorActionMessage = "Created zipline";
	levelDirty = true;
}

void LevelEditor::moveSelectedZiplinePoint(Room &room, glm::ivec2 position)
{
	if (selectedZiplineIndex < 0 || selectedZiplineIndex >= static_cast<int>(room.ziplines.size()) ||
		selectedZiplinePoint < 0 || selectedZiplinePoint > 1)
	{
		return;
	}

	Zipline &zipline = room.ziplines[selectedZiplineIndex];
	glm::ivec2 clampedPosition = {
		std::clamp(position.x, 0, std::max(room.size.x - 1, 0)),
		std::clamp(position.y, 0, std::max(room.size.y - 1, 0))
	};

	if (zipline.points[selectedZiplinePoint] == clampedPosition)
	{
		return;
	}

	zipline.points[selectedZiplinePoint] = clampedPosition;
	doorActionHasError = false;
	doorActionMessage = "Moved zipline point";
	levelDirty = true;
}

void LevelEditor::requestDeleteSelectedZipline(Room &room)
{
	if (selectedZiplineIndex < 0 || selectedZiplineIndex >= static_cast<int>(room.ziplines.size()))
	{
		return;
	}

	pendingDeleteZiplineIndex = selectedZiplineIndex;
	pendingOpenDeleteZiplinePopup = true;
}

void LevelEditor::deleteSelectedZipline(Room &room, int ziplineIndex)
{
	if (ziplineIndex < 0 || ziplineIndex >= static_cast<int>(room.ziplines.size()))
	{
		return;
	}

	room.ziplines.erase(room.ziplines.begin() + ziplineIndex);
	clearZiplineSelection();
	pendingDeleteZiplineIndex = -1;
	pendingOpenDeleteZiplinePopup = false;
	doorActionHasError = false;
	doorActionMessage = "Deleted selected zipline";
	levelDirty = true;
	pushUndoSnapshot(room);
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
	pushUndoSnapshot(room);
}

void LevelEditor::clampDoorToRoom(Door &door, Room const &room)
{
	door.size.x = std::max(door.size.x, 1);
	door.size.y = std::max(door.size.y, 1);
	door.playerSpawnPosition.x = std::clamp(door.playerSpawnPosition.x, 0, std::max(room.size.x - 1, 0));
	door.playerSpawnPosition.y = std::clamp(door.playerSpawnPosition.y, 0, std::max(room.size.y - 1, 0));
}

void LevelEditor::clampSpawnRegionRectToRoom(SpawnRegionRect &rect, Room const &room)
{
	rect.position.x = std::clamp(rect.position.x, 0, std::max(room.size.x - 1, 0));
	rect.position.y = std::clamp(rect.position.y, 0, std::max(room.size.y - 1, 0));
	rect.size.x = std::clamp(rect.size.x, 1, std::max(room.size.x - rect.position.x, 1));
	rect.size.y = std::clamp(rect.size.y, 1, std::max(room.size.y - rect.position.y, 1));
}

void LevelEditor::clampSpawnRegionToRoom(SpawnRegion &spawnRegion, Room const &room)
{
	spawnRegion.spawnPosition.x = std::clamp(spawnRegion.spawnPosition.x, 0, std::max(room.size.x - 1, 0));
	spawnRegion.spawnPosition.y = std::clamp(spawnRegion.spawnPosition.y, 0, std::max(room.size.y - 1, 0));

	if (spawnRegion.rects.empty())
	{
		SpawnRegionRect rect = {};
		rect.position = spawnRegion.spawnPosition;
		rect.size = {1, 1};
		spawnRegion.rects.push_back(rect);
	}

	for (SpawnRegionRect &rect : spawnRegion.rects)
	{
		clampSpawnRegionRectToRoom(rect, room);
	}
}

void LevelEditor::clampZiplineToRoom(Zipline &zipline, Room const &room)
{
	for (glm::ivec2 &point : zipline.points)
	{
		point.x = std::clamp(point.x, 0, std::max(room.size.x - 1, 0));
		point.y = std::clamp(point.y, 0, std::max(room.size.y - 1, 0));
	}
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
	const gl2d::Color4f solidBlockColor = {0.32f, 0.40f, 0.50f, 1.f};
	const gl2d::Color4f spikeBlockColor = {0.86f, 0.18f, 0.20f, 1.f};

	renderer.renderRectangle({0.f, 0.f, static_cast<float>(room.size.x), static_cast<float>(room.size.y)}, roomBackground);

	for (int y = 0; y < room.size.y; y++)
	{
		for (int x = 0; x < room.size.x; x++)
		{
			Block const &block = room.getBlockUnsafe(x, y);
			if (block.isEmpty())
			{
				continue;
			}

			renderer.renderRectangle(
				{static_cast<float>(x), static_cast<float>(y), 1.f, 1.f},
				block.isSolid() ? solidBlockColor : spikeBlockColor);
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

void LevelEditor::drawSpawnRegions(Room &room, gl2d::Renderer2D &renderer)
{
	// Spawn regions stay lightly visible so they can be laid out against the room
	// without fighting the stronger door and zipline overlays.
	for (int regionIndex = 0; regionIndex < static_cast<int>(room.spawnRegions.size()); regionIndex++)
	{
		SpawnRegion const &spawnRegion = room.spawnRegions[regionIndex];
		bool selected = regionIndex == selectedSpawnRegionIndex;
		gl2d::Color4f fillColor = selected ? kSelectedSpawnRegionFillColor : kSpawnRegionFillColor;
		gl2d::Color4f outlineColor = selected ? kSelectedSpawnRegionOutlineColor : kSpawnRegionOutlineColor;
		gl2d::Color4f pointFillColor = selected ? gl2d::Color4f{0.58f, 0.84f, 1.0f, 0.30f} : kSpawnRegionPointFillColor;
		gl2d::Color4f pointOutlineColor = selected ? gl2d::Color4f{0.72f, 0.92f, 1.0f, 1.0f} : kSpawnRegionPointOutlineColor;
		glm::vec2 spawnCenter = glm::vec2(spawnRegion.spawnPosition) + glm::vec2(0.5f, 0.5f);

		for (int rectIndex = 0; rectIndex < static_cast<int>(spawnRegion.rects.size()); rectIndex++)
		{
			SpawnRegionRect const &rect = spawnRegion.rects[rectIndex];
			bool selectedRect = selected && rectIndex == selectedSpawnRegionRectIndex;
			renderer.renderRectangle(rect.getRectF(), fillColor);
			renderer.renderRectangleOutline(rect.getRectF(), outlineColor, selectedRect ? 0.11f : 0.07f);
			renderer.renderLine(
				spawnCenter,
				glm::vec2(rect.position) + glm::vec2(rect.size) * 0.5f,
				outlineColor,
				selectedRect ? 0.08f : 0.05f);

			if (selectedRect && tool == spawnRegionTool)
			{
				renderer.renderRectangle(
					{
						rect.position.x + rect.size.x - 0.46f,
						rect.position.y + rect.size.y - 0.46f,
						0.34f,
						0.34f
					},
					kSelectedSpawnRegionOutlineColor);
			}
		}

		renderer.renderRectangle(spawnRegion.getSpawnRectF(), pointFillColor);
		renderer.renderRectangleOutline(
			spawnRegion.getSpawnRectF(),
			pointOutlineColor,
			selected && spawnRegionSpawnDragActive ? 0.12f : 0.08f);
	}

	if (!spawnRegionCreateDragActive)
	{
		return;
	}

	SpawnRegionRect previewRect = makeSpawnRegionRect(spawnRegionCreateStart, spawnRegionCreateEnd);
	renderer.renderRectangle(previewRect.getRectF(), {0.44f, 0.72f, 1.0f, 0.10f});
	renderer.renderRectangleOutline(previewRect.getRectF(), {0.72f, 0.90f, 1.0f, 0.92f}, 0.08f);
}

void LevelEditor::drawZiplines(Room &room, gl2d::Renderer2D &renderer)
{
	// Ziplines stay visible while editing so their travel path is easy to line up
	// against the room geometry and doors.
	for (int i = 0; i < static_cast<int>(room.ziplines.size()); i++)
	{
		Zipline const &zipline = room.ziplines[i];
		bool selected = i == selectedZiplineIndex;
		gl2d::Color4f lineColor = selected ? kSelectedZiplineLineColor : kZiplineLineColor;
		gl2d::Color4f pointFillColor = selected ? kSelectedZiplinePointFillColor : kZiplinePointFillColor;
		gl2d::Color4f pointOutlineColor = selected ? kSelectedZiplinePointOutlineColor : kZiplinePointOutlineColor;

		renderer.renderLine(
			zipline.getPointCenter(0),
			zipline.getPointCenter(1),
			lineColor,
			selected ? kZiplineLineWidth * 1.35f : kZiplineLineWidth);

		for (int pointIndex = 0; pointIndex < 2; pointIndex++)
		{
			glm::vec2 pointMin = glm::vec2(zipline.points[pointIndex]) + glm::vec2(0.5f - kZiplinePointSize * 0.5f);
			renderer.renderRectangle(
				{pointMin.x, pointMin.y, kZiplinePointSize, kZiplinePointSize},
				pointFillColor);
			renderer.renderRectangleOutline(
				{pointMin.x, pointMin.y, kZiplinePointSize, kZiplinePointSize},
				pointOutlineColor,
				selected && pointIndex == selectedZiplinePoint ? 0.10f : 0.07f);
		}
	}

	if (!ziplineCreateDragActive)
	{
		return;
	}

	Zipline preview = {};
	preview.points[0] = ziplineCreateStart;
	preview.points[1] = ziplineCreateEnd;
	renderer.renderLine(
		preview.getPointCenter(0),
		preview.getPointCenter(1),
		{0.92f, 0.94f, 0.98f, 0.95f},
		kZiplineLineWidth * 1.35f);

	for (int pointIndex = 0; pointIndex < 2; pointIndex++)
	{
		glm::vec2 pointMin = glm::vec2(preview.points[pointIndex]) + glm::vec2(0.5f - kZiplinePointSize * 0.5f);
		renderer.renderRectangle(
			{pointMin.x, pointMin.y, kZiplinePointSize, kZiplinePointSize},
			{1.0f, 0.92f, 0.34f, 0.30f});
		renderer.renderRectangleOutline(
			{pointMin.x, pointMin.y, kZiplinePointSize, kZiplinePointSize},
			{1.0f, 0.98f, 0.50f, 1.f},
			0.08f);
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
	if (tool == doorTool)
	{
		hoverColor = doorSpawnDragActive
			? gl2d::Color4f{1.0f, 0.94f, 0.28f, 0.98f}
			: gl2d::Color4f{1.0f, 0.74f, 0.22f, 0.95f};
	}
	if (tool == moveTool)
	{
		hoverColor = {0.30f, 0.88f, 1.0f, 0.95f};
	}
	if (tool == ziplineTool)
	{
		hoverColor = {1.0f, 0.88f, 0.34f, 0.95f};
	}

	renderer.renderRectangleOutline(
		{static_cast<float>(hoveredTile.x), static_cast<float>(hoveredTile.y), 1.f, 1.f},
		hoverColor,
		0.08f);
}

void LevelEditor::drawMoveSelection(gl2d::Renderer2D &renderer)
{
	if (!moveSelection.active)
	{
		return;
	}

	glm::vec4 sourceRect = {
		static_cast<float>(moveSelection.sourceStart.x),
		static_cast<float>(moveSelection.sourceStart.y),
		static_cast<float>(moveSelection.size.x),
		static_cast<float>(moveSelection.size.y)
	};

	renderer.renderRectangleOutline(sourceRect, {0.28f, 0.82f, 1.0f, 0.95f}, 0.10f);

	for (int y = 0; y < moveSelection.size.y; y++)
	{
		for (int x = 0; x < moveSelection.size.x; x++)
		{
			BlockType type = moveSelection.blockTypes[x + y * moveSelection.size.x];
			if (type == emptyBlock)
			{
				continue;
			}

			renderer.renderRectangle(
				{
					static_cast<float>(moveSelection.sourceStart.x + x),
					static_cast<float>(moveSelection.sourceStart.y + y),
					1.f,
					1.f
				},
				type == spikeBlock
					? gl2d::Color4f{1.0f, 0.34f, 0.30f, 0.16f}
					: gl2d::Color4f{0.28f, 0.82f, 1.0f, 0.14f});
		}
	}

	if (!moveSelection.previewActive)
	{
		return;
	}

	glm::vec4 previewRect = {
		static_cast<float>(moveSelection.previewPosition.x),
		static_cast<float>(moveSelection.previewPosition.y),
		static_cast<float>(moveSelection.size.x),
		static_cast<float>(moveSelection.size.y)
	};
	renderer.renderRectangleOutline(previewRect, {0.22f, 1.0f, 0.72f, 0.98f}, 0.10f);

	for (int y = 0; y < moveSelection.size.y; y++)
	{
		for (int x = 0; x < moveSelection.size.x; x++)
		{
			BlockType type = moveSelection.blockTypes[x + y * moveSelection.size.x];
			if (type == emptyBlock)
			{
				continue;
			}

			renderer.renderRectangle(
				{
					static_cast<float>(moveSelection.previewPosition.x + x),
					static_cast<float>(moveSelection.previewPosition.y + y),
					1.f,
					1.f
				},
				type == spikeBlock
					? gl2d::Color4f{1.0f, 0.36f, 0.32f, 0.26f}
					: gl2d::Color4f{0.22f, 1.0f, 0.72f, 0.24f});
		}
	}
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
	if (tool == spikeTool)
	{
		previewColor = rectDragPlacesSolid
			? gl2d::Color4f{1.0f, 0.30f, 0.26f, 0.95f}
			: gl2d::Color4f{1.0f, 0.28f, 0.22f, 0.9f};
	}
	if (tool == moveTool)
	{
		previewColor = {0.28f, 0.82f, 1.0f, 0.95f};
	}

	renderer.renderRectangleOutline(rect, previewColor, 0.10f);
}

void LevelEditor::drawMeasureText(gl2d::Renderer2D &renderer)
{
	if ((tool != measureTool && tool != rectTool && tool != moveTool && tool != spikeTool) || !rectDragActive || !measureFont.texture.isValid())
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
	else if (tool == spikeTool)
	{
		textColor = rectDragPlacesSolid
			? gl2d::Color4f{1.0f, 0.42f, 0.34f, 1.f}
			: gl2d::Color4f{1.0f, 0.38f, 0.30f, 1.f};
	}
	else if (tool == moveTool)
	{
		textColor = {0.28f, 0.82f, 1.0f, 1.f};
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
		bool hasSelectedSpawnRegion = hasLoadedLevel &&
			selectedSpawnRegionIndex >= 0 &&
			selectedSpawnRegionIndex < static_cast<int>(room.spawnRegions.size()) &&
			selectedSpawnRegionRectIndex >= 0 &&
			selectedSpawnRegionRectIndex < static_cast<int>(room.spawnRegions[selectedSpawnRegionIndex].rects.size());
		bool hasSelectedZipline = hasLoadedLevel &&
			selectedZiplineIndex >= 0 &&
			selectedZiplineIndex < static_cast<int>(room.ziplines.size());

		ImGui::TextUnformatted("F10 hides / shows ImGui");
		ImGui::TextUnformatted("F6 Game, F7 Level Editor, F8 World Editor");
		ImGui::TextUnformatted("` toggles back to gameplay");
		ImGui::TextUnformatted("WASD / Arrows move camera, Q/E zoom");
		ImGui::TextUnformatted("Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo");
		ImGui::TextUnformatted("Ctrl+S saves the current level");
		ImGui::TextUnformatted("Tab returns to the world editor");
		ImGui::TextUnformatted("Escape cancels active move, rect, spike, measure, door, spawn region, or zipline drag input");
		if (!hasLoadedLevel)
		{
			ImGui::TextColored({1.f, 0.88f, 0.35f, 1.f}, "Load or create a level file before editing.");
		}

		ImGui::Separator();
		if (!hasLoadedLevel) { ImGui::BeginDisabled(); }
		ImGui::TextUnformatted("Tools");
		if (ImGui::RadioButton("None (1)", tool == noneTool)) { tool = noneTool; rectDragActive = false; clearMoveSelection(); doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; spawnRegionDragActive = false; spawnRegionResizeActive = false; spawnRegionSpawnDragActive = false; spawnRegionCreateDragActive = false; spawnRegionAddRectArmed = false; ziplineCreateDragActive = false; ziplinePointDragActive = false; }
		if (ImGui::RadioButton("Brush (2)", tool == brushTool)) { tool = brushTool; rectDragActive = false; clearMoveSelection(); doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; spawnRegionDragActive = false; spawnRegionResizeActive = false; spawnRegionSpawnDragActive = false; spawnRegionCreateDragActive = false; spawnRegionAddRectArmed = false; ziplineCreateDragActive = false; ziplinePointDragActive = false; }
		if (ImGui::RadioButton("Rect (3)", tool == rectTool)) { tool = rectTool; rectDragActive = false; clearMoveSelection(); doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; spawnRegionDragActive = false; spawnRegionResizeActive = false; spawnRegionSpawnDragActive = false; spawnRegionCreateDragActive = false; spawnRegionAddRectArmed = false; ziplineCreateDragActive = false; ziplinePointDragActive = false; }
		if (ImGui::RadioButton("Measure (4)", tool == measureTool)) { tool = measureTool; rectDragActive = false; clearMoveSelection(); doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; spawnRegionDragActive = false; spawnRegionResizeActive = false; spawnRegionSpawnDragActive = false; spawnRegionCreateDragActive = false; spawnRegionAddRectArmed = false; ziplineCreateDragActive = false; ziplinePointDragActive = false; }
		if (ImGui::RadioButton("Door (5)", tool == doorTool)) { tool = doorTool; rectDragActive = false; clearMoveSelection(); doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; spawnRegionDragActive = false; spawnRegionResizeActive = false; spawnRegionSpawnDragActive = false; spawnRegionCreateDragActive = false; spawnRegionAddRectArmed = false; ziplineCreateDragActive = false; ziplinePointDragActive = false; }
		if (ImGui::RadioButton("Move (6)", tool == moveTool)) { tool = moveTool; rectDragActive = false; clearMoveSelection(); doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; spawnRegionDragActive = false; spawnRegionResizeActive = false; spawnRegionSpawnDragActive = false; spawnRegionCreateDragActive = false; spawnRegionAddRectArmed = false; ziplineCreateDragActive = false; ziplinePointDragActive = false; }
		if (ImGui::RadioButton("Zipline (7)", tool == ziplineTool)) { tool = ziplineTool; rectDragActive = false; clearMoveSelection(); doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; spawnRegionDragActive = false; spawnRegionResizeActive = false; spawnRegionSpawnDragActive = false; spawnRegionCreateDragActive = false; spawnRegionAddRectArmed = false; ziplineCreateDragActive = false; ziplinePointDragActive = false; }
		if (ImGui::RadioButton("Spawn Region (8)", tool == spawnRegionTool)) { tool = spawnRegionTool; rectDragActive = false; clearMoveSelection(); doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; spawnRegionDragActive = false; spawnRegionResizeActive = false; spawnRegionSpawnDragActive = false; spawnRegionCreateDragActive = false; spawnRegionAddRectArmed = false; ziplineCreateDragActive = false; ziplinePointDragActive = false; }
		if (ImGui::RadioButton("Spike (9)", tool == spikeTool)) { tool = spikeTool; rectDragActive = false; clearMoveSelection(); doorDragActive = false; doorResizeActive = false; doorSpawnDragActive = false; spawnRegionDragActive = false; spawnRegionResizeActive = false; spawnRegionSpawnDragActive = false; spawnRegionCreateDragActive = false; spawnRegionAddRectArmed = false; ziplineCreateDragActive = false; ziplinePointDragActive = false; }

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
		else if (tool == spikeTool)
		{
			ImGui::TextUnformatted("Drag LMB to place spikes, drag RMB to clear");
		}
		else if (tool == doorTool)
		{
			ImGui::TextUnformatted("Ctrl+LMB empty tile adds, drag door moves, drag yellow spawn, drag corner resizes");
		}
		else if (tool == spawnRegionTool)
		{
			ImGui::TextUnformatted("Ctrl+drag adds a region, drag selected blue rect moves only it, drag blue point moves spawn, drag corner resizes");
			if (spawnRegionAddRectArmed)
			{
				ImGui::TextUnformatted("Drag in the room to add another rectangle to the selected region");
			}
		}
		else if (tool == moveTool)
		{
			ImGui::TextUnformatted("Drag to select blocks, Ctrl+drag selection to move, Enter places, Escape cancels");
			if (moveSelection.active)
			{
				ImGui::Text("Selection: %d x %d", moveSelection.size.x, moveSelection.size.y);
			}
		}
		else if (tool == ziplineTool)
		{
			ImGui::TextUnformatted("Ctrl+drag adds a zipline, drag a point to move it, Del deletes the selected zipline");
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
				requestDeleteSelectedDoor(room);
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

		ImGui::Separator();
		ImGui::TextUnformatted("Spawn Regions");
		ImGui::Text("Count: %d", static_cast<int>(room.spawnRegions.size()));
		if (hasSelectedSpawnRegion)
		{
			SpawnRegion const &selectedSpawnRegion = room.spawnRegions[selectedSpawnRegionIndex];
			SpawnRegionRect const &selectedRect = selectedSpawnRegion.rects[selectedSpawnRegionRectIndex];
			ImGui::Text("Selected Region: %d", selectedSpawnRegionIndex + 1);
			ImGui::Text("Selected Rect: %d / %d",
				selectedSpawnRegionRectIndex + 1,
				static_cast<int>(selectedSpawnRegion.rects.size()));

			int rectX = selectedRect.position.x;
			int rectY = selectedRect.position.y;
			int rectW = selectedRect.size.x;
			int rectH = selectedRect.size.y;
			bool movedRect = false;
			bool resizedRect = false;

			if (ImGui::InputInt("Region Rect X", &rectX)) { movedRect = true; }
			if (ImGui::InputInt("Region Rect Y", &rectY)) { movedRect = true; }
			if (ImGui::InputInt("Region Rect Width", &rectW)) { resizedRect = true; }
			if (ImGui::InputInt("Region Rect Height", &rectH)) { resizedRect = true; }

			if (movedRect)
			{
				moveSelectedSpawnRegion(room, {rectX, rectY});
			}
			if (resizedRect)
			{
				resizeSelectedSpawnRegionRect(room, {rectW, rectH});
			}

			ImGui::Text("Spawn Point: %d, %d",
				selectedSpawnRegion.spawnPosition.x,
				selectedSpawnRegion.spawnPosition.y);
			ImGui::TextUnformatted("Drag the blue spawn marker to move it.");

			if (ImGui::Button("Add Rectangle"))
			{
				spawnRegionAddRectArmed = true;
				spawnRegionActionHasError = false;
				spawnRegionActionMessage = "Drag in the room to add a rectangle";
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove Selected Rect"))
			{
				removeSelectedSpawnRegionRect(room);
			}
			if (ImGui::Button("Delete Selected Region"))
			{
				requestDeleteSelectedSpawnRegion(room);
			}
		}
		else
		{
			ImGui::TextUnformatted("Selected: none");
			ImGui::TextUnformatted("Pick Spawn Region (8) and Ctrl+drag in the room to add one.");
		}

		if (!spawnRegionActionMessage.empty())
		{
			ImVec4 spawnRegionColor = spawnRegionActionHasError
				? ImVec4(1.f, 0.45f, 0.35f, 1.f)
				: ImVec4(0.58f, 0.82f, 1.f, 1.f);
			ImGui::TextColored(spawnRegionColor, "%s", spawnRegionActionMessage.c_str());
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Ziplines");
		ImGui::Text("Count: %d", static_cast<int>(room.ziplines.size()));
		if (hasSelectedZipline)
		{
			Zipline const &selectedZipline = room.ziplines[selectedZiplineIndex];
			ImGui::Text("Point A: %d, %d", selectedZipline.points[0].x, selectedZipline.points[0].y);
			ImGui::Text("Point B: %d, %d", selectedZipline.points[1].x, selectedZipline.points[1].y);
			ImGui::Text("Selected Point: %c", selectedZiplinePoint == 0 ? 'A' : 'B');
			if (ImGui::Button("Delete Selected Zipline"))
			{
				requestDeleteSelectedZipline(room);
			}
		}
		else
		{
			ImGui::TextUnformatted("Selected: none");
			ImGui::TextUnformatted("Pick Zipline (7), Ctrl+drag to add, then drag a point to edit it.");
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
			ImGui::Text("Block Type: %d", static_cast<int>(room.getBlockUnsafe(hoveredTile.x, hoveredTile.y).type));
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

		if (pendingOpenDeleteDoorPopup)
		{
			ImGui::OpenPopup("Delete Door");
			pendingOpenDeleteDoorPopup = false;
		}

		if (ImGui::BeginPopupModal("Delete Door", 0, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Delete door \"%s\"?", pendingDeleteDoorName.c_str());
			ImGui::TextUnformatted("This also removes saved world door links for it next time you save the level.");

			if (ImGui::Button("Delete", {120.f, 0.f}))
			{
				selectedDoorIndex = -1;
				for (int i = 0; i < static_cast<int>(room.doors.size()); i++)
				{
					if (room.doors[i].name == pendingDeleteDoorName)
					{
						selectedDoorIndex = i;
						break;
					}
				}

				if (selectedDoorIndex >= 0)
				{
					deleteSelectedDoor(room);
				}
				else
				{
					pendingDeleteDoorName.clear();
					doorActionHasError = true;
					doorActionMessage = "That door no longer exists";
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", {120.f, 0.f}))
			{
				pendingDeleteDoorName.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (pendingOpenDeleteZiplinePopup)
		{
			ImGui::OpenPopup("Delete Zipline");
			pendingOpenDeleteZiplinePopup = false;
		}

		if (pendingOpenDeleteSpawnRegionPopup)
		{
			ImGui::OpenPopup("Delete Spawn Region");
			pendingOpenDeleteSpawnRegionPopup = false;
		}

		if (ImGui::BeginPopupModal("Delete Spawn Region", 0, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Delete the selected spawn region?");

			if (ImGui::Button("Delete", {120.f, 0.f}))
			{
				selectedSpawnRegionIndex = pendingDeleteSpawnRegionIndex;
				if (selectedSpawnRegionIndex >= 0 &&
					selectedSpawnRegionIndex < static_cast<int>(room.spawnRegions.size()))
				{
					deleteSelectedSpawnRegion(room);
				}
				else
				{
					clearSpawnRegionSelection();
					spawnRegionActionHasError = true;
					spawnRegionActionMessage = "That spawn region no longer exists";
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", {120.f, 0.f}))
			{
				pendingDeleteSpawnRegionIndex = -1;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal("Delete Zipline", 0, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Delete the selected zipline?");

			if (ImGui::Button("Delete", {120.f, 0.f}))
			{
				if (pendingDeleteZiplineIndex >= 0 &&
					pendingDeleteZiplineIndex < static_cast<int>(room.ziplines.size()))
				{
					deleteSelectedZipline(room, pendingDeleteZiplineIndex);
				}
				else
				{
					clearZiplineSelection();
					pendingDeleteZiplineIndex = -1;
					doorActionHasError = true;
					doorActionMessage = "That zipline no longer exists";
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", {120.f, 0.f}))
			{
				pendingDeleteZiplineIndex = -1;
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
		for (auto const &deletedDoorName : pendingDoorDeletes)
		{
			WorldIoResult worldResult = deleteDoorReferencesInWorld(
				currentLevelName.c_str(),
				deletedDoorName.c_str());

			if (!worldResult.success)
			{
				fileActionHasError = true;
				fileActionMessage = "Saved level, but couldn't remove world door links: " + worldResult.message;
				levelDirty = false;
				return;
			}
		}

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

		pendingDoorDeletes.clear();
		pendingDoorRenames.clear();
		levelDirty = false;
		pushUndoSnapshot(room);
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
		clearMoveSelection();
		clearDoorSelection();
		clearSpawnRegionSelection();
		clearZiplineSelection();
		doorActionMessage.clear();
		doorActionHasError = false;
		spawnRegionActionMessage.clear();
		spawnRegionActionHasError = false;
		pendingDeleteDoorName.clear();
		pendingOpenDeleteDoorPopup = false;
		pendingDeleteSpawnRegionIndex = -1;
		pendingOpenDeleteSpawnRegionPopup = false;
		pendingDeleteZiplineIndex = -1;
		pendingOpenDeleteZiplinePopup = false;
		pendingDoorDeletes.clear();
		pendingDoorRenames.clear();
		levelDirty = false;
		copyStringToBuffer(renameName, result.levelName);
		focusRoom(room, renderer);
		resetUndoHistory(room);
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
	resetUndoHistory(room);
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
		clearMoveSelection();
		clearDoorSelection();
		clearSpawnRegionSelection();
		clearZiplineSelection();
		doorActionMessage.clear();
		doorActionHasError = false;
		spawnRegionActionMessage.clear();
		spawnRegionActionHasError = false;
		pendingDeleteDoorName.clear();
		pendingOpenDeleteDoorPopup = false;
		pendingDeleteSpawnRegionIndex = -1;
		pendingOpenDeleteSpawnRegionPopup = false;
		pendingDeleteZiplineIndex = -1;
		pendingOpenDeleteZiplinePopup = false;
		pendingDoorDeletes.clear();
		pendingDoorRenames.clear();
		levelDirty = false;
		newLevelName[0] = 0;
		copyStringToBuffer(renameName, result.levelName);
		focusRoom(room, renderer);
		resetUndoHistory(room);
	}
}

LevelEditor::UndoSnapshot LevelEditor::captureUndoSnapshot(Room &room) const
{
	UndoSnapshot snapshot = {};
	snapshot.room = room;
	snapshot.currentLevelName = currentLevelName;
	snapshot.selectedLevelName = selectedLevelName;
	snapshot.renameName = renameName;
	snapshot.selectedDoorIndex = selectedDoorIndex;
	snapshot.selectedDoorName = selectedDoorName;
	snapshot.selectedSpawnRegionIndex = selectedSpawnRegionIndex;
	snapshot.selectedSpawnRegionRectIndex = selectedSpawnRegionRectIndex;
	snapshot.selectedZiplineIndex = selectedZiplineIndex;
	snapshot.selectedZiplinePoint = selectedZiplinePoint;
	snapshot.pendingRoomSize = pendingRoomSize;
	snapshot.newLevelSize = newLevelSize;
	snapshot.levelDirty = levelDirty;
	snapshot.pendingDoorRenames = pendingDoorRenames;
	snapshot.pendingDoorDeletes = pendingDoorDeletes;
	return snapshot;
}

bool LevelEditor::undoSnapshotsMatch(UndoSnapshot const &a, UndoSnapshot const &b) const
{
	auto roomsMatch = [](Room const &lhs, Room const &rhs)
	{
		if (lhs.size != rhs.size || lhs.blocks.size() != rhs.blocks.size() ||
			lhs.doors.size() != rhs.doors.size() ||
			lhs.spawnRegions.size() != rhs.spawnRegions.size() ||
			lhs.ziplines.size() != rhs.ziplines.size())
		{
			return false;
		}

		for (size_t i = 0; i < lhs.blocks.size(); i++)
		{
			if (lhs.blocks[i].type != rhs.blocks[i].type)
			{
				return false;
			}
		}

		for (size_t i = 0; i < lhs.doors.size(); i++)
		{
			Door const &leftDoor = lhs.doors[i];
			Door const &rightDoor = rhs.doors[i];
			if (leftDoor.name != rightDoor.name ||
				leftDoor.position != rightDoor.position ||
				leftDoor.size != rightDoor.size ||
				leftDoor.playerSpawnPosition != rightDoor.playerSpawnPosition)
			{
				return false;
			}
		}

		for (size_t i = 0; i < lhs.spawnRegions.size(); i++)
		{
			SpawnRegion const &leftRegion = lhs.spawnRegions[i];
			SpawnRegion const &rightRegion = rhs.spawnRegions[i];
			if (leftRegion.spawnPosition != rightRegion.spawnPosition ||
				leftRegion.rects.size() != rightRegion.rects.size())
			{
				return false;
			}

			for (size_t rectIndex = 0; rectIndex < leftRegion.rects.size(); rectIndex++)
			{
				SpawnRegionRect const &leftRect = leftRegion.rects[rectIndex];
				SpawnRegionRect const &rightRect = rightRegion.rects[rectIndex];
				if (leftRect.position != rightRect.position || leftRect.size != rightRect.size)
				{
					return false;
				}
			}
		}

		for (size_t i = 0; i < lhs.ziplines.size(); i++)
		{
			Zipline const &leftZipline = lhs.ziplines[i];
			Zipline const &rightZipline = rhs.ziplines[i];
			if (leftZipline.points[0] != rightZipline.points[0] ||
				leftZipline.points[1] != rightZipline.points[1])
			{
				return false;
			}
		}

		return true;
	};

	auto renamesMatch = [](std::vector<PendingDoorRename> const &lhs, std::vector<PendingDoorRename> const &rhs)
	{
		if (lhs.size() != rhs.size())
		{
			return false;
		}

		for (size_t i = 0; i < lhs.size(); i++)
		{
			if (lhs[i].oldName != rhs[i].oldName || lhs[i].newName != rhs[i].newName)
			{
				return false;
			}
		}

		return true;
	};

	return
		roomsMatch(a.room, b.room) &&
		a.currentLevelName == b.currentLevelName &&
		a.selectedLevelName == b.selectedLevelName &&
		a.renameName == b.renameName &&
		a.selectedDoorIndex == b.selectedDoorIndex &&
		a.selectedDoorName == b.selectedDoorName &&
		a.selectedSpawnRegionIndex == b.selectedSpawnRegionIndex &&
		a.selectedSpawnRegionRectIndex == b.selectedSpawnRegionRectIndex &&
		a.selectedZiplineIndex == b.selectedZiplineIndex &&
		a.selectedZiplinePoint == b.selectedZiplinePoint &&
		a.pendingRoomSize == b.pendingRoomSize &&
		a.newLevelSize == b.newLevelSize &&
		a.levelDirty == b.levelDirty &&
		renamesMatch(a.pendingDoorRenames, b.pendingDoorRenames) &&
		a.pendingDoorDeletes == b.pendingDoorDeletes;
}

void LevelEditor::resetUndoHistory(Room &room)
{
	undoHistory.clear();
	undoHistoryIndex = -1;
	pushUndoSnapshot(room);
}

void LevelEditor::pushUndoSnapshot(Room &room)
{
	if (applyingUndoRedo)
	{
		return;
	}

	UndoSnapshot snapshot = captureUndoSnapshot(room);
	if (undoHistoryIndex >= 0 && undoHistoryIndex < static_cast<int>(undoHistory.size()) &&
		undoSnapshotsMatch(undoHistory[undoHistoryIndex], snapshot))
	{
		return;
	}

	if (undoHistoryIndex + 1 < static_cast<int>(undoHistory.size()))
	{
		undoHistory.erase(undoHistory.begin() + undoHistoryIndex + 1, undoHistory.end());
	}

	undoHistory.push_back(snapshot);
	undoHistoryIndex = static_cast<int>(undoHistory.size()) - 1;

	if (static_cast<int>(undoHistory.size()) > kMaxUndoSteps)
	{
		undoHistory.erase(undoHistory.begin());
		undoHistoryIndex--;
	}
}

bool LevelEditor::applyUndoSnapshot(UndoSnapshot const &snapshot, Room &room, gl2d::Renderer2D &renderer)
{
	if (currentLevelName != snapshot.currentLevelName)
	{
		if (currentLevelName.empty() || snapshot.currentLevelName.empty())
		{
			fileActionHasError = true;
			fileActionMessage = "Undo/redo can't switch to an unloaded level state";
			return false;
		}

		RoomIoResult renameResult = renameRoomFile(currentLevelName.c_str(), snapshot.currentLevelName.c_str());
		if (!renameResult.success)
		{
			fileActionHasError = true;
			fileActionMessage = "Undo/redo couldn't rename the level file: " + renameResult.message;
			return false;
		}

		WorldIoResult worldResult = renameLevelReferencesInWorld(currentLevelName.c_str(), snapshot.currentLevelName.c_str());
		if (!worldResult.success)
		{
			fileActionHasError = true;
			fileActionMessage = "Undo/redo couldn't update world level references: " + worldResult.message;
			return false;
		}
	}

	room = snapshot.room;
	currentLevelName = snapshot.currentLevelName;
	selectedLevelName = snapshot.selectedLevelName;
	pendingRoomSize = snapshot.pendingRoomSize;
	newLevelSize = snapshot.newLevelSize;
	levelDirty = snapshot.levelDirty;
	pendingDoorRenames = snapshot.pendingDoorRenames;
	pendingDoorDeletes = snapshot.pendingDoorDeletes;
	copyStringToBuffer(renameName, snapshot.renameName);

	rectDragActive = false;
	brushPaintActive = false;
	clearMoveSelection();
	clearDoorSelection();
	clearSpawnRegionSelection();
	clearZiplineSelection();
	selectedDoorIndex = snapshot.selectedDoorIndex;
	if (selectedDoorIndex >= 0 && selectedDoorIndex < static_cast<int>(room.doors.size()))
	{
		copyStringToBuffer(selectedDoorName, snapshot.selectedDoorName);
	}
	else
	{
		selectedDoorName[0] = 0;
	}
	selectedSpawnRegionIndex = snapshot.selectedSpawnRegionIndex;
	selectedSpawnRegionRectIndex = snapshot.selectedSpawnRegionRectIndex;
	if (selectedSpawnRegionIndex < 0 ||
		selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()) ||
		selectedSpawnRegionRectIndex < 0 ||
		selectedSpawnRegionIndex >= static_cast<int>(room.spawnRegions.size()) ||
		selectedSpawnRegionRectIndex >= static_cast<int>(room.spawnRegions[selectedSpawnRegionIndex].rects.size()))
	{
		clearSpawnRegionSelection();
	}
	selectedZiplineIndex = snapshot.selectedZiplineIndex;
	selectedZiplinePoint = snapshot.selectedZiplinePoint;
	if (selectedZiplineIndex < 0 || selectedZiplineIndex >= static_cast<int>(room.ziplines.size()))
	{
		clearZiplineSelection();
	}

	pendingDeleteDoorName.clear();
	pendingOpenDeleteDoorPopup = false;
	pendingDeleteSpawnRegionIndex = -1;
	pendingOpenDeleteSpawnRegionPopup = false;
	pendingDeleteZiplineIndex = -1;
	pendingOpenDeleteZiplinePopup = false;
	hoveredTileValid = false;
	syncPendingRoomSize(room);
	if (cameraInitialized)
	{
		clampCamera(room, renderer);
	}

	fileActionHasError = false;
	fileActionMessage = {};
	return true;
}

void LevelEditor::undo(Room &room, gl2d::Renderer2D &renderer)
{
	if (undoHistoryIndex <= 0)
	{
		return;
	}

	applyingUndoRedo = true;
	if (applyUndoSnapshot(undoHistory[undoHistoryIndex - 1], room, renderer))
	{
		undoHistoryIndex--;
		doorActionHasError = false;
		doorActionMessage = "Undo";
	}
	applyingUndoRedo = false;
}

void LevelEditor::redo(Room &room, gl2d::Renderer2D &renderer)
{
	if (undoHistoryIndex + 1 >= static_cast<int>(undoHistory.size()))
	{
		return;
	}

	applyingUndoRedo = true;
	if (applyUndoSnapshot(undoHistory[undoHistoryIndex + 1], room, renderer))
	{
		undoHistoryIndex++;
		doorActionHasError = false;
		doorActionMessage = "Redo";
	}
	applyingUndoRedo = false;
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
		clearMoveSelection();
		clearDoorSelection();
		clearZiplineSelection();
		doorActionMessage.clear();
		doorActionHasError = false;
		pendingDeleteDoorName.clear();
		pendingOpenDeleteDoorPopup = false;
		pendingDeleteZiplineIndex = -1;
		pendingOpenDeleteZiplinePopup = false;
		pendingDoorDeletes.clear();
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
			std::string oldLevelName = selectedLevelName;
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

				WorldIoResult worldResult = renameLevelReferencesInWorld(
					oldLevelName.c_str(),
					result.levelName.c_str());

				if (!worldResult.success)
				{
					fileActionHasError = true;
					fileActionMessage = "Renamed level, but couldn't update world references: " + worldResult.message;
				}

				pushUndoSnapshot(room);
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
