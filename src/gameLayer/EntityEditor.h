#pragma once

#include "EntityData.h"

#include <gl2d/gl2d.h>
#include <platformInput.h>
#include <string>

// Entity editor for authoring reusable convex gameplay shapes such as attacks.
struct EntityEditor
{
	struct Tuning
	{
		float cameraZoom = 24.f;
		float cameraMoveSpeed = 18.f;
		bool showGrid = true;
		float gridAlpha = 0.18f;
		float gridLineWidth = 0.05f;
	};

	void init();
	void cleanup();
	void enter(gl2d::Renderer2D &renderer);
	void update(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer,
		bool gameViewHovered, bool gameViewFocused);

	glm::vec2 getViewSize(gl2d::Renderer2D &renderer);
	glm::vec2 getViewCenter(gl2d::Renderer2D &renderer);
	void setViewCenter(glm::vec2 center, gl2d::Renderer2D &renderer);
	glm::vec2 screenToWorld(glm::vec2 screenPos, gl2d::Renderer2D &renderer);
	glm::vec2 worldToScreen(glm::vec2 worldPos, gl2d::Renderer2D &renderer);
	EditableConvexShape &getSelectedShape();
	EditableConvexShape const &getSelectedShape() const;
	bool selectedPointIsValid() const;
	int getHoveredPointIndex(glm::vec2 worldPoint) const;
	void clampSelection();
	void updateCamera(float deltaTime, platform::Input &input, gl2d::Renderer2D &renderer, bool gameViewHovered);
	void updateShortcuts(platform::Input &input, gl2d::Renderer2D &renderer, bool gameViewFocused);
	void updateEditing(platform::Input &input, gl2d::Renderer2D &renderer, bool gameViewHovered);
	void addPoint(glm::vec2 worldPoint);
	void deleteSelectedPoint();
	void saveCurrentData();
	void loadCurrentData();
	void discardChanges();
	void drawGrid(gl2d::Renderer2D &renderer);
	void drawPreview(gl2d::Renderer2D &renderer);
	void drawWindow(gl2d::Renderer2D &renderer);
	void drawFilesWindow(gl2d::Renderer2D &renderer);
	void drawDiscardWindow();

	gl2d::Camera camera = {};
	gl2d::Font labelFont = {};
	Tuning tuning = {};
	EntityData data = {};

	int selectedEntity = 0;
	int selectedShape = playerAttackShapeFront;
	int selectedPoint = -1;
	bool pointDragActive = false;
	glm::vec2 pointDragOffset = {};
	glm::vec2 mouseWorldPosition = {};
	bool cameraInitialized = false;

	bool entityDirty = false;
	bool entityHasError = false;
	std::string entityMessage = {};
	bool pendingDiscardChanges = false;

	bool requestGameplayMode = false;
	bool requestLevelEditorMode = false;
	bool requestWorldEditorMode = false;
};
