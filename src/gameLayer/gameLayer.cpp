#include "gameLayer.h"
#include "GameRenderWindow.h"
#include "Gameplay.h"
#include "LevelEditor.h"
#include "WorldEditor.h"
#include "imguiTools.h"

#include <cstdlib>
#include <fstream>
#include <logs.h>
#include <platformTools.h>
#include <gl2d/gl2d.h>

gl2d::Renderer2D renderer;

namespace
{
	enum RuntimeMode
	{
		gameplayMode = 0,
		levelEditorMode,
		worldEditorMode,
	};

	Gameplay gameplay;
	LevelEditor levelEditor;
	WorldEditor worldEditor;
	GameRenderWindow gameRenderWindow;
	int runtimeMode = gameplayMode;
	int syncedGameplayLevelLoadRevision = -1;

	void clearModeRequests();
	void syncLevelFileNamesFromGameplayToEditor();
	void syncLevelFileNamesFromEditorToGameplay();
	void syncLevelSelectionFromWorldEditor();
	void switchToMode(int newMode);

	void clearModeRequests()
	{
		gameplay.requestLevelEditorMode = false;
		gameplay.requestWorldEditorMode = false;
		levelEditor.requestGameplayMode = false;
		levelEditor.requestWorldEditorMode = false;
		worldEditor.requestGameplayMode = false;
		worldEditor.requestLevelEditorMode = false;
	}

	void syncLevelFileNamesFromGameplayToEditor()
	{
		if (syncedGameplayLevelLoadRevision != gameplay.levelLoadRevision)
		{
			levelEditor.levelDirty = false;
			syncedGameplayLevelLoadRevision = gameplay.levelLoadRevision;
		}

		levelEditor.currentLevelName = gameplay.currentLevelName;
		levelEditor.selectedLevelName = gameplay.selectedLevelName;
		worldEditor.selectedLevelName = gameplay.selectedLevelName.empty()
			? gameplay.currentLevelName
			: gameplay.selectedLevelName;
	}

	void syncLevelFileNamesFromEditorToGameplay()
	{
		gameplay.currentLevelName = levelEditor.currentLevelName;
		gameplay.selectedLevelName = levelEditor.selectedLevelName;
		worldEditor.selectedLevelName = levelEditor.selectedLevelName.empty()
			? levelEditor.currentLevelName
			: levelEditor.selectedLevelName;
	}

	void syncLevelSelectionFromWorldEditor()
	{
		gameplay.selectedLevelName = worldEditor.selectedLevelName;
		levelEditor.selectedLevelName = worldEditor.selectedLevelName;
	}

	void switchToMode(int newMode)
	{
		if (runtimeMode == gameplayMode)
		{
			syncLevelFileNamesFromGameplayToEditor();
		}
		else if (runtimeMode == levelEditorMode)
		{
			syncLevelFileNamesFromEditorToGameplay();
		}
		else if (runtimeMode == worldEditorMode)
		{
			syncLevelSelectionFromWorldEditor();
		}

		runtimeMode = newMode;
		clearModeRequests();

		if (runtimeMode == levelEditorMode)
		{
			levelEditor.enter(gameplay.room, renderer);
		}
		else if (runtimeMode == worldEditorMode)
		{
			worldEditor.enter(renderer);
		}
	}
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
	worldEditor.init();

	return true;
}

bool gameLogic(float deltaTime, platform::Input &input, SDL_Renderer *sdlRenderer)
{
	(void)sdlRenderer;

	int mainWindowW = platform::getFrameBufferSizeX();
	int mainWindowH = platform::getFrameBufferSizeY();

	renderer.updateWindowMetrics(mainWindowW, mainWindowH);

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

	gameRenderWindow.begin();

	platform::Input renderInput = input;
	glm::ivec2 renderSize = gameRenderWindow.getRenderSize({mainWindowW, mainWindowH});
	bool renderIntoWindow = gameRenderWindow.usesWindowFrameBuffer();

	if (renderIntoWindow)
	{
		renderInput = gameRenderWindow.remapInput(input);
	}

	renderer.updateWindowMetrics(renderSize.x, renderSize.y);

	if (input.isButtonPressed(platform::Button::F6))
	{
		switchToMode(gameplayMode);
	}
	else if (input.isButtonPressed(platform::Button::F7))
	{
		switchToMode(levelEditorMode);
	}
	else if (input.isButtonPressed(platform::Button::F8))
	{
		switchToMode(worldEditorMode);
	}

	if (renderIntoWindow)
	{
		// Keep the swapchain clear behind the ImGui viewport, then clear the offscreen game target.
		renderer.clearScreen();
		gameRenderWindow.frameBuffer.bind();
		renderer.clearScreen();
		gameRenderWindow.frameBuffer.unbind();
	}
	else
	{
		renderer.clearScreen();
	}

	if (runtimeMode == levelEditorMode)
	{
		levelEditor.update(deltaTime, renderInput, renderer, gameplay.room,
			gameRenderWindow.contentHovered, gameRenderWindow.contentFocused);
		syncLevelFileNamesFromEditorToGameplay();
		if (levelEditor.requestGameplayMode)
		{
			switchToMode(gameplayMode);
		}
		else if (levelEditor.requestWorldEditorMode)
		{
			switchToMode(worldEditorMode);
		}
	}
	else if (runtimeMode == worldEditorMode)
	{
		worldEditor.update(deltaTime, renderInput, renderer,
			gameRenderWindow.contentHovered, gameRenderWindow.contentFocused);
		syncLevelSelectionFromWorldEditor();
		if (worldEditor.requestGameplayMode)
		{
			switchToMode(gameplayMode);
		}
		else if (worldEditor.requestLevelEditorMode)
		{
			switchToMode(levelEditorMode);
		}
	}
	else
	{
		gameplay.update(deltaTime, renderInput, renderer);
		syncLevelFileNamesFromGameplayToEditor();
		if (gameplay.requestLevelEditorMode)
		{
			switchToMode(levelEditorMode);
		}
		else if (gameplay.requestWorldEditorMode)
		{
			switchToMode(worldEditorMode);
		}
	}

	if (renderIntoWindow)
	{
	#if GL2D_USE_SDL_GPU
		if (renderer.gpuDevice)
		{
			renderer.flushFBO(gameRenderWindow.frameBuffer, true);
		}
		else
	#endif
		{
			gameRenderWindow.frameBuffer.bind();
			renderer.flush(true);
			gameRenderWindow.frameBuffer.unbind();
		}

		renderer.updateWindowMetrics(mainWindowW, mainWindowH);
	}
	else
	{
	#if GL2D_USE_SDL_GPU
		if (!renderer.gpuDevice)
	#endif
		{
			renderer.flush();
		}
	}

	gameRenderWindow.end();

	return true;
}

//This function might not be be called if the program is forced closed
void closeGame()
{
	levelEditor.cleanup();
	worldEditor.cleanup();
	gameRenderWindow.cleanup();
}
