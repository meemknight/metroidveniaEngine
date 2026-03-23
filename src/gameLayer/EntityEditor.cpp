#include "EntityEditor.h"

#include "imguiTools.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <glm/glm.hpp>

namespace
{
	constexpr float kEditorMouseWheelZoomStep = 1.18f;
	constexpr float kMinEntityZoom = 8.f;
	constexpr float kMaxEntityZoom = 96.f;
	constexpr float kPlayerPreviewWidth = 3.f;
	constexpr float kPlayerPreviewHeight = 5.f;
	constexpr float kPointDrawSize = 0.24f;
	constexpr float kPointHitRadius = 0.34f;
	constexpr float kShapeLineWidth = 0.08f;

	bool imguiBlocksEntityEditorMouse(bool gameViewHovered)
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

	bool entityEditorModalPopupOpen()
	{
		return ImGui::IsPopupOpen("Discard Entity Changes");
	}
}

void EntityEditor::init()
{
	cleanup();

	*this = {};
	camera.zoom = tuning.cameraZoom;

	std::string fontPath = std::string(RESOURCES_PATH) + "arial.ttf";
	labelFont.createFromFile(fontPath.c_str());
	loadCurrentData();
}

void EntityEditor::cleanup()
{
	labelFont.cleanup();
}

void EntityEditor::enter(gl2d::Renderer2D &renderer)
{
	camera.zoom = tuning.cameraZoom;
	if (!cameraInitialized)
	{
		setViewCenter({}, renderer);
	}

	clampSelection();
}

void EntityEditor::update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer,
	bool gameViewHovered, bool gameViewFocused)
{
	deltaTime = std::min(deltaTime, 0.05f);

#if REMOVE_IMGUI == 0
	if (entityEditorModalPopupOpen())
	{
		input = {};
	}
#endif

	if (pendingDiscardChanges)
	{
		pendingDiscardChanges = false;
		discardChanges();
	}

	if (!cameraInitialized)
	{
		setViewCenter({}, renderer);
	}

	mouseWorldPosition = screenToWorld(
		{static_cast<float>(input.mouseX), static_cast<float>(input.mouseY)},
		renderer);

	updateShortcuts(input, renderer, gameViewFocused);
	updateCamera(deltaTime, input, renderer, gameViewHovered);
	updateEditing(input, renderer, gameViewHovered);

	renderer.setCamera(camera);
	drawGrid(renderer);
	drawPreview(renderer);
	drawWindow(renderer);
	drawFilesWindow(renderer);
	drawDiscardWindow();
}

glm::vec2 EntityEditor::getViewSize(gl2d::Renderer2D &renderer)
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

glm::vec2 EntityEditor::getViewCenter(gl2d::Renderer2D &renderer)
{
	return screenToWorld(
		{renderer.windowW * 0.5f, renderer.windowH * 0.5f},
		renderer);
}

void EntityEditor::setViewCenter(glm::vec2 center, gl2d::Renderer2D &renderer)
{
	camera.follow(center, 1.f, 0.f, 0.f,
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));
	camera.zoom = tuning.cameraZoom;
	cameraInitialized = true;
}

glm::vec2 EntityEditor::screenToWorld(glm::vec2 screenPos, gl2d::Renderer2D &renderer)
{
	return gl2d::internal::convertPoint(
		camera,
		screenPos,
		static_cast<float>(renderer.windowW),
		static_cast<float>(renderer.windowH));
}

glm::vec2 EntityEditor::worldToScreen(glm::vec2 worldPos, gl2d::Renderer2D &renderer)
{
	glm::vec2 screenCenter = {
		static_cast<float>(renderer.windowW) * 0.5f,
		static_cast<float>(renderer.windowH) * 0.5f
	};

	glm::vec2 cameraCenter = camera.position + screenCenter;
	return screenCenter + (worldPos - cameraCenter) * camera.zoom;
}

EditableConvexShape &EntityEditor::getSelectedShape()
{
	return getPlayerAttackShape(data, selectedShape);
}

EditableConvexShape const &EntityEditor::getSelectedShape() const
{
	return getPlayerAttackShape(data, selectedShape);
}

bool EntityEditor::selectedPointIsValid() const
{
	EditableConvexShape const &shape = getSelectedShape();
	return selectedPoint >= 0 && selectedPoint < static_cast<int>(shape.points.size());
}

int EntityEditor::getHoveredPointIndex(glm::vec2 worldPoint) const
{
	EditableConvexShape const &shape = getSelectedShape();
	for (int i = static_cast<int>(shape.points.size()) - 1; i >= 0; i--)
	{
		if (glm::distance(shape.points[i], worldPoint) <= kPointHitRadius)
		{
			return i;
		}
	}

	return -1;
}

void EntityEditor::clampSelection()
{
	if (!selectedPointIsValid())
	{
		selectedPoint = -1;
	}
}

void EntityEditor::updateCamera(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer, bool gameViewHovered)
{
	float moveX = 0.f;
	float moveY = 0.f;

#if REMOVE_IMGUI == 0
	ImGuiIO &io = ImGui::GetIO();
	bool allowKeyboardCamera = !io.WantTextInput && !ImGui::IsAnyItemActive();
	if (gameViewHovered)
	{
		allowKeyboardCamera = true;
	}
#else
	bool allowKeyboardCamera = true;
#endif

	if (allowKeyboardCamera)
	{
		if (input.isButtonHeld(platform::Button::A) || input.isButtonHeld(platform::Button::Left)) { moveX -= 1.f; }
		if (input.isButtonHeld(platform::Button::D) || input.isButtonHeld(platform::Button::Right)) { moveX += 1.f; }
		if (input.isButtonHeld(platform::Button::W) || input.isButtonHeld(platform::Button::Up)) { moveY -= 1.f; }
		if (input.isButtonHeld(platform::Button::S) || input.isButtonHeld(platform::Button::Down)) { moveY += 1.f; }
	}

	glm::vec2 viewCenter = getViewCenter(renderer);
	glm::vec2 moveDirection = {moveX, moveY};
	if (moveDirection != glm::vec2{})
	{
		moveDirection = glm::normalize(moveDirection);
		viewCenter += moveDirection * tuning.cameraMoveSpeed * deltaTime;
		setViewCenter(viewCenter, renderer);
	}

	bool allowScrollZoom = gameViewHovered;
	float zoomMultiplier = 1.f;

	if (allowKeyboardCamera && input.isButtonHeld(platform::Button::Q))
	{
		zoomMultiplier /= (1.f + deltaTime * 3.5f);
	}
	if (allowKeyboardCamera && input.isButtonHeld(platform::Button::E))
	{
		zoomMultiplier *= (1.f + deltaTime * 3.5f);
	}

	if (allowScrollZoom && std::abs(input.mouseScrollY) > 0.001f)
	{
		zoomMultiplier *= std::pow(kEditorMouseWheelZoomStep, input.mouseScrollY);
	}

	if (zoomMultiplier != 1.f)
	{
		tuning.cameraZoom = std::clamp(tuning.cameraZoom * zoomMultiplier, kMinEntityZoom, kMaxEntityZoom);
		camera.zoom = tuning.cameraZoom;
		setViewCenter(viewCenter, renderer);
	}
}

void EntityEditor::updateShortcuts(platform::Input &input, gl2d::Renderer2D &renderer, bool gameViewFocused)
{
#if REMOVE_IMGUI == 0
	bool allowEditorShortcuts = !entityEditorModalPopupOpen();
	if (allowEditorShortcuts)
	{
		ImGuiIO &io = ImGui::GetIO();
		allowEditorShortcuts = gameViewFocused || (!io.WantTextInput && !ImGui::IsAnyItemActive());
	}
#else
	bool allowEditorShortcuts = true;
#endif

	if (!allowEditorShortcuts)
	{
		return;
	}

	if (input.isButtonHeld(platform::Button::LeftCtrl) && input.isButtonPressed(platform::Button::S))
	{
		saveCurrentData();
	}

#if REMOVE_IMGUI == 0
	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		deleteSelectedPoint();
	}
#endif

	if (input.isButtonPressed(platform::Button::G))
	{
		tuning.showGrid = !tuning.showGrid;
	}

	if (input.isButtonPressed(platform::Button::F6))
	{
		requestGameplayMode = true;
	}
	else if (input.isButtonPressed(platform::Button::F7))
	{
		requestLevelEditorMode = true;
	}
	else if (input.isButtonPressed(platform::Button::F8))
	{
		requestWorldEditorMode = true;
	}
}

void EntityEditor::updateEditing(platform::Input &input, gl2d::Renderer2D &renderer, bool gameViewHovered)
{
#if REMOVE_IMGUI == 0
	if (imguiBlocksEntityEditorMouse(gameViewHovered))
	{
		pointDragActive = false;
		return;
	}
#endif

	EditableConvexShape &shape = getSelectedShape();

	if (pointDragActive)
	{
		if (input.isLMouseHeld() && selectedPointIsValid())
		{
			shape.points[selectedPoint] = mouseWorldPosition - pointDragOffset;
			entityDirty = true;
			entityHasError = false;
			entityMessage = "Moved point";
			return;
		}

		pointDragActive = false;
		return;
	}

	int hoveredPointIndex = getHoveredPointIndex(mouseWorldPosition);

	if (input.isButtonHeld(platform::Button::LeftCtrl) && input.isLMousePressed() && hoveredPointIndex == -1)
	{
		size_t oldPointCount = shape.points.size();
		addPoint(mouseWorldPosition);
		if (shape.points.size() != oldPointCount)
		{
			selectedPoint = static_cast<int>(shape.points.size()) - 1;
			pointDragActive = true;
			pointDragOffset = {};
		}
		return;
	}

	if (input.isLMousePressed())
	{
		selectedPoint = hoveredPointIndex;
		if (selectedPointIsValid())
		{
			pointDragActive = true;
			pointDragOffset = mouseWorldPosition - shape.points[selectedPoint];
			return;
		}
	}
}

void EntityEditor::addPoint(glm::vec2 worldPoint)
{
	EditableConvexShape &shape = getSelectedShape();
	if (shape.points.size() >= maxEntityShapePoints)
	{
		entityHasError = true;
		entityMessage = "Shape already uses the max number of points";
		return;
	}

	shape.points.push_back(worldPoint);
	selectedPoint = static_cast<int>(shape.points.size()) - 1;
	entityDirty = true;
	entityHasError = false;
	entityMessage = "Added point";
}

void EntityEditor::deleteSelectedPoint()
{
	EditableConvexShape &shape = getSelectedShape();
	if (!selectedPointIsValid())
	{
		return;
	}

	if (shape.points.size() <= 3)
	{
		entityHasError = true;
		entityMessage = "Convex shapes need at least 3 points";
		return;
	}

	shape.points.erase(shape.points.begin() + selectedPoint);
	selectedPoint = std::min(selectedPoint, static_cast<int>(shape.points.size()) - 1);
	entityDirty = true;
	entityHasError = false;
	entityMessage = "Deleted point";
}

void EntityEditor::saveCurrentData()
{
	std::string message = {};
	if (saveEntityData(data, &message))
	{
		entityDirty = false;
		entityHasError = false;
		entityMessage = message;
	}
	else
	{
		entityHasError = true;
		entityMessage = message;
	}
}

void EntityEditor::loadCurrentData()
{
	std::string message = {};
	if (loadEntityData(data, &message))
	{
		entityDirty = false;
		entityHasError = false;
		entityMessage = message;
		clampSelection();
	}
	else
	{
		data = makeDefaultEntityData();
		entityDirty = false;
		entityHasError = true;
		entityMessage = message;
		clampSelection();
	}
}

void EntityEditor::discardChanges()
{
	loadCurrentData();
}

void EntityEditor::drawGrid(gl2d::Renderer2D &renderer)
{
	if (!tuning.showGrid)
	{
		return;
	}

	glm::vec2 viewSize = getViewSize(renderer);
	glm::vec2 viewCenter = getViewCenter(renderer);
	float minX = std::floor(viewCenter.x - viewSize.x * 0.5f) - 1.f;
	float maxX = std::ceil(viewCenter.x + viewSize.x * 0.5f) + 1.f;
	float minY = std::floor(viewCenter.y - viewSize.y * 0.5f) - 1.f;
	float maxY = std::ceil(viewCenter.y + viewSize.y * 0.5f) + 1.f;

	for (int x = static_cast<int>(minX); x <= static_cast<int>(maxX); x++)
	{
		gl2d::Color4f color = {0.22f, 0.26f, 0.32f, tuning.gridAlpha};
		if (x == 0)
		{
			color = {0.50f, 0.62f, 0.84f, 0.45f};
		}

		renderer.renderLine(
			{static_cast<float>(x), minY},
			{static_cast<float>(x), maxY},
			color,
			tuning.gridLineWidth);
	}

	for (int y = static_cast<int>(minY); y <= static_cast<int>(maxY); y++)
	{
		gl2d::Color4f color = {0.22f, 0.26f, 0.32f, tuning.gridAlpha};
		if (y == 0)
		{
			color = {0.50f, 0.62f, 0.84f, 0.45f};
		}

		renderer.renderLine(
			{minX, static_cast<float>(y)},
			{maxX, static_cast<float>(y)},
			color,
			tuning.gridLineWidth);
	}
}

void EntityEditor::drawPreview(gl2d::Renderer2D &renderer)
{
	glm::vec4 playerRect = {
		-kPlayerPreviewWidth * 0.5f,
		-kPlayerPreviewHeight * 0.5f,
		kPlayerPreviewWidth,
		kPlayerPreviewHeight
	};

	renderer.renderRectangle(playerRect, {0.26f, 0.32f, 0.40f, 0.30f});
	renderer.renderRectangleOutline(playerRect, {0.72f, 0.80f, 0.92f, 0.92f}, 0.08f);
	renderer.renderLine({0.f, -0.35f}, {0.f, 0.35f}, {0.90f, 0.96f, 1.0f, 0.95f}, 0.08f);
	renderer.renderLine({-0.35f, 0.f}, {0.35f, 0.f}, {0.90f, 0.96f, 1.0f, 0.95f}, 0.08f);

	EditableConvexShape const &shape = getSelectedShape();
	if (shape.points.size() < 2)
	{
		return;
	}

	for (size_t i = 0; i < shape.points.size(); i++)
	{
		glm::vec2 a = shape.points[i];
		glm::vec2 b = shape.points[(i + 1) % shape.points.size()];
		renderer.renderLine(a, b, {1.0f, 0.82f, 0.32f, 0.96f}, kShapeLineWidth);
	}

	for (size_t i = 0; i < shape.points.size(); i++)
	{
		bool selected = static_cast<int>(i) == selectedPoint;
		float pointSize = selected ? kPointDrawSize * 1.45f : kPointDrawSize;
		gl2d::Color4f color = selected
			? gl2d::Color4f{1.0f, 0.96f, 0.46f, 1.f}
			: gl2d::Color4f{0.98f, 0.68f, 0.26f, 0.96f};

		renderer.renderRectangle(
			{
				shape.points[i].x - pointSize * 0.5f,
				shape.points[i].y - pointSize * 0.5f,
				pointSize,
				pointSize
			},
			color);
	}

	if (labelFont.texture.isValid())
	{
		renderer.pushCamera();
		for (size_t i = 0; i < shape.points.size(); i++)
		{
			char text[16] = {};
			std::snprintf(text, sizeof(text), "%d", static_cast<int>(i));
			glm::vec2 screenPos = worldToScreen(shape.points[i], renderer);
			renderer.renderText(
				screenPos + glm::vec2(10.f, -28.f),
				text,
				labelFont,
				{0.95f, 0.98f, 1.0f, 1.f},
				24.f,
				3.f,
				2.f,
				false,
				{0.04f, 0.06f, 0.10f, 0.90f});
		}
		renderer.popCamera();
	}
}

void EntityEditor::drawWindow(gl2d::Renderer2D &renderer)
{
	(void)renderer;

	ImGui::SetNextWindowBgAlpha(0.90f);
	ImGui::SetNextWindowSize({420.f, 0.f}, ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Entity Editor"))
	{
		static char const *entityNames[] = {"Player"};
		ImGui::TextUnformatted("Entity");
		if (ImGui::Combo("##Entity", &selectedEntity, entityNames, IM_ARRAYSIZE(entityNames)))
		{
			selectedPoint = -1;
		}

		char const *currentShapeName = getPlayerAttackShapeName(selectedShape);
		if (ImGui::BeginCombo("Shape", currentShapeName))
		{
			for (int shapeId = 0; shapeId < playerAttackShapeCount; shapeId++)
			{
				bool selected = shapeId == selectedShape;
				if (ImGui::Selectable(getPlayerAttackShapeName(shapeId), selected))
				{
					selectedShape = shapeId;
					selectedPoint = -1;
				}

				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		EditableConvexShape &shape = getSelectedShape();
		ImGui::Text("Points: %d / %d", static_cast<int>(shape.points.size()), maxEntityShapePoints);
		ImGui::TextUnformatted("Ctrl+Click adds a point, drag a point to move it, Delete removes it.");

		if (ImGui::Button("Add Point At Mouse"))
		{
			addPoint(mouseWorldPosition);
		}
		ImGui::SameLine();
		bool canDeletePoint = selectedPointIsValid();
		if (!canDeletePoint) { ImGui::BeginDisabled(); }
		if (ImGui::Button("Delete Selected Point"))
		{
			deleteSelectedPoint();
		}
		if (!canDeletePoint) { ImGui::EndDisabled(); }

		ImGui::SameLine();
		if (ImGui::Button("Reset Shape To Default"))
		{
			EntityData defaults = makeDefaultEntityData();
			getSelectedShape() = getPlayerAttackShape(defaults, selectedShape);
			selectedPoint = -1;
			entityDirty = true;
			entityHasError = false;
			entityMessage = "Reset shape to defaults";
		}

		if (selectedPointIsValid())
		{
			glm::vec2 &point = shape.points[selectedPoint];
			if (ImGui::DragFloat2("Selected Point", &point.x, 0.02f))
			{
				entityDirty = true;
				entityHasError = false;
				entityMessage = "Updated point";
			}
		}
		else
		{
			ImGui::TextUnformatted("Selected Point: none");
		}

		if (ImGui::BeginChild("ShapePointList", {0.f, 220.f}, true))
		{
			for (int i = 0; i < static_cast<int>(shape.points.size()); i++)
			{
				char label[96] = {};
				std::snprintf(label, sizeof(label), "%d: %.2f, %.2f",
					i, shape.points[i].x, shape.points[i].y);
				if (ImGui::Selectable(label, i == selectedPoint))
				{
					selectedPoint = i;
				}
			}
		}
		ImGui::EndChild();

		ImGui::Separator();
		ImGui::TextUnformatted("The preview edits the right-facing shape. Gameplay mirrors it automatically for left-facing slashes.");
		ImGui::TextUnformatted("F6 Game, F7 Level Editor, F8 World Editor, F9 Entity Editor");

		if (!entityMessage.empty())
		{
			ImGui::TextColored(
				entityHasError ? ImVec4(1.f, 0.45f, 0.35f, 1.f) : ImVec4(0.35f, 1.f, 0.55f, 1.f),
				"%s",
				entityMessage.c_str());
		}
	}
	ImGui::End();
}

void EntityEditor::drawFilesWindow(gl2d::Renderer2D &renderer)
{
	(void)renderer;

	ImGui::SetNextWindowBgAlpha(0.90f);
	ImGui::SetNextWindowSize({420.f, 0.f}, ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Entity Files"))
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
		if (ImGui::RadioButton("World Editor", false))
		{
			requestWorldEditorMode = true;
		}
		ImGui::SameLine();
		ImGui::RadioButton("Entity Editor", true);

		ImGui::Separator();
		if (entityDirty)
		{
			ImGui::TextColored({1.0f, 0.90f, 0.30f, 1.f}, "Current Data: entityShapes.json*");
		}
		else
		{
			ImGui::TextColored({0.35f, 1.f, 0.55f, 1.f}, "Current Data: entityShapes.json");
		}

		ImGui::Text("File: %s", getEntityDataFilePath().c_str());
		ImGui::Text("State: %s", entityDirty ? "Unsaved changes" : "Saved");
		ImGui::Text("Current Shape: %s", getPlayerAttackShapeName(selectedShape));
		ImGui::Text("Ctrl+S saves entityShapes.json");

		ImGui::Separator();
		if (ImGui::Button("Save Current"))
		{
			saveCurrentData();
		}

		ImGui::SameLine();
		if (ImGui::Button("Reload From Disk"))
		{
			if (entityDirty)
			{
				ImGui::OpenPopup("Discard Entity Changes");
			}
			else
			{
				loadCurrentData();
			}
		}

		ImGui::SameLine();
		bool canDiscard = entityDirty;
		if (!canDiscard) { ImGui::BeginDisabled(); }
		if (ImGui::Button("Discard Changes"))
		{
			ImGui::OpenPopup("Discard Entity Changes");
		}
		if (!canDiscard) { ImGui::EndDisabled(); }
	}
	ImGui::End();
}

void EntityEditor::drawDiscardWindow()
{
	if (ImGui::BeginPopupModal("Discard Entity Changes", 0, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Reload entityShapes.json and discard your unsaved changes?");
		if (ImGui::Button("Discard", {140.f, 0.f}))
		{
			pendingDiscardChanges = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", {140.f, 0.f}))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
