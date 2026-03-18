#pragma once

#include <glm/vec2.hpp>
#include <platformInput.h>
#include <gl2d/gl2d.h>

// Renders the game into a framebuffer and shows it inside an ImGui window.
struct GameRenderWindow
{
	void begin();
	void end();
	void cleanup();
	void syncFrameBuffer();
	bool usesWindowFrameBuffer();
	glm::ivec2 getRenderSize(glm::ivec2 fallbackSize);
	platform::Input remapInput(platform::Input input);

	bool enabled = true;
	bool beginCalled = false;
	bool contentVisible = false;
	bool contentHovered = false;
	bool contentFocused = false;

	glm::vec2 contentPosition = {};
	glm::vec2 contentSize = {};

	gl2d::FrameBuffer frameBuffer = {};
};
