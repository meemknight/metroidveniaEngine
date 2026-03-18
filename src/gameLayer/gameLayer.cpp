#include "gameLayer.h"
#include "Gameplay.h"
#include "LevelEditor.h"
#include "imguiTools.h"

#include <cstdlib>
#include <fstream>
#include <logs.h>
#include <platformTools.h>
#include <gl2d/gl2d.h>

gl2d::Renderer2D renderer;

namespace
{
	Gameplay gameplay;
	LevelEditor levelEditor;
	bool levelEditorMode = false;
}

// Rebuilds shader binaries in development and reloads GPU shader objects.
static void tryHotReloadShaders()
{
#if defined(_WIN32) && defined(DEVELOPLEMT_BUILD) && (DEVELOPLEMT_BUILD == 1)
	std::string scriptPath = std::string(RESOURCES_PATH) + "shaders/compile_all_shaders.bat";
	for (char &c : scriptPath)
	{
		if (c == '/') { c = '\\'; }
	}

	std::ifstream scriptFile(scriptPath);
	if (!scriptFile.is_open())
	{
		platform::log("Shader reload failed: missing resources\\shaders\\compile_all_shaders.bat",
			LogManager::logError);
		return;
	}

	const std::string command = std::string("cmd.exe /C call \"") + scriptPath + "\"";
	int result = std::system(command.c_str());
	if (result != 0)
	{
		platform::log("Shader reload failed: compile_all_shaders.bat returned an error",
			LogManager::logError);
		return;
	}

	renderer.reloadGpuShaders();

	platform::log("Hot reloaded shaders", LogManager::logNormal);
#else
	platform::log("Shader reload is only enabled on Windows development builds", LogManager::logWarning);
#endif
}

gl2d::Renderer2D &getRenderer()
{
	return renderer;
}

bool initGame(SDL_Renderer *sdlRenderer)
{
	gl2d::init();

	renderer.create(sdlRenderer);
#if GL2D_USE_SDL_GPU
	if (!renderer.gpuDevice)
	{
		platform::log("SDL_gpu device unavailable on this platform; GPU post-process effects are disabled.",
			LogManager::logWarning);
	}
#endif

	gameplay.init();
	levelEditor.init();

	return true;
}

bool gameLogic(float deltaTime, platform::Input &input, SDL_Renderer *sdlRenderer)
{
	(void)sdlRenderer;

	int w = platform::getFrameBufferSizeX();
	int h = platform::getFrameBufferSizeY();

	renderer.updateWindowMetrics(w, h);

	if (input.isButtonPressed(platform::Button::F1))
	{
		platform::setFullScreen(!platform::isFullScreen());
	}

	if (input.isButtonPressed(platform::Button::F5))
	{
		tryHotReloadShaders();
	}

#if REMOVE_IMGUI == 0
	if (input.isButtonPressed(platform::Button::F10))
	{
		ImGui::toggleImguiWindowOpen();
	}
#endif

	if (input.isButtonPressed(platform::Button::F6))
	{
		levelEditorMode = !levelEditorMode;
		if (levelEditorMode)
		{
			levelEditor.enter(gameplay.room, renderer);
		}
	}

	renderer.clearScreen();

	if (levelEditorMode)
	{
		levelEditor.update(deltaTime, input, renderer, gameplay.room);
	}
	else
	{
		gameplay.update(deltaTime, input, renderer);
	}

#if GL2D_USE_SDL_GPU
	if (!renderer.gpuDevice)
#endif
	{
		renderer.flush();
	}

	return true;
}

//This function might not be be called if the program is forced closed
void closeGame()
{
	levelEditor.cleanup();
}
