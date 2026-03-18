#include "GameRenderWindow.h"

#include "imguiTools.h"

#include <algorithm>
#include <cmath>

void GameRenderWindow::begin()
{
	beginCalled = false;
	contentVisible = false;
	contentHovered = false;
	contentFocused = false;
	contentPosition = {};
	contentSize = {};

#if REMOVE_IMGUI == 0
	if (!enabled || !ImGui::isImguiWindowOpen())
	{
		return;
	}

	beginCalled = true;

	ImGui::SetNextWindowSize({960.f, 540.f}, ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
	bool drawWindowContents = ImGui::Begin(
		"Game View",
		nullptr,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();

	contentFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (!drawWindowContents)
	{
		ImGui::End();
		return;
	}

	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 avail = ImGui::GetContentRegionAvail();

	if (avail.x <= 0.f || avail.y <= 0.f)
	{
		ImGui::End();
		return;
	}

	contentPosition = {pos.x, pos.y};
	contentSize = {avail.x, avail.y};
	contentVisible = true;
	syncFrameBuffer();

	ImVec2 mouse = ImGui::GetIO().MousePos;
	contentHovered =
		mouse.x >= contentPosition.x &&
		mouse.y >= contentPosition.y &&
		mouse.x < contentPosition.x + contentSize.x &&
		mouse.y < contentPosition.y + contentSize.y;

	ImGui::End();
#endif
}

void GameRenderWindow::end()
{
#if REMOVE_IMGUI == 0
	if (!beginCalled)
	{
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
	bool drawWindowContents = ImGui::Begin(
		"Game View",
		nullptr,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();

	if (drawWindowContents && contentVisible)
	{
		ImTextureID textureId = {};

	#if GL2D_USE_SDL_GPU
		if (frameBuffer.texture.gpuTexture)
		{
			textureId = (ImTextureID)frameBuffer.texture.gpuTexture;
		}
	#endif

		if (!textureId && frameBuffer.texture.tex)
		{
			textureId = (ImTextureID)frameBuffer.texture.tex;
		}

		if (textureId)
		{
			ImGui::Image(textureId, {contentSize.x, contentSize.y});
		}
		else
		{
			ImGui::Dummy({contentSize.x, contentSize.y});
		}
	}

	ImGui::End();
#endif
}

void GameRenderWindow::cleanup()
{
	frameBuffer.cleanup();
	*this = {};
}

void GameRenderWindow::syncFrameBuffer()
{
	int targetW = std::max(static_cast<int>(std::floor(contentSize.x)), 1);
	int targetH = std::max(static_cast<int>(std::floor(contentSize.y)), 1);
	frameBuffer.resize(targetW, targetH);
}

bool GameRenderWindow::usesWindowFrameBuffer()
{
	return enabled && contentVisible && frameBuffer.w > 0 && frameBuffer.h > 0;
}

glm::ivec2 GameRenderWindow::getRenderSize(glm::ivec2 fallbackSize)
{
	if (!usesWindowFrameBuffer())
	{
		return fallbackSize;
	}

	return {frameBuffer.w, frameBuffer.h};
}

platform::Input GameRenderWindow::remapInput(platform::Input input)
{
	if (!usesWindowFrameBuffer())
	{
		return input;
	}

	input.mouseX = static_cast<int>(std::floor(input.mouseX - contentPosition.x));
	input.mouseY = static_cast<int>(std::floor(input.mouseY - contentPosition.y));
	input.hasFocus = contentFocused;

	if (!contentHovered)
	{
		input.lMouse.pressed = 0;
		input.lMouse.held = 0;
		input.rMouse.pressed = 0;
		input.rMouse.held = 0;
	}

	return input;
}
