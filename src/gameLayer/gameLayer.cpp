#include "gameLayer.h"
#include "EntityEditor.h"
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
		entityEditorMode,
	};

	struct LevelEditorResumeState
	{
		bool valid = false;
		gl2d::Camera camera = {};
		float cameraZoom = 32.f;
		bool cameraInitialized = false;
		int tool = LevelEditor::measureTool;
	};

	struct WorldEditorResumeState
	{
		bool valid = false;
		gl2d::Camera camera = {};
		float cameraZoom = 1.f;
		bool cameraInitialized = false;
		std::string selectedLevelName = {};
	};

	struct EntityEditorResumeState
	{
		bool valid = false;
		gl2d::Camera camera = {};
		float cameraZoom = 24.f;
		bool cameraInitialized = false;
		int selectedEntity = 0;
		int selectedShape = playerAttackShapeFront;
		int selectedPoint = -1;
	};

	Gameplay gameplay;
	LevelEditor levelEditor;
	WorldEditor worldEditor;
	EntityEditor entityEditor;
	GameRenderWindow gameRenderWindow;
	int runtimeMode = gameplayMode;
	int lastEditorMode = levelEditorMode;
	int syncedGameplayLevelLoadRevision = -1;
	bool levelEditorLoaded = false;
	bool worldEditorLoaded = false;
	bool entityEditorLoaded = false;
	int pendingModeSwitchTarget = -1;
	int queuedModeSwitchTarget = -1;
	LevelEditorResumeState levelEditorResumeState = {};
	WorldEditorResumeState worldEditorResumeState = {};
	EntityEditorResumeState entityEditorResumeState = {};

	void clearModeRequests();
	void syncLevelFileNamesFromGameplayToEditor();
	void syncLevelFileNamesFromEditorToGameplay();
	void syncLevelSelectionFromWorldEditor();
	void saveLevelEditorResumeState();
	void saveWorldEditorResumeState();
	void unloadLevelEditor();
	void unloadWorldEditor();
	void saveEntityEditorResumeState();
	void unloadEntityEditor();
	void prepareLevelEditorForEnter();
	void prepareWorldEditorForEnter();
	void prepareEntityEditorForEnter();
	char const *getRuntimeModeName(int mode);
	void switchToMode(int newMode);
	void requestModeSwitch(int newMode);
	void applyQueuedModeSwitch();
#if REMOVE_IMGUI == 0
	bool modeSwitchPopupOpen();
	void drawModeSwitchPopup();
#endif

	void clearModeRequests()
	{
		gameplay.requestLevelEditorMode = false;
		gameplay.requestWorldEditorMode = false;
		gameplay.requestEntityEditorMode = false;
		levelEditor.requestGameplayMode = false;
		levelEditor.requestWorldEditorMode = false;
		levelEditor.requestEntityEditorMode = false;
		worldEditor.requestGameplayMode = false;
		worldEditor.requestLevelEditorMode = false;
		worldEditor.requestLoadedLevelEditorMode = false;
		worldEditor.requestEntityEditorMode = false;
		entityEditor.requestGameplayMode = false;
		entityEditor.requestLevelEditorMode = false;
		entityEditor.requestWorldEditorMode = false;
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

	void saveLevelEditorResumeState()
	{
		if (!levelEditorLoaded)
		{
			return;
		}

		levelEditorResumeState.valid = true;
		levelEditorResumeState.camera = levelEditor.camera;
		levelEditorResumeState.cameraZoom = levelEditor.tuning.cameraZoom;
		levelEditorResumeState.cameraInitialized = levelEditor.cameraInitialized;
		levelEditorResumeState.tool = levelEditor.tool;
	}

	void saveWorldEditorResumeState()
	{
		if (!worldEditorLoaded)
		{
			return;
		}

		worldEditorResumeState.valid = true;
		worldEditorResumeState.camera = worldEditor.camera;
		worldEditorResumeState.cameraZoom = worldEditor.tuning.cameraZoom;
		worldEditorResumeState.cameraInitialized = worldEditor.cameraInitialized;
		worldEditorResumeState.selectedLevelName = worldEditor.selectedLevelName;
	}

	void saveEntityEditorResumeState()
	{
		if (!entityEditorLoaded)
		{
			return;
		}

		entityEditorResumeState.valid = true;
		entityEditorResumeState.camera = entityEditor.camera;
		entityEditorResumeState.cameraZoom = entityEditor.tuning.cameraZoom;
		entityEditorResumeState.cameraInitialized = entityEditor.cameraInitialized;
		entityEditorResumeState.selectedEntity = entityEditor.selectedEntity;
		entityEditorResumeState.selectedShape = entityEditor.selectedShape;
		entityEditorResumeState.selectedPoint = entityEditor.selectedPoint;
	}

	void unloadLevelEditor()
	{
		if (!levelEditorLoaded)
		{
			return;
		}

		saveLevelEditorResumeState();
		levelEditor.cleanup();
		levelEditor = {};
		levelEditorLoaded = false;
	}

	void unloadWorldEditor()
	{
		if (!worldEditorLoaded)
		{
			return;
		}

		saveWorldEditorResumeState();
		worldEditor.cleanup();
		worldEditor = {};
		worldEditorLoaded = false;
	}

	void unloadEntityEditor()
	{
		if (!entityEditorLoaded)
		{
			return;
		}

		saveEntityEditorResumeState();
		entityEditor.cleanup();
		entityEditor = {};
		entityEditorLoaded = false;
	}

	void prepareLevelEditorForEnter()
	{
		levelEditor.init();
		levelEditorLoaded = true;
		levelEditor.currentLevelName = gameplay.currentLevelName;
		levelEditor.selectedLevelName = gameplay.selectedLevelName;

		if (levelEditorResumeState.valid)
		{
			levelEditor.camera = levelEditorResumeState.camera;
			levelEditor.tuning.cameraZoom = levelEditorResumeState.cameraZoom;
			levelEditor.cameraInitialized = levelEditorResumeState.cameraInitialized;
			levelEditor.tool = levelEditorResumeState.tool;
		}

		// When we jump into the level editor from the world view, open the selected room directly.
		if (!levelEditor.selectedLevelName.empty() &&
			levelEditor.selectedLevelName != levelEditor.currentLevelName)
		{
			levelEditor.loadSelectedLevel(gameplay.room, renderer);
			gameplay.currentLevelName = levelEditor.currentLevelName;
			gameplay.selectedLevelName = levelEditor.selectedLevelName;
		}
	}

	void prepareWorldEditorForEnter()
	{
		worldEditor.init();
		worldEditorLoaded = true;

		if (worldEditorResumeState.valid)
		{
			worldEditor.camera = worldEditorResumeState.camera;
			worldEditor.tuning.cameraZoom = worldEditorResumeState.cameraZoom;
			worldEditor.cameraInitialized = worldEditorResumeState.cameraInitialized;
			worldEditor.selectedLevelName = worldEditorResumeState.selectedLevelName;
		}

		std::string gameplaySelectedLevel = gameplay.selectedLevelName.empty()
			? gameplay.currentLevelName
			: gameplay.selectedLevelName;
		if (!gameplaySelectedLevel.empty())
		{
			worldEditor.selectedLevelName = gameplaySelectedLevel;
		}
	}

	void prepareEntityEditorForEnter()
	{
		entityEditor.init();
		entityEditorLoaded = true;

		if (entityEditorResumeState.valid)
		{
			entityEditor.camera = entityEditorResumeState.camera;
			entityEditor.tuning.cameraZoom = entityEditorResumeState.cameraZoom;
			entityEditor.cameraInitialized = entityEditorResumeState.cameraInitialized;
			entityEditor.selectedEntity = entityEditorResumeState.selectedEntity;
			entityEditor.selectedShape = entityEditorResumeState.selectedShape;
			entityEditor.selectedPoint = entityEditorResumeState.selectedPoint;
			entityEditor.clampSelection();
		}
	}

	char const *getRuntimeModeName(int mode)
	{
		switch (mode)
		{
			case gameplayMode: return "Game";
			case levelEditorMode: return "Level Editor";
			case worldEditorMode: return "World Editor";
			case entityEditorMode: return "Entity Editor";
			default: return "Unknown";
		}
	}

	void switchToMode(int newMode)
	{
		if (newMode == runtimeMode)
		{
			clearModeRequests();
			pendingModeSwitchTarget = -1;
			return;
		}

		if (runtimeMode == gameplayMode)
		{
		}
		else if (runtimeMode == levelEditorMode)
		{
			lastEditorMode = levelEditorMode;
			syncLevelFileNamesFromEditorToGameplay();
			unloadLevelEditor();
		}
		else if (runtimeMode == worldEditorMode)
		{
			lastEditorMode = worldEditorMode;
			syncLevelSelectionFromWorldEditor();
			unloadWorldEditor();
		}
		else if (runtimeMode == entityEditorMode)
		{
			lastEditorMode = entityEditorMode;
			unloadEntityEditor();
		}

		runtimeMode = newMode;
		clearModeRequests();
		pendingModeSwitchTarget = -1;
		queuedModeSwitchTarget = -1;

		if (runtimeMode == levelEditorMode)
		{
			prepareLevelEditorForEnter();
			levelEditor.enter(gameplay.room, renderer);
		}
		else if (runtimeMode == worldEditorMode)
		{
			prepareWorldEditorForEnter();
			worldEditor.enter(renderer);
		}
		else if (runtimeMode == entityEditorMode)
		{
			prepareEntityEditorForEnter();
			entityEditor.enter(renderer);
		}
		else if (runtimeMode == gameplayMode)
		{
			gameplay.refreshWorldData();
			gameplay.reloadEntityData();
		}
	}

	void requestModeSwitch(int newMode)
	{
		clearModeRequests();

		if (newMode == runtimeMode)
		{
			pendingModeSwitchTarget = -1;
			return;
		}

#if REMOVE_IMGUI == 0
		if ((runtimeMode == levelEditorMode && levelEditor.levelDirty) ||
			(runtimeMode == worldEditorMode && worldEditor.worldDirty) ||
			(runtimeMode == entityEditorMode && entityEditor.entityDirty))
		{
			pendingModeSwitchTarget = newMode;
			ImGui::OpenPopup("Unsaved Changes Before Switching");
			return;
		}
#else
		if ((runtimeMode == levelEditorMode && levelEditor.levelDirty) ||
			(runtimeMode == worldEditorMode && worldEditor.worldDirty) ||
			(runtimeMode == entityEditorMode && entityEditor.entityDirty))
		{
			return;
		}
#endif

		queuedModeSwitchTarget = newMode;
	}

	// Apply queued mode switches only at safe frame boundaries so queued draw data
	// never references textures/fonts that got cleaned up mid-frame.
	void applyQueuedModeSwitch()
	{
		if (queuedModeSwitchTarget == -1 || queuedModeSwitchTarget == runtimeMode)
		{
			queuedModeSwitchTarget = -1;
			return;
		}

		switchToMode(queuedModeSwitchTarget);
	}

#if REMOVE_IMGUI == 0
	bool modeSwitchPopupOpen()
	{
		return ImGui::IsPopupOpen("Unsaved Changes Before Switching");
	}

	// Mode switches go through one popup so hotkeys and radio buttons behave the same.
	void drawModeSwitchPopup()
	{
		if (ImGui::BeginPopupModal("Unsaved Changes Before Switching", 0, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Switch from %s to %s?",
				getRuntimeModeName(runtimeMode),
				getRuntimeModeName(pendingModeSwitchTarget));

			if (runtimeMode == levelEditorMode)
			{
				ImGui::TextUnformatted("The current level has unsaved changes.");
				ImGui::TextUnformatted("Save, discard, or cancel before changing modes.");
			}
			else if (runtimeMode == worldEditorMode)
			{
				ImGui::TextUnformatted("The current world has unsaved changes.");
				ImGui::TextUnformatted("Save, discard, or cancel before changing modes.");
			}
			else if (runtimeMode == entityEditorMode)
			{
				ImGui::TextUnformatted("The current entity data has unsaved changes.");
				ImGui::TextUnformatted("Save, discard, or cancel before changing modes.");
			}

			if (runtimeMode == levelEditorMode && levelEditor.fileActionHasError && !levelEditor.fileActionMessage.empty())
			{
				ImGui::TextColored({1.f, 0.45f, 0.35f, 1.f}, "%s", levelEditor.fileActionMessage.c_str());
			}
			else if (runtimeMode == worldEditorMode && worldEditor.worldHasError && !worldEditor.worldMessage.empty())
			{
				ImGui::TextColored({1.f, 0.45f, 0.35f, 1.f}, "%s", worldEditor.worldMessage.c_str());
			}
			else if (runtimeMode == entityEditorMode && entityEditor.entityHasError && !entityEditor.entityMessage.empty())
			{
				ImGui::TextColored({1.f, 0.45f, 0.35f, 1.f}, "%s", entityEditor.entityMessage.c_str());
			}

			if (ImGui::Button("Save", {120.f, 0.f}))
			{
				bool canSwitch = false;
				if (runtimeMode == levelEditorMode)
				{
					levelEditor.saveCurrentLevel(gameplay.room);
					canSwitch = !levelEditor.levelDirty && !levelEditor.fileActionHasError;
				}
				else if (runtimeMode == worldEditorMode)
				{
					worldEditor.saveWorld();
					canSwitch = !worldEditor.worldDirty && !worldEditor.worldHasError;
				}
				else if (runtimeMode == entityEditorMode)
				{
					entityEditor.saveCurrentData();
					canSwitch = !entityEditor.entityDirty && !entityEditor.entityHasError;
				}

				if (canSwitch)
				{
					queuedModeSwitchTarget = pendingModeSwitchTarget;
					pendingModeSwitchTarget = -1;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard", {120.f, 0.f}))
			{
				if (runtimeMode == levelEditorMode)
				{
					gl2d::Camera preservedCamera = levelEditor.camera;
					float preservedZoom = levelEditor.tuning.cameraZoom;
					bool preservedCameraInitialized = levelEditor.cameraInitialized;
					int preservedTool = levelEditor.tool;
					levelEditor.reloadCurrentLevel(gameplay.room, renderer);
					levelEditor.camera = preservedCamera;
					levelEditor.tuning.cameraZoom = preservedZoom;
					levelEditor.cameraInitialized = preservedCameraInitialized;
					levelEditor.tool = preservedTool;
				}
				else if (runtimeMode == entityEditorMode)
				{
					entityEditor.discardChanges();
				}

				queuedModeSwitchTarget = pendingModeSwitchTarget;
				pendingModeSwitchTarget = -1;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", {120.f, 0.f}))
			{
				pendingModeSwitchTarget = -1;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
#endif
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
	levelEditor = {};
	worldEditor = {};
	entityEditor = {};
	levelEditorLoaded = false;
	worldEditorLoaded = false;
	entityEditorLoaded = false;
	levelEditorResumeState = {};
	worldEditorResumeState = {};
	entityEditorResumeState = {};
	pendingModeSwitchTarget = -1;
	queuedModeSwitchTarget = -1;

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
		//temporarily removed
		//ImGui::toggleImguiWindowOpen();
	}
#endif

	gameRenderWindow.begin();

#if REMOVE_IMGUI == 0
	bool blockingModeSwitchPopup = modeSwitchPopupOpen();
#else
	bool blockingModeSwitchPopup = false;
#endif

	platform::Input renderInput = input;
	glm::ivec2 renderSize = gameRenderWindow.getRenderSize({mainWindowW, mainWindowH});
	bool renderIntoWindow = gameRenderWindow.usesWindowFrameBuffer();

	if (renderIntoWindow)
	{
		renderInput = gameRenderWindow.remapInput(input);
	}

	if (blockingModeSwitchPopup)
	{
		renderInput = {};
	}

	renderer.updateWindowMetrics(renderSize.x, renderSize.y);

	if (!blockingModeSwitchPopup && input.isButtonPressed(platform::Button::F6))
	{
		requestModeSwitch(gameplayMode);
	}
	else if (!blockingModeSwitchPopup && input.isButtonPressed(platform::Button::F7))
	{
		requestModeSwitch(levelEditorMode);
	}
	else if (!blockingModeSwitchPopup && input.isButtonPressed(platform::Button::F8))
	{
		requestModeSwitch(worldEditorMode);
	}
	else if (!blockingModeSwitchPopup && input.isButtonPressed(platform::Button::F9))
	{
		requestModeSwitch(entityEditorMode);
	}
	else if (!blockingModeSwitchPopup && input.isButtonPressed(platform::Button::Grave))
	{
		if (runtimeMode == gameplayMode)
		{
			requestModeSwitch(lastEditorMode);
		}
		else
		{
			requestModeSwitch(gameplayMode);
		}
	}

	applyQueuedModeSwitch();

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
			requestModeSwitch(gameplayMode);
		}
		else if (levelEditor.requestWorldEditorMode)
		{
			requestModeSwitch(worldEditorMode);
		}
		else if (levelEditor.requestEntityEditorMode)
		{
			requestModeSwitch(entityEditorMode);
		}
	}
	else if (runtimeMode == worldEditorMode)
	{
		worldEditor.update(deltaTime, renderInput, renderer,
			gameRenderWindow.contentHovered, gameRenderWindow.contentFocused);
		if (worldEditor.requestGameplayMode)
		{
			syncLevelSelectionFromWorldEditor();
			requestModeSwitch(gameplayMode);
		}
		else if (worldEditor.requestLoadedLevelEditorMode)
		{
			requestModeSwitch(levelEditorMode);
		}
		else if (worldEditor.requestLevelEditorMode)
		{
			syncLevelSelectionFromWorldEditor();
			requestModeSwitch(levelEditorMode);
		}
		else if (worldEditor.requestEntityEditorMode)
		{
			syncLevelSelectionFromWorldEditor();
			requestModeSwitch(entityEditorMode);
		}
		else
		{
			syncLevelSelectionFromWorldEditor();
		}
	}
	else if (runtimeMode == entityEditorMode)
	{
		entityEditor.update(deltaTime, renderInput, renderer,
			gameRenderWindow.contentHovered, gameRenderWindow.contentFocused);
		if (entityEditor.requestGameplayMode)
		{
			requestModeSwitch(gameplayMode);
		}
		else if (entityEditor.requestLevelEditorMode)
		{
			requestModeSwitch(levelEditorMode);
		}
		else if (entityEditor.requestWorldEditorMode)
		{
			requestModeSwitch(worldEditorMode);
		}
	}
	else
	{
		gameplay.update(deltaTime, renderInput, renderer);
		if (gameplay.requestLevelEditorMode)
		{
			requestModeSwitch(levelEditorMode);
		}
		else if (gameplay.requestWorldEditorMode)
		{
			requestModeSwitch(worldEditorMode);
		}
		else if (gameplay.requestEntityEditorMode)
		{
			requestModeSwitch(entityEditorMode);
		}
	}

#if REMOVE_IMGUI == 0
	drawModeSwitchPopup();
#endif

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

bool hasUnsavedEditorChangesForClose()
{
	return
		(runtimeMode == levelEditorMode && levelEditorLoaded && levelEditor.levelDirty) ||
		(runtimeMode == worldEditorMode && worldEditorLoaded && worldEditor.worldDirty) ||
		(runtimeMode == entityEditorMode && entityEditorLoaded && entityEditor.entityDirty);
}

std::string getUnsavedEditorChangesDescriptionForClose()
{
	if (runtimeMode == levelEditorMode && levelEditorLoaded && levelEditor.levelDirty)
	{
		if (!levelEditor.currentLevelName.empty())
		{
			return "the current level \"" + levelEditor.currentLevelName + "\"";
		}

		return "the current level";
	}

	if (runtimeMode == worldEditorMode && worldEditorLoaded && worldEditor.worldDirty)
	{
		return "the current world";
	}

	if (runtimeMode == entityEditorMode && entityEditorLoaded && entityEditor.entityDirty)
	{
		return "the current entity shapes";
	}

	return {};
}

bool saveUnsavedEditorChangesForClose(std::string *errorMessage)
{
	if (runtimeMode == levelEditorMode && levelEditorLoaded && levelEditor.levelDirty)
	{
		levelEditor.saveCurrentLevel(gameplay.room);
		if (levelEditor.levelDirty || levelEditor.fileActionHasError)
		{
			if (errorMessage)
			{
				*errorMessage = levelEditor.fileActionMessage.empty()
					? "Couldn't save the current level."
					: levelEditor.fileActionMessage;
			}

			return false;
		}

		return true;
	}

	if (runtimeMode == worldEditorMode && worldEditorLoaded && worldEditor.worldDirty)
	{
		worldEditor.saveWorld();
		if (worldEditor.worldDirty || worldEditor.worldHasError)
		{
			if (errorMessage)
			{
				*errorMessage = worldEditor.worldMessage.empty()
					? "Couldn't save the current world."
					: worldEditor.worldMessage;
			}

			return false;
		}

		return true;
	}

	if (runtimeMode == entityEditorMode && entityEditorLoaded && entityEditor.entityDirty)
	{
		entityEditor.saveCurrentData();
		if (entityEditor.entityDirty || entityEditor.entityHasError)
		{
			if (errorMessage)
			{
				*errorMessage = entityEditor.entityMessage.empty()
					? "Couldn't save the current entity shapes."
					: entityEditor.entityMessage;
			}

			return false;
		}

		return true;
	}

	return true;
}

//This function might not be be called if the program is forced closed
void closeGame()
{
	gameplay.cleanup();
	if (levelEditorLoaded)
	{
		levelEditor.cleanup();
	}
	if (worldEditorLoaded)
	{
		worldEditor.cleanup();
	}
	if (entityEditorLoaded)
	{
		entityEditor.cleanup();
	}
	gameRenderWindow.cleanup();
}
