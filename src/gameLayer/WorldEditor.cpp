#include "WorldEditor.h"

#include "RoomIo.h"
#include "imguiTools.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>

namespace
{
	constexpr int kMaxUndoSteps = 100;
	constexpr float kMinWorldZoom = 0.1f;
	constexpr float kMaxWorldZoom = 48.f;
	constexpr float kPlacedLevelDoubleClickTime = 0.28f;
	const gl2d::Color4f kPreviewBackgroundColor = {0.08f, 0.10f, 0.13f, 1.f};
	const gl2d::Color4f kPreviewBlockColor = {0.32f, 0.40f, 0.50f, 1.f};
	const gl2d::Color4f kPreviewDoorFillColor = {1.0f, 0.56f, 0.12f, 0.16f};
	const gl2d::Color4f kPreviewDoorOutlineColor = {1.0f, 0.68f, 0.22f, 0.85f};
	const gl2d::Color4f kLinkedDoorLineColor = {0.34f, 0.88f, 1.0f, 0.90f};

	bool isDoorReferenceSet(WorldEditor::DoorReference const &doorRef)
	{
		return !doorRef.levelName.empty() && !doorRef.doorName.empty();
	}

	bool doorReferencesMatch(WorldEditor::DoorReference const &a, WorldEditor::DoorReference const &b)
	{
		return a.levelName == b.levelName && a.doorName == b.doorName;
	}

	bool rectsOverlap(glm::vec4 a, glm::vec4 b)
	{
		return
			a.x < b.x + b.z &&
			a.x + a.z > b.x &&
			a.y < b.y + b.w &&
			a.y + a.w > b.y;
	}

	std::string makeDoorReferenceKey(WorldEditor::DoorReference const &doorRef)
	{
		return doorRef.levelName + "::" + doorRef.doorName;
	}

	// Draws a small room snapshot, one world tile per preview pixel.
	void drawRoomPreview(Room const &room, gl2d::Renderer2D &renderer)
	{
		renderer.renderRectangle(
			{0.f, 0.f, static_cast<float>(room.size.x), static_cast<float>(room.size.y)},
			kPreviewBackgroundColor);

		for (int y = 0; y < room.size.y; y++)
		{
			for (int x = 0; x < room.size.x; x++)
			{
				if (!room.getBlockUnsafe(x, y).solid)
				{
					continue;
				}

				renderer.renderRectangle(
					{static_cast<float>(x), static_cast<float>(y), 1.f, 1.f},
					kPreviewBlockColor);
			}
		}

		for (Door const &door : room.doors)
		{
			renderer.renderRectangle(door.getRectF(), kPreviewDoorFillColor);
			renderer.renderRectangleOutline(door.getRectF(), kPreviewDoorOutlineColor, 0.18f);
		}
	}

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

	std::string makeDuplicateLevelName(std::string const &baseName)
	{
		std::string prefix = baseName;
		if (prefix.empty())
		{
			prefix = "level";
		}

		for (int index = 2; index < 100000; index++)
		{
			std::string candidate = prefix + " " + std::to_string(index);
			if (!roomFileExists(candidate.c_str()))
			{
				return candidate;
			}
		}

		return prefix + " copy";
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
	cleanupAllPreviews();
	labelFont.cleanup();
}

void WorldEditor::enter(gl2d::Renderer2D &renderer)
{
	refreshPlacementSizesFromRooms();
	invalidateAllLevelPreviews();
	if (sanitizeDoorLinks())
	{
		worldDirty = true;
		worldHasError = false;
		worldMessage = "Cleaned invalid world door links";
	}
	camera.zoom = tuning.cameraZoom;
	if (!cameraInitialized)
	{
		focusWorld(renderer);
	}
	else
	{
		clampCamera(renderer);
	}

	resetUndoHistory();
}

void WorldEditor::update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer,
	bool gameViewHovered, bool gameViewFocused)
{
	deltaTime = std::min(deltaTime, 0.05f);
	placedLevelDoubleClickTimer = std::max(placedLevelDoubleClickTimer - deltaTime, 0.f);
	if (placedLevelDoubleClickTimer <= 0.f)
	{
		lastClickedPlacedLevelName.clear();
	}

	// World reloads can destroy preview framebuffers, so apply them only at the
	// start of a frame before any preview textures are queued for drawing.
	if (pendingDiscardChanges)
	{
		pendingDiscardChanges = false;
		discardWorldChanges();
	}

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

	updateShortcuts(input, renderer, gameViewFocused);
	updateCamera(deltaTime, input, renderer);
	updateDragging(input, renderer, gameViewHovered);
	refreshClosestMissingPreview(renderer);

	renderer.setCamera(camera);
	drawWorld(renderer);
	drawGrid(renderer);
	drawLevels(renderer);
	drawDoorOverlays(renderer);
	drawLevelLabels(renderer);

	drawWindow(renderer);
	drawLevelFilesWindow(renderer);
	drawDiscardWindow(renderer);
}

WorldEditor::UndoSnapshot WorldEditor::captureUndoSnapshot() const
{
	UndoSnapshot snapshot = {};
	snapshot.world = world;
	snapshot.selectedLevelName = selectedLevelName;
	snapshot.selectedPlacedLevelName = selectedPlacedLevelName;
	snapshot.selectedPlacedLevels = selectedPlacedLevels;
	snapshot.selectedDoor = selectedDoor;
	snapshot.worldDirty = worldDirty;
	return snapshot;
}

bool WorldEditor::undoSnapshotsMatch(UndoSnapshot const &a, UndoSnapshot const &b) const
{
	auto worldDataMatches = [](WorldData const &lhs, WorldData const &rhs)
	{
		if (lhs.bounds != rhs.bounds || lhs.levels.size() != rhs.levels.size())
		{
			return false;
		}

		for (auto const &levelIt : lhs.levels)
		{
			auto rightLevel = rhs.levels.find(levelIt.first);
			if (rightLevel == rhs.levels.end())
			{
				return false;
			}

			WorldLevelPlacement const &leftPlacement = levelIt.second;
			WorldLevelPlacement const &rightPlacement = rightLevel->second;
			if (leftPlacement.name != rightPlacement.name ||
				leftPlacement.position != rightPlacement.position ||
				leftPlacement.size != rightPlacement.size ||
				leftPlacement.flags != rightPlacement.flags ||
				leftPlacement.doorLinks.size() != rightPlacement.doorLinks.size())
			{
				return false;
			}

			for (auto const &linkIt : leftPlacement.doorLinks)
			{
				auto rightLink = rightPlacement.doorLinks.find(linkIt.first);
				if (rightLink == rightPlacement.doorLinks.end())
				{
					return false;
				}

				if (linkIt.second.levelName != rightLink->second.levelName ||
					linkIt.second.doorName != rightLink->second.doorName)
				{
					return false;
				}
			}
		}

		return true;
	};

	return
		worldDataMatches(a.world, b.world) &&
		a.selectedLevelName == b.selectedLevelName &&
		a.selectedPlacedLevelName == b.selectedPlacedLevelName &&
		a.selectedPlacedLevels == b.selectedPlacedLevels &&
		doorReferencesMatch(a.selectedDoor, b.selectedDoor) &&
		a.worldDirty == b.worldDirty;
}

void WorldEditor::resetUndoHistory()
{
	undoHistory.clear();
	undoHistoryIndex = -1;
	pushUndoSnapshot();
}

void WorldEditor::pushUndoSnapshot()
{
	if (applyingUndoRedo)
	{
		return;
	}

	UndoSnapshot snapshot = captureUndoSnapshot();
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

bool WorldEditor::applyUndoSnapshot(UndoSnapshot const &snapshot, gl2d::Renderer2D &renderer)
{
	world = snapshot.world;
	selectedLevelName = snapshot.selectedLevelName;
	selectedPlacedLevelName = snapshot.selectedPlacedLevelName;
	selectedPlacedLevels = snapshot.selectedPlacedLevels;
	selectedDoor = snapshot.selectedDoor;
	worldDirty = snapshot.worldDirty;

	selectionPressActive = false;
	selectionDragActive = false;
	dragActive = false;
	placementDragMoved = false;
	dragStartPositions.clear();
	dragAnchorLevelName.clear();
	clearPendingDoorLink();

	syncPreviewStorageToWorld();
	refreshPlacementSizesFromRooms();
	if (cameraInitialized)
	{
		clampCamera(renderer);
	}

	worldHasError = false;
	worldMessage = {};
	return true;
}

void WorldEditor::undo(gl2d::Renderer2D &renderer)
{
	if (undoHistoryIndex <= 0)
	{
		return;
	}

	applyingUndoRedo = true;
	if (applyUndoSnapshot(undoHistory[undoHistoryIndex - 1], renderer))
	{
		undoHistoryIndex--;
		worldMessage = "Undo";
	}
	applyingUndoRedo = false;
}

void WorldEditor::redo(gl2d::Renderer2D &renderer)
{
	if (undoHistoryIndex + 1 >= static_cast<int>(undoHistory.size()))
	{
		return;
	}

	applyingUndoRedo = true;
	if (applyUndoSnapshot(undoHistory[undoHistoryIndex + 1], renderer))
	{
		undoHistoryIndex++;
		worldMessage = "Redo";
	}
	applyingUndoRedo = false;
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

	selectPlacedLevel(levelName);
	selectedLevelName = levelName;

	glm::vec4 rect = found->second.getRect();
	setViewCenter({rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f}, renderer);
	clampCamera(renderer);
}

void WorldEditor::clearPlacedLevelSelection()
{
	selectedPlacedLevelName.clear();
	selectedPlacedLevels.clear();
}

void WorldEditor::selectPlacedLevel(std::string const &levelName)
{
	clearPlacedLevelSelection();
	if (levelName.empty())
	{
		return;
	}

	selectedPlacedLevelName = levelName;
	selectedPlacedLevels.insert(levelName);
}

bool WorldEditor::isPlacedLevelSelected(std::string const &levelName) const
{
	return selectedPlacedLevels.find(levelName) != selectedPlacedLevels.end();
}

glm::vec4 WorldEditor::getSelectionRect() const
{
	float minX = std::min(selectionDragStart.x, selectionDragEnd.x);
	float minY = std::min(selectionDragStart.y, selectionDragEnd.y);
	float maxX = std::max(selectionDragStart.x, selectionDragEnd.x);
	float maxY = std::max(selectionDragStart.y, selectionDragEnd.y);

	return {minX, minY, maxX - minX, maxY - minY};
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
		tuning.cameraZoom -= 42.0f * deltaTime;
	}

	if (input.isButtonHeld(platform::Button::E))
	{
		tuning.cameraZoom += 42.0f * deltaTime;
	}

	tuning.cameraZoom = std::clamp(tuning.cameraZoom, kMinWorldZoom, kMaxWorldZoom);
	camera.zoom = tuning.cameraZoom;

	setViewCenter(center, renderer);
	clampCamera(renderer);
}

void WorldEditor::updateShortcuts(platform::Input &input, gl2d::Renderer2D &renderer, bool gameViewFocused)
{
	if (input.isButtonPressed(platform::Button::Escape))
	{
		dragActive = false;
		selectionPressActive = false;
		selectionDragActive = false;
		dragStartPositions.clear();
		dragAnchorLevelName.clear();
		clearPendingDoorLink();
	}

#if REMOVE_IMGUI == 0
	bool allowTabShortcut = !worldEditorModalPopupOpen();
	bool allowUndoRedoShortcut = !worldEditorModalPopupOpen();
	if (allowTabShortcut)
	{
		ImGuiIO &io = ImGui::GetIO();
		allowTabShortcut = gameViewFocused || (!io.WantTextInput && !ImGui::IsAnyItemActive());
		allowUndoRedoShortcut = allowTabShortcut;
	}

	if (allowTabShortcut && input.isButtonPressed(platform::Button::Tab))
	{
		requestLoadedLevelEditorMode = true;
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
#else
	if (input.isButtonPressed(platform::Button::Tab))
	{
		requestLoadedLevelEditorMode = true;
	}

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
#endif

	if (undoShortcut)
	{
		undo(renderer);
		return;
	}

	if (redoShortcut)
	{
		redo(renderer);
		return;
	}

	bool saveShortcut =
		input.isButtonHeld(platform::Button::LeftCtrl) &&
		input.isButtonPressed(platform::Button::S);
	if (saveShortcut)
	{
		saveWorld();
	}

	bool duplicateShortcut =
		input.isButtonHeld(platform::Button::LeftCtrl) &&
		input.isButtonPressed(platform::Button::D);
	if (duplicateShortcut)
	{
		duplicateSelectedLevel();
	}
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
	pendingDoorLinkPreviewPoint = mouseWorld;

	if (pendingDoorLinkPick)
	{
		selectionPressActive = false;
		selectionDragActive = false;
		pendingLinkHoveredDoor = getPlacedDoorAt(mouseWorld);
		if (doorReferencesMatch(pendingLinkHoveredDoor, pendingLinkSourceDoor))
		{
			pendingLinkHoveredDoor = {};
		}

		if (pendingDoorLinkDragActive)
		{
			dragActive = false;
			if (input.isLMouseHeld())
			{
				return;
			}

			if (input.isLMouseReleased())
			{
				if (isDoorReferenceSet(pendingLinkHoveredDoor))
				{
					linkDoors(pendingLinkSourceDoor, pendingLinkHoveredDoor);
				}
				else
				{
					selectDoor(pendingLinkSourceDoor);
					clearPendingDoorLink();
					worldHasError = false;
					worldMessage = "Link unchanged";
				}
			}

			return;
		}

		if (!input.isLMousePressed())
		{
			return;
		}

		if (!isDoorReferenceSet(pendingLinkHoveredDoor))
		{
			worldHasError = true;
			worldMessage = "Click another placed door to finish linking";
			return;
		}

		linkDoors(pendingLinkSourceDoor, pendingLinkHoveredDoor);
		return;
	}

	if (dragActive)
	{
		if (!input.isLMouseHeld() || !input.isButtonHeld(platform::Button::LeftCtrl))
		{
			if (placementDragMoved)
			{
				pushUndoSnapshot();
			}
			dragActive = false;
			placementDragMoved = false;
			dragStartPositions.clear();
			dragAnchorLevelName.clear();
			return;
		}

		auto anchorStart = dragStartPositions.find(dragAnchorLevelName);
		if (anchorStart == dragStartPositions.end())
		{
			dragActive = false;
			placementDragMoved = false;
			dragStartPositions.clear();
			dragAnchorLevelName.clear();
			return;
		}

		glm::vec2 anchorPosition = mouseWorld - dragGrabOffset;
		glm::vec2 dragDelta = anchorPosition - anchorStart->second;
		bool movedAny = false;

		for (auto const &dragIt : dragStartPositions)
		{
			auto found = world.levels.find(dragIt.first);
			if (found == world.levels.end())
			{
				continue;
			}

			glm::vec2 newPosition = dragIt.second + dragDelta;
			if (found->second.position != newPosition)
			{
				found->second.position = newPosition;
				ensureBoundsForPlacement(found->second);
				movedAny = true;
			}
		}

		if (movedAny)
		{
			worldDirty = true;
			placementDragMoved = true;
		}
		return;
	}

	if (selectionPressActive)
	{
		if (input.isLMouseHeld())
		{
			glm::vec2 mouseScreen = {static_cast<float>(input.mouseX), static_cast<float>(input.mouseY)};
			float dragDistance = glm::length(mouseScreen - selectionPressStartScreen);
			if (selectionDragActive || dragDistance >= 8.f)
			{
				selectionDragActive = true;
				selectionDragEnd = mouseWorld;
			}

			return;
		}

		if (input.isLMouseReleased())
		{
			bool finishedSelectionDrag = selectionDragActive;
			std::string pressedLevelName = selectionPressedLevelName;
			selectionPressActive = false;
			selectionDragActive = false;
			selectionPressedLevelName.clear();

			if (finishedSelectionDrag)
			{
				glm::vec4 selectionRect = getSelectionRect();
				clearSelectedDoor();
				clearPlacedLevelSelection();
				selectedLevelName.clear();
				lastClickedPlacedLevelName.clear();
				placedLevelDoubleClickTimer = 0.f;

				for (auto const &name : getSortedPlacedLevelNames(world))
				{
					glm::vec4 rect = world.levels.at(name).getRect();
					if (rectsOverlap(selectionRect, rect))
					{
						selectedPlacedLevels.insert(name);
						if (selectedPlacedLevelName.empty())
						{
							selectedPlacedLevelName = name;
						}
					}
				}

				if (!selectedPlacedLevelName.empty())
				{
					selectedLevelName = selectedPlacedLevelName;
				}

				return;
			}

			if (pressedLevelName.empty())
			{
				clearSelectedDoor();
				clearPlacedLevelSelection();
				selectedLevelName.clear();
				lastClickedPlacedLevelName.clear();
				placedLevelDoubleClickTimer = 0.f;
				return;
			}

			selectPlacedLevel(pressedLevelName);
			selectedLevelName = pressedLevelName;
			selectedDoor = {};

			if (lastClickedPlacedLevelName == pressedLevelName && placedLevelDoubleClickTimer > 0.f)
			{
				clearPendingDoorLink();
				requestLevelEditorMode = true;
				lastClickedPlacedLevelName.clear();
				placedLevelDoubleClickTimer = 0.f;
				return;
			}

			lastClickedPlacedLevelName = pressedLevelName;
			placedLevelDoubleClickTimer = kPlacedLevelDoubleClickTime;
			return;
		}
	}

	if (!input.isLMousePressed())
	{
		return;
	}

	DoorReference hoveredDoor = getPlacedDoorAt(mouseWorld);
	if (isDoorReferenceSet(hoveredDoor))
	{
		selectDoor(hoveredDoor);
		pendingDoorLinkPick = true;
		pendingDoorLinkDragActive = true;
		pendingLinkSourceDoor = hoveredDoor;
		pendingLinkHoveredDoor = {};
		pendingDoorLinkPreviewPoint = getDoorWorldCenter(hoveredDoor);
		return;
	}

	std::string hoveredLevel = getPlacedLevelAt(mouseWorld);
	if (input.isButtonHeld(platform::Button::LeftCtrl) && !hoveredLevel.empty())
	{
		clearSelectedDoor();
		if (!isPlacedLevelSelected(hoveredLevel))
		{
			selectPlacedLevel(hoveredLevel);
			selectedLevelName = hoveredLevel;
		}

		dragActive = true;
		dragAnchorLevelName = hoveredLevel;
		dragGrabOffset = mouseWorld - world.levels[hoveredLevel].position;
		dragStartPositions.clear();
		for (auto const &name : selectedPlacedLevels)
		{
			auto found = world.levels.find(name);
			if (found != world.levels.end())
			{
				dragStartPositions[name] = found->second.position;
			}
		}
		return;
	}

	selectionPressActive = true;
	selectionDragActive = false;
	selectionPressStartScreen = {static_cast<float>(input.mouseX), static_cast<float>(input.mouseY)};
	selectionDragStart = mouseWorld;
	selectionDragEnd = mouseWorld;
	selectionPressedLevelName = hoveredLevel;
}

void WorldEditor::selectDoor(DoorReference const &doorRef)
{
	if (!isDoorReferenceSet(doorRef))
	{
		clearSelectedDoor();
		return;
	}

	selectedDoor = doorRef;
	selectPlacedLevel(doorRef.levelName);
	selectedLevelName = doorRef.levelName;
}

void WorldEditor::clearSelectedDoor()
{
	selectedDoor = {};
	clearPendingDoorLink();
}

void WorldEditor::clearPendingDoorLink()
{
	pendingDoorLinkPick = false;
	pendingDoorLinkDragActive = false;
	pendingLinkSourceDoor = {};
	pendingLinkHoveredDoor = {};
	pendingDoorLinkPreviewPoint = {};
}

WorldEditor::DoorReference WorldEditor::getPlacedDoorAt(glm::vec2 worldPoint)
{
	if (isDoorReferenceSet(selectedDoor))
	{
		Door const *selectedPreviewDoor = getPreviewDoor(selectedDoor.levelName, selectedDoor.doorName);
		auto selectedPlacement = world.levels.find(selectedDoor.levelName);
		if (selectedPreviewDoor && selectedPlacement != world.levels.end())
		{
			glm::vec4 rect = {
				selectedPlacement->second.position.x + selectedPreviewDoor->position.x,
				selectedPlacement->second.position.y + selectedPreviewDoor->position.y,
				static_cast<float>(selectedPreviewDoor->size.x),
				static_cast<float>(selectedPreviewDoor->size.y)
			};

			if (worldPoint.x >= rect.x && worldPoint.y >= rect.y &&
				worldPoint.x <= rect.x + rect.z && worldPoint.y <= rect.y + rect.w)
			{
				return selectedDoor;
			}
		}
	}

	for (auto const &levelName : getSortedPlacedLevelNames(world))
	{
		auto placement = world.levels.find(levelName);
		auto preview = levelPreviews.find(levelName);
		if (placement == world.levels.end() || preview == levelPreviews.end())
		{
			continue;
		}

		for (int doorIndex = static_cast<int>(preview->second.doors.size()) - 1; doorIndex >= 0; doorIndex--)
		{
			Door const &door = preview->second.doors[doorIndex];
			glm::vec4 rect = {
				placement->second.position.x + door.position.x,
				placement->second.position.y + door.position.y,
				static_cast<float>(door.size.x),
				static_cast<float>(door.size.y)
			};

			if (worldPoint.x >= rect.x && worldPoint.y >= rect.y &&
				worldPoint.x <= rect.x + rect.z && worldPoint.y <= rect.y + rect.w)
			{
				return {levelName, door.name};
			}
		}
	}

	return {};
}

Door const *WorldEditor::getPreviewDoor(std::string const &levelName, std::string const &doorName) const
{
	auto preview = levelPreviews.find(levelName);
	if (preview == levelPreviews.end())
	{
		return nullptr;
	}

	for (Door const &door : preview->second.doors)
	{
		if (door.name == doorName)
		{
			return &door;
		}
	}

	return nullptr;
}

glm::vec2 WorldEditor::getDoorWorldCenter(DoorReference const &doorRef)
{
	Door const *door = getPreviewDoor(doorRef.levelName, doorRef.doorName);
	auto placement = world.levels.find(doorRef.levelName);
	if (!door || placement == world.levels.end())
	{
		return {};
	}

	return placement->second.position + glm::vec2(door->position) + glm::vec2(door->size) * 0.5f;
}

WorldDoorLink *WorldEditor::getDoorLink(std::string const &levelName, std::string const &doorName)
{
	auto level = world.levels.find(levelName);
	if (level == world.levels.end())
	{
		return nullptr;
	}

	auto link = level->second.doorLinks.find(doorName);
	if (link == level->second.doorLinks.end())
	{
		return nullptr;
	}

	return &link->second;
}

WorldDoorLink const *WorldEditor::getDoorLink(std::string const &levelName, std::string const &doorName) const
{
	auto level = world.levels.find(levelName);
	if (level == world.levels.end())
	{
		return nullptr;
	}

	auto link = level->second.doorLinks.find(doorName);
	if (link == level->second.doorLinks.end())
	{
		return nullptr;
	}

	return &link->second;
}

void WorldEditor::unlinkDoor(DoorReference const &doorRef)
{
	if (!isDoorReferenceSet(doorRef))
	{
		return;
	}

	auto level = world.levels.find(doorRef.levelName);
	if (level == world.levels.end())
	{
		return;
	}

	auto link = level->second.doorLinks.find(doorRef.doorName);
	if (link == level->second.doorLinks.end())
	{
		return;
	}

	DoorReference targetDoor = {link->second.levelName, link->second.doorName};
	level->second.doorLinks.erase(link);

	auto targetLevel = world.levels.find(targetDoor.levelName);
	if (targetLevel != world.levels.end())
	{
		auto targetLink = targetLevel->second.doorLinks.find(targetDoor.doorName);
		if (targetLink != targetLevel->second.doorLinks.end() &&
			targetLink->second.levelName == doorRef.levelName &&
			targetLink->second.doorName == doorRef.doorName)
		{
			targetLevel->second.doorLinks.erase(targetLink);
		}
	}

	worldDirty = true;
}

void WorldEditor::linkDoors(DoorReference const &sourceDoor, DoorReference const &targetDoor)
{
	if (!isDoorReferenceSet(sourceDoor) || !isDoorReferenceSet(targetDoor))
	{
		worldHasError = true;
		worldMessage = "Pick two valid doors to link";
		return;
	}

	if (doorReferencesMatch(sourceDoor, targetDoor))
	{
		worldHasError = true;
		worldMessage = "A door can't link to itself";
		return;
	}

	if (sourceDoor.levelName == targetDoor.levelName)
	{
		worldHasError = true;
		worldMessage = "Doors must link between different rooms";
		return;
	}

	if (!getPreviewDoor(sourceDoor.levelName, sourceDoor.doorName) ||
		!getPreviewDoor(targetDoor.levelName, targetDoor.doorName))
	{
		worldHasError = true;
		worldMessage = "One of the selected doors is no longer valid";
		clearPendingDoorLink();
		return;
	}

	unlinkDoor(sourceDoor);
	unlinkDoor(targetDoor);

	world.levels[sourceDoor.levelName].doorLinks[sourceDoor.doorName] = {
		targetDoor.levelName,
		targetDoor.doorName
	};
	world.levels[targetDoor.levelName].doorLinks[targetDoor.doorName] = {
		sourceDoor.levelName,
		sourceDoor.doorName
	};

	selectDoor(sourceDoor);
	clearPendingDoorLink();
	worldDirty = true;
	worldHasError = false;
	worldMessage = "Linked doors";
	pushUndoSnapshot();
}

bool WorldEditor::sanitizeDoorLinks()
{
	bool changed = false;

	for (auto &levelIt : world.levels)
	{
		auto &placement = levelIt.second;
		for (auto linkIt = placement.doorLinks.begin(); linkIt != placement.doorLinks.end(); )
		{
			Door const *sourceDoor = getPreviewDoor(levelIt.first, linkIt->first);
			Door const *targetDoor = getPreviewDoor(linkIt->second.levelName, linkIt->second.doorName);
			bool valid = true;

			if (!sourceDoor || !targetDoor)
			{
				valid = false;
			}
			else if (linkIt->second.levelName.empty() || linkIt->second.doorName.empty())
			{
				valid = false;
			}
			else if (levelIt.first == linkIt->second.levelName)
			{
				valid = false;
			}

			if (!valid)
			{
				linkIt = placement.doorLinks.erase(linkIt);
				changed = true;
				continue;
			}

			++linkIt;
		}
	}

	for (auto &levelIt : world.levels)
	{
		auto &placement = levelIt.second;
		for (auto linkIt = placement.doorLinks.begin(); linkIt != placement.doorLinks.end(); )
		{
			auto targetLevel = world.levels.find(linkIt->second.levelName);
			bool reciprocalValid = false;
			if (targetLevel != world.levels.end())
			{
				auto reciprocalLink = targetLevel->second.doorLinks.find(linkIt->second.doorName);
				if (reciprocalLink != targetLevel->second.doorLinks.end() &&
					reciprocalLink->second.levelName == levelIt.first &&
					reciprocalLink->second.doorName == linkIt->first)
				{
					reciprocalValid = true;
				}
			}

			if (!reciprocalValid)
			{
				linkIt = placement.doorLinks.erase(linkIt);
				changed = true;
				continue;
			}

			++linkIt;
		}
	}

	if (isDoorReferenceSet(selectedDoor) && !getPreviewDoor(selectedDoor.levelName, selectedDoor.doorName))
	{
		clearSelectedDoor();
	}

	if (pendingDoorLinkPick &&
		(!isDoorReferenceSet(pendingLinkSourceDoor) ||
		 !getPreviewDoor(pendingLinkSourceDoor.levelName, pendingLinkSourceDoor.doorName)))
	{
		clearPendingDoorLink();
	}

	if (pendingDoorLinkPick &&
		isDoorReferenceSet(pendingLinkHoveredDoor) &&
		!getPreviewDoor(pendingLinkHoveredDoor.levelName, pendingLinkHoveredDoor.doorName))
	{
		pendingLinkHoveredDoor = {};
	}

	return changed;
}

void WorldEditor::loadWorld()
{
	WorldIoResult result = loadWorldData(world);
	worldHasError = !result.success;
	worldMessage = result.message;
	worldDirty = false;
	clearPlacedLevelSelection();
	selectedDoor = {};
	pendingLinkSourceDoor = {};
	pendingLinkHoveredDoor = {};
	pendingDoorLinkPick = false;
	pendingDoorLinkDragActive = false;
	pendingDoorLinkPreviewPoint = {};
	selectionPressActive = false;
	selectionDragActive = false;
	dragActive = false;
	placementDragMoved = false;
	dragStartPositions.clear();
	dragAnchorLevelName.clear();
	refreshPlacementSizesFromRooms();

	for (auto const &it : world.levels)
	{
		ensureBoundsForPlacement(it.second);
	}

	syncPreviewStorageToWorld();
	invalidateAllLevelPreviews();
	resetUndoHistory();
}

void WorldEditor::saveWorld()
{
	bool linksChanged = sanitizeDoorLinks();
	WorldIoResult result = saveWorldData(world);
	worldHasError = !result.success;
	worldMessage = result.message;

	if (result.success)
	{
		worldDirty = false;
		if (linksChanged)
		{
			worldMessage = "Saved world and cleaned invalid door links";
		}
		pushUndoSnapshot();
	}
}

void WorldEditor::discardWorldChanges()
{
	loadWorld();
}

void WorldEditor::cleanupPreview(std::string const &levelName)
{
	auto found = levelPreviews.find(levelName);
	if (found == levelPreviews.end())
	{
		return;
	}

	found->second.frameBuffer.cleanup();
	levelPreviews.erase(found);
}

void WorldEditor::cleanupAllPreviews()
{
	for (auto &it : levelPreviews)
	{
		it.second.frameBuffer.cleanup();
	}

	levelPreviews.clear();
}

void WorldEditor::invalidateLevelPreview(std::string const &levelName)
{
	auto placement = world.levels.find(levelName);
	if (placement == world.levels.end())
	{
		cleanupPreview(levelName);
		previewRefreshFailedLevels.erase(levelName);
		return;
	}

	Room room = {};
	RoomIoResult result = loadRoomFromFile(room, levelName.c_str());
	if (!result.success)
	{
		cleanupPreview(levelName);
		previewRefreshFailedLevels.insert(levelName);
		return;
	}

	LevelPreview &preview = levelPreviews[levelName];
	preview.frameBuffer.cleanup();
	preview.doors = room.doors;
	placement->second.size = room.size;
	previewRefreshFailedLevels.erase(levelName);
}

void WorldEditor::invalidateAllLevelPreviews()
{
	for (auto &it : levelPreviews)
	{
		it.second.frameBuffer.cleanup();
	}
}

void WorldEditor::syncPreviewStorageToWorld()
{
	for (auto it = levelPreviews.begin(); it != levelPreviews.end(); )
	{
		if (world.levels.find(it->first) != world.levels.end())
		{
			++it;
			continue;
		}

		it->second.frameBuffer.cleanup();
		it = levelPreviews.erase(it);
	}

	for (auto it = previewRefreshFailedLevels.begin(); it != previewRefreshFailedLevels.end(); )
	{
		if (world.levels.find(*it) != world.levels.end())
		{
			++it;
			continue;
		}

		it = previewRefreshFailedLevels.erase(it);
	}
}

void WorldEditor::refreshPlacementSizesFromRooms()
{
	// World data stores placement and links only. Room size and door metadata come from the level file.
	for (auto &it : world.levels)
	{
		Room room = {};
		RoomIoResult result = loadRoomFromFile(room, it.first.c_str());
		if (result.success)
		{
			it.second.size = room.size;
			levelPreviews[it.first].doors = room.doors;
			previewRefreshFailedLevels.erase(it.first);
			continue;
		}

		cleanupPreview(it.first);
		previewRefreshFailedLevels.insert(it.first);
		if (it.second.size.x <= 0 || it.second.size.y <= 0)
		{
			it.second.size = {1, 1};
		}
	}
}

void WorldEditor::refreshLevelPreview(std::string const &levelName, gl2d::Renderer2D &renderer)
{
	if (world.levels.find(levelName) == world.levels.end())
	{
		cleanupPreview(levelName);
		previewRefreshFailedLevels.erase(levelName);
		return;
	}

	Room room = {};
	RoomIoResult result = loadRoomFromFile(room, levelName.c_str());
	if (!result.success)
	{
		cleanupPreview(levelName);
		previewRefreshFailedLevels.insert(levelName);
		return;
	}

	LevelPreview &preview = levelPreviews[levelName];
	preview.frameBuffer.cleanup();
	preview.frameBuffer.create(std::max(room.size.x, 1), std::max(room.size.y, 1), true);
	preview.doors = room.doors;
	world.levels[levelName].size = room.size;
	previewRefreshFailedLevels.erase(levelName);

	gl2d::Camera oldCamera = renderer.currentCamera;
	auto oldBlendMode = renderer.getBlendMode();
	int oldWindowW = renderer.windowW;
	int oldWindowH = renderer.windowH;

	renderer.clearDrawData();
	renderer.updateWindowMetrics(preview.frameBuffer.w, preview.frameBuffer.h);

	gl2d::Camera previewCamera = {};
	previewCamera.zoom = 1.f;
	previewCamera.follow(
		{room.size.x * 0.5f, room.size.y * 0.5f},
		1.f,
		0.f,
		0.f,
		static_cast<float>(preview.frameBuffer.w),
		static_cast<float>(preview.frameBuffer.h));

	preview.frameBuffer.bind();
	renderer.clearScreen(kPreviewBackgroundColor);
	preview.frameBuffer.unbind();

	renderer.setBlendMode(gl2d::Renderer2D::BlendMode_Alpha);
	renderer.setCamera(previewCamera);
	drawRoomPreview(room, renderer);
	renderer.flushFBO(preview.frameBuffer, true);

	renderer.setBlendMode(oldBlendMode);
	renderer.updateWindowMetrics(oldWindowW, oldWindowH);
	renderer.setCamera(oldCamera);
	renderer.clearDrawData();
}

bool WorldEditor::refreshClosestMissingPreview(gl2d::Renderer2D &renderer)
{
	struct PendingPreview
	{
		float distanceSquared = 0.f;
		std::string levelName = {};
	};

	syncPreviewStorageToWorld();

	glm::vec2 viewCenter = getViewCenter(renderer);
	std::vector<PendingPreview> pendingPreviews = {};
	pendingPreviews.reserve(world.levels.size());

	for (auto const &name : getSortedPlacedLevelNames(world))
	{
		if (previewRefreshFailedLevels.find(name) != previewRefreshFailedLevels.end())
		{
			continue;
		}

		auto preview = levelPreviews.find(name);
		if (preview != levelPreviews.end() && preview->second.frameBuffer.texture.isValid())
		{
			continue;
		}

		auto placement = world.levels.find(name);
		if (placement == world.levels.end())
		{
			continue;
		}

		glm::vec2 placementCenter = {
			placement->second.position.x + placement->second.size.x * 0.5f,
			placement->second.position.y + placement->second.size.y * 0.5f,
		};
		glm::vec2 delta = placementCenter - viewCenter;

		pendingPreviews.push_back({
			delta.x * delta.x + delta.y * delta.y,
			name,
		});
	}

	if (pendingPreviews.empty())
	{
		return false;
	}

	std::sort(pendingPreviews.begin(), pendingPreviews.end(),
		[](PendingPreview const &a, PendingPreview const &b)
	{
		if (a.distanceSquared != b.distanceSquared)
		{
			return a.distanceSquared < b.distanceSquared;
		}

		return a.levelName < b.levelName;
	});

	// Build only one missing preview each frame so entering the world editor stays responsive.
	refreshLevelPreview(pendingPreviews.front().levelName, renderer);
	return true;
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
		selectPlacedLevel(selectedLevelName);
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
	selectPlacedLevel(placement.name);
	selectedDoor = {};
	ensureBoundsForPlacement(placement);
	worldDirty = true;
	worldHasError = false;
	worldMessage = "Spawned level into world";
	invalidateLevelPreview(placement.name);
	pushUndoSnapshot();
}

void WorldEditor::duplicateSelectedLevel()
{
	if (selectedPlacedLevels.size() != 1 || selectedPlacedLevelName.empty())
	{
		worldHasError = true;
		worldMessage = "Select exactly one placed level to duplicate";
		return;
	}

	auto selectedPlacement = world.levels.find(selectedPlacedLevelName);
	if (selectedPlacement == world.levels.end())
	{
		worldHasError = true;
		worldMessage = "That level is no longer placed in the world";
		return;
	}

	// Duplicating a placed room creates a brand-new level file. Door links stay in world.json.
	Room room = {};
	RoomIoResult loadResult = loadRoomFromFile(room, selectedPlacedLevelName.c_str());
	worldHasError = !loadResult.success;
	worldMessage = loadResult.message;
	if (!loadResult.success)
	{
		return;
	}

	std::string duplicateName = makeDuplicateLevelName(loadResult.levelName);
	RoomIoResult saveResult = saveRoomToFile(room, duplicateName.c_str());
	worldHasError = !saveResult.success;
	worldMessage = saveResult.message;
	if (!saveResult.success)
	{
		return;
	}

	WorldLevelPlacement placement = {};
	placement.name = saveResult.levelName;
	placement.size = room.size;
	placement.position = selectedPlacement->second.position + glm::vec2(-4.f, -4.f);

	world.levels[placement.name] = placement;
	selectedLevelName = placement.name;
	selectPlacedLevel(placement.name);
	selectedDoor = {};
	ensureBoundsForPlacement(placement);
	worldDirty = true;
	worldHasError = false;
	worldMessage = "Duplicated level";
	invalidateLevelPreview(placement.name);
	pushUndoSnapshot();
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

	for (auto const &name : selectedPlacedLevels)
	{
		auto found = world.levels.find(name);
		if (found == world.levels.end())
		{
			continue;
		}

		glm::vec4 rect = found->second.getRect();
		if (rect.x <= worldPoint.x && rect.y <= worldPoint.y &&
			rect.x + rect.z >= worldPoint.x && rect.y + rect.w >= worldPoint.y)
		{
			return name;
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
		bool selected = isPlacedLevelSelected(name);
		bool primarySelected = name == selectedPlacedLevelName;
		auto preview = levelPreviews.find(name);

		gl2d::Color4f fillColor = primarySelected
			? gl2d::Color4f{0.18f, 0.52f, 1.0f, 0.28f}
			: (selected
				? gl2d::Color4f{0.20f, 0.48f, 0.95f, 0.20f}
				: gl2d::Color4f{0.22f, 0.30f, 0.42f, 0.24f});
		gl2d::Color4f outlineColor = primarySelected
			? gl2d::Color4f{0.25f, 0.70f, 1.0f, 1.f}
			: (selected
				? gl2d::Color4f{0.32f, 0.78f, 1.0f, 0.96f}
				: gl2d::Color4f{0.80f, 0.86f, 0.94f, 0.95f});

		if (preview != levelPreviews.end() && preview->second.frameBuffer.texture.isValid())
		{
			renderer.renderRectangle(
				placement.getRect(),
				preview->second.frameBuffer.texture,
				gl2d::Color4f{1, 1, 1, 1},
				{},
				0.f,
				{0.f, 0.f, 1.f, 1.f});
			renderer.renderRectangle(placement.getRect(), fillColor);
		}
		else
		{
			renderer.renderRectangle(placement.getRect(), fillColor);
		}

		renderer.renderRectangleOutline(placement.getRect(), outlineColor, 1.0f);
	}

	if (selectionDragActive)
	{
		glm::vec4 selectionRect = getSelectionRect();
		renderer.renderRectangle(selectionRect, {0.22f, 0.64f, 1.0f, 0.12f});
		renderer.renderRectangleOutline(selectionRect, {0.30f, 0.78f, 1.0f, 0.95f}, 0.35f);
	}
}

void WorldEditor::drawDoorOverlays(gl2d::Renderer2D &renderer)
{
	const gl2d::Color4f fillColor = {1.0f, 0.56f, 0.12f, 0.14f};
	const gl2d::Color4f outlineColor = {1.0f, 0.70f, 0.24f, 0.90f};
	const gl2d::Color4f selectedFillColor = {1.0f, 0.76f, 0.20f, 0.28f};
	const gl2d::Color4f selectedOutlineColor = {1.0f, 0.90f, 0.32f, 1.0f};
	const gl2d::Color4f linkedFillColor = {0.28f, 0.82f, 1.0f, 0.18f};
	const gl2d::Color4f linkedOutlineColor = {0.42f, 0.92f, 1.0f, 0.96f};

	std::unordered_set<std::string> drawnConnections;

	for (auto const &name : getSortedPlacedLevelNames(world))
	{
		auto preview = levelPreviews.find(name);
		if (preview == levelPreviews.end())
		{
			continue;
		}

		for (Door const &door : preview->second.doors)
		{
			DoorReference sourceDoor = {name, door.name};
			WorldDoorLink const *doorLink = getDoorLink(name, door.name);
			if (!doorLink || doorLink->levelName.empty() || doorLink->doorName.empty())
			{
				continue;
			}

			DoorReference targetDoor = {doorLink->levelName, doorLink->doorName};
			if (!getPreviewDoor(targetDoor.levelName, targetDoor.doorName))
			{
				continue;
			}

			std::string sourceKey = makeDoorReferenceKey(sourceDoor);
			std::string targetKey = makeDoorReferenceKey(targetDoor);
			std::string pairKey = sourceKey < targetKey
				? sourceKey + "|" + targetKey
				: targetKey + "|" + sourceKey;
			if (!drawnConnections.insert(pairKey).second)
			{
				continue;
			}

			gl2d::Color4f lineColor = kLinkedDoorLineColor;
			if (doorReferencesMatch(selectedDoor, sourceDoor) || doorReferencesMatch(selectedDoor, targetDoor))
			{
				lineColor = {1.0f, 0.92f, 0.40f, 0.95f};
			}

			renderer.renderLine(
				getDoorWorldCenter(sourceDoor),
				getDoorWorldCenter(targetDoor),
				lineColor,
				0.35f);
		}
	}

	if (pendingDoorLinkPick && isDoorReferenceSet(pendingLinkSourceDoor))
	{
		glm::vec2 lineStart = getDoorWorldCenter(pendingLinkSourceDoor);
		glm::vec2 lineEnd = pendingDoorLinkPreviewPoint;
		if (isDoorReferenceSet(pendingLinkHoveredDoor))
		{
			lineEnd = getDoorWorldCenter(pendingLinkHoveredDoor);
		}

		renderer.renderLine(
			lineStart,
			lineEnd,
			{1.0f, 0.92f, 0.36f, 0.96f},
			0.42f);
	}

	for (auto const &name : getSortedPlacedLevelNames(world))
	{
		auto const &placement = world.levels.at(name);
		auto preview = levelPreviews.find(name);
		if (preview == levelPreviews.end())
		{
			continue;
		}

		for (Door const &door : preview->second.doors)
		{
			DoorReference doorRef = {name, door.name};
			WorldDoorLink const *doorLink = getDoorLink(name, door.name);
			bool selected = doorReferencesMatch(selectedDoor, doorRef);
			bool linkedToSelected = false;
			if (doorLink && isDoorReferenceSet(selectedDoor))
			{
				linkedToSelected =
					doorLink->levelName == selectedDoor.levelName &&
					doorLink->doorName == selectedDoor.doorName;
			}
			bool pendingTarget = pendingDoorLinkPick && doorReferencesMatch(pendingLinkHoveredDoor, doorRef);

			glm::vec4 rect = {
				placement.position.x + door.position.x,
				placement.position.y + door.position.y,
				static_cast<float>(door.size.x),
				static_cast<float>(door.size.y)
			};

			renderer.renderRectangle(
				rect,
				selected ? selectedFillColor : (pendingTarget ? gl2d::Color4f{1.0f, 0.90f, 0.28f, 0.24f} : (linkedToSelected ? linkedFillColor : fillColor)));
			renderer.renderRectangleOutline(
				rect,
				selected ? selectedOutlineColor : (pendingTarget ? gl2d::Color4f{1.0f, 0.95f, 0.34f, 1.f} : (linkedToSelected ? linkedOutlineColor : outlineColor)),
				selected ? 0.55f : (pendingTarget ? 0.48f : 0.35f));
		}
	}

	if (!labelFont.texture.isValid() || tuning.cameraZoom < 0.18f)
	{
		return;
	}

	renderer.pushCamera();

	for (auto const &name : getSortedPlacedLevelNames(world))
	{
		auto const &placement = world.levels.at(name);
		auto preview = levelPreviews.find(name);
		if (preview == levelPreviews.end())
		{
			continue;
		}

		for (Door const &door : preview->second.doors)
		{
			glm::vec2 screenPos = worldToScreen(
				placement.position + glm::vec2(door.position),
				renderer) + glm::vec2(6.f, 6.f);

			if (screenPos.x < -220.f || screenPos.y < -80.f ||
				screenPos.x > renderer.windowW + 220.f || screenPos.y > renderer.windowH + 80.f)
			{
				continue;
			}

			char text[128] = {};
			if (tuning.cameraZoom >= 0.35f)
			{
				std::snprintf(text, sizeof(text), "%s\n%d x %d", door.name.c_str(), door.size.x, door.size.y);
			}
			else
			{
				std::snprintf(text, sizeof(text), "%s", door.name.c_str());
			}

			renderer.renderText(
				screenPos,
				text,
				labelFont,
				doorReferencesMatch(selectedDoor, {name, door.name})
					? gl2d::Color4f{1.0f, 0.92f, 0.42f, 1.f}
					: gl2d::Color4f{1.0f, 0.78f, 0.36f, 0.96f},
				18.f,
				4.f,
				3.f,
				false,
				{0.10f, 0.08f, 0.04f, 0.92f});
		}
	}

	renderer.popCamera();
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
			: (isPlacedLevelSelected(name)
				? gl2d::Color4f{0.72f, 0.92f, 1.0f, 1.f}
				: gl2d::Color4f{0.90f, 0.94f, 0.98f, 1.f});

		renderer.renderText(
			screenPos,
			text,
			labelFont,
			color,
			24.f,
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
		WorldDoorLink const *selectedDoorLink = nullptr;
		if (isDoorReferenceSet(selectedDoor))
		{
			selectedDoorLink = getDoorLink(selectedDoor.levelName, selectedDoor.doorName);
		}

		ImGui::TextUnformatted("F10 hides / shows ImGui");
		ImGui::TextUnformatted("F6 Game, F7 Level Editor, F8 World Editor");
		ImGui::TextUnformatted("` toggles back to gameplay");
		ImGui::TextUnformatted("WASD / Arrows move camera, Q/E zoom");
		ImGui::TextUnformatted("Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo");
		ImGui::TextUnformatted("Tab returns to the previously loaded level");
		ImGui::TextUnformatted("Click a level or door to select it");
		ImGui::TextUnformatted("Drag empty space to box-select levels");
		ImGui::TextUnformatted("Ctrl+drag a selected level to move the whole selection");
		ImGui::TextUnformatted("Drag from a door onto another door to relink it");
		ImGui::TextUnformatted("Ctrl+D duplicates the selected placed level");
		ImGui::TextUnformatted("Ctrl+S saves world.json");

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
		if (isDoorReferenceSet(selectedDoor))
		{
			ImGui::Text("Selected Door: %s", selectedDoor.doorName.c_str());
			ImGui::Text("Room: %s", selectedDoor.levelName.c_str());

			Door const *previewDoor = getPreviewDoor(selectedDoor.levelName, selectedDoor.doorName);
			if (previewDoor)
			{
				ImGui::Text("Size: %d x %d", previewDoor->size.x, previewDoor->size.y);
			}

			if (selectedDoorLink && !selectedDoorLink->levelName.empty() && !selectedDoorLink->doorName.empty())
			{
				ImGui::Text("Linked To: %s / %s",
					selectedDoorLink->levelName.c_str(),
					selectedDoorLink->doorName.c_str());
			}
			else
			{
				ImGui::TextUnformatted("Linked To: none");
			}

			if (!pendingDoorLinkPick)
			{
				if (ImGui::Button("Link To Another Door"))
				{
					pendingDoorLinkPick = true;
					pendingDoorLinkDragActive = false;
					pendingLinkSourceDoor = selectedDoor;
					pendingLinkHoveredDoor = {};
					pendingDoorLinkPreviewPoint = getDoorWorldCenter(selectedDoor);
					worldHasError = false;
					worldMessage = "Click another placed door to link it";
				}
			}
			else
			{
				if (ImGui::Button("Cancel Link"))
				{
					clearPendingDoorLink();
				}

				if (pendingDoorLinkDragActive)
				{
					ImGui::TextColored({1.f, 0.90f, 0.30f, 1.f}, "Release on another door to relink it");
				}
				else if (doorReferencesMatch(pendingLinkSourceDoor, selectedDoor))
				{
					ImGui::TextColored({1.f, 0.90f, 0.30f, 1.f}, "Pick a target door in another room");
				}
			}

			bool canUnlink = selectedDoorLink && !selectedDoorLink->levelName.empty() && !selectedDoorLink->doorName.empty();
			if (!canUnlink) { ImGui::BeginDisabled(); }
			if (ImGui::Button("Unlink Door"))
			{
				unlinkDoor(selectedDoor);
				worldHasError = false;
				worldMessage = "Unlinked door";
				pushUndoSnapshot();
			}
			if (!canUnlink) { ImGui::EndDisabled(); }
		}
		else if (selectedPlacedLevels.size() > 1)
		{
			ImGui::TextUnformatted("Multiple levels selected");
		}
		else if (!selectedPlacedLevelName.empty() && world.levels.find(selectedPlacedLevelName) != world.levels.end())
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
			pendingDiscardChanges = true;
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
