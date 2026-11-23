#include "ModuleUI.h"
#include "../Engine.h"
#include "../Window/Window.h"
#include "../Input/Input.h"
#include "../Render/Render.h"
#include "../Render/Texture/TextureManager.h"
#include "../Render/Primitives/GeometryGenerator.h"
#include "../Scene/Scene.h"
#include "../Scene/GameObjects/GameObject.h"
#include "../Scene/GameObjects/Components/TransformComponent.h"
#include "../Scene/GameObjects/Components/MeshComponent.h"
#include "../Scene/GameObjects/Components/MaterialComponent.h"
#include "../Scene/GameObjects/Components/CameraComponent.h"
#include "../Audio/Audio.h"
#include "../ResourceManagement/ResourceManager.h"
#include "../ResourceManagement/ResourceMesh.h"
#include "../ResourceManagement/ResourceTexture.h"
#include "../ResourceManagement/Importers/ImporterManager.h"
#include "EditorDragDrop.h"
#include "EditorIcons.h"
#include "EditorPlatform.h"
#include "AboutContent.h"
#include "../Project/ProjectInfo.h"
#include "Version.h"
#include "EditorTheme.h"
#include "EditorWidgets.h"

#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h> // gizmos 3D (siempre despues de imgui.h)

#include <SDL3/SDL.h>

#include "../../Utils/Log.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _DEBUG
	#include <tracy/Tracy.hpp>
#endif

using namespace EditorWidgets;

namespace
{
	constexpr const char* REPO_URL_DOCS     = "https://github.com/JanCorredor/MotorVideojuego";
	constexpr const char* REPO_URL_ISSUES   = "https://github.com/JanCorredor/MotorVideojuego/issues";
	constexpr const char* REPO_URL_RELEASES = "https://github.com/JanCorredor/MotorVideojuego/releases";

	// Fichero de layout. Nombre nuevo: la primera vez se crea la disposicion de Unity
	constexpr const char* LAYOUT_INI_FILE = "imgui_layout_unity.ini";
	constexpr const char* PROJECT_LAYOUT_INI_FILE = "ProjectSettings/EditorLayout.ini";
	constexpr const char* STREET_MODEL_ASSET = "Assets/Street/StreetEnvironment.fbx";

	void OpenWebURL(const char* url)
	{
		if (!SDL_OpenURL(url))
		{
			LOG_ERROR("[UI] Could not open URL '%s': %s", url, SDL_GetError());
		}
	}

	// -------------------- Seccion propia en el .ini: camara del editor --------------------
	void* EditorCameraReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char*)
	{
		return reinterpret_cast<void*>(1); // cualquier valor no nulo: hay una unica entrada
	}

	void EditorCameraReadLine(ImGuiContext*, ImGuiSettingsHandler* handler, void*, const char* line)
	{
		if (handler && handler->UserData) static_cast<ModuleUI*>(handler->UserData)->ReadEditorCameraSetting(line);
	}

	void EditorCameraWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer)
	{
		if (handler && handler->UserData) static_cast<ModuleUI*>(handler->UserData)->WriteEditorCameraSetting(buffer);
	}

	// Ultimo estado guardado (para marcar el .ini como modificado cuando la camara se mueve)
	CameraComponent::EditorViewState lastSavedView;
}

ModuleUI::ModuleUI() : Module()
{
	name = "UI";
}

ModuleUI::~ModuleUI()
{
}

bool ModuleUI::Awake()
{
	return true;
}

bool ModuleUI::Start()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	// Sin navegacion con teclado: las flechas las usan la Hierarchy y el Project, como en Unity
	// (Tab entre campos funciona igualmente)
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	// Layout por proyecto (ProjectSettings/EditorLayout.ini). Sin proyecto activo, el fichero de siempre
	if (Project::HasActive())
	{
		FileSystem::CreateDirectories(FileSystem::PROJECT_SETTINGS_DIR);
		layoutIniPath = FileSystem::ToDiskPath(PROJECT_LAYOUT_INI_FILE);
	}
	else
	{
		layoutIniPath = LAYOUT_INI_FILE;
	}
	io.IniFilename = layoutIniPath.c_str();

	// Las ventanas solo se mueven arrastrando su barra de titulo (arrastrar dentro de la vista
	// Scene es navegar, mover el gizmo o seleccionar con un rectangulo)
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	io.ConfigDockingWithShift = false;

	EditorTheme::LoadFonts();
	EditorTheme::ApplyUnityDark();
	RegisterSettingsHandler();

	ImGui_ImplSDL3_InitForOpenGL(Engine::GetInstance().window->GetWindow(), Engine::GetInstance().window->GetGLContext());
	ImGui_ImplOpenGL3_Init("#version 130");

	LOG("[ImGui] Dear ImGui %s initialized (SDL3 + OpenGL3, docking)", IMGUI_VERSION);

	const std::string& initTitle = Engine::GetInstance().window->GetTitle();
	strncpy_s(windowTitleBuffer, sizeof(windowTitleBuffer), initTitle.c_str(), _TRUNCATE);

	return true;
}

void ModuleUI::RegisterSettingsHandler()
{
	ImGuiSettingsHandler handler;
	handler.TypeName = "EditorCamera";
	handler.TypeHash = ImHashStr("EditorCamera");
	handler.ReadOpenFn = EditorCameraReadOpen;
	handler.ReadLineFn = EditorCameraReadLine;
	handler.WriteAllFn = EditorCameraWriteAll;
	handler.UserData = this;
	ImGui::AddSettingsHandler(&handler);
}

void ModuleUI::ReadEditorCameraSetting(const char* line)
{
	CameraComponent* camera = GetEditorCamera();
	if (camera == nullptr || line == nullptr) return;

	CameraComponent::EditorViewState state;
	int ortho = 0;
	float speed = camera->GetMovementSpeed();
	if (sscanf_s(line, "View=%f %f %f %f %f %f %d %f", &state.pivot.x, &state.pivot.y, &state.pivot.z,
		&state.distance, &state.yaw, &state.pitch, &ortho, &speed) >= 7)
	{
		state.orthographic = (ortho != 0);
		camera->SetEditorViewState(state);
		camera->SetMovementSpeed(speed);
		lastSavedView = state;
	}
}

void ModuleUI::WriteEditorCameraSetting(ImGuiTextBuffer* buffer)
{
	const CameraComponent* camera = GetEditorCamera();
	if (camera == nullptr || buffer == nullptr) return;
	const CameraComponent::EditorViewState state = camera->GetEditorViewState();
	buffer->appendf("[EditorCamera][SceneView]\n");
	buffer->appendf("View=%.4f %.4f %.4f %.4f %.3f %.3f %d %.3f\n\n", state.pivot.x, state.pivot.y, state.pivot.z,
		state.distance, state.yaw, state.pitch, state.orthographic ? 1 : 0, camera->GetMovementSpeed());
}

bool ModuleUI::PreUpdate()
{
#ifdef _DEBUG
	ZoneScoped;
#endif
	SDL_Window* sdlWindow = Engine::GetInstance().window->GetWindow();
	const bool relativeMouse = (sdlWindow != nullptr) && SDL_GetWindowRelativeMouseMode(sdlWindow);

	ImGuiIO& io = ImGui::GetIO();
	if (relativeMouse) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
	else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

	// Tinte del modo Play (como Unity): se aplica antes de empezar a dibujar el frame
	const Scene* scene = Engine::GetInstance().scene.get();
	EditorTheme::SetPlayModeTint(scene != nullptr && scene->IsInPlayMode());

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();

	return true;
}

bool ModuleUI::Update(float dt)
{
#ifdef _DEBUG
	ZoneScoped;
#endif
	(void)dt;

	// Operaciones sobre assets pedidas el frame anterior (borrar, reimportar...)
	ProcessPendingAssetOperations();
	TrackSceneChanges();

	const bool ret = DrawMainMenuBar();
	DrawToolbar();
	DrawDockSpaceAndLayout();

	sceneOrHierarchyFocused = false;
	hierarchyFocused = false;

	DrawScenePanel();
	DrawGamePanel();
	DrawHierarchyPanel();
	DrawInspectorPanel();
	DrawConfigurationPanel();
	DrawProjectPanel();
	DrawConsolePanel();
	DrawResourcesPanel();
	DrawDepthBufferPanel();
	DrawAboutPanel();
	DrawProjectSettingsPanel();
	DrawMaximizedGameWindow();
	UpdateWindowTitle();

	// Ventanas modales (fuera de cualquier ventana para que OpenPopup y BeginPopupModal coincidan)
	DrawSceneFilePopups();
	DrawAssetsPopups();

	HandleShortcuts();

	// La camara del editor se guarda en el .ini: se marca como modificado cuando se mueve
	if (const CameraComponent* camera = GetEditorCamera())
	{
		const CameraComponent::EditorViewState state = camera->GetEditorViewState();
		if (state.pivot != lastSavedView.pivot || state.distance != lastSavedView.distance || state.yaw != lastSavedView.yaw ||
			state.pitch != lastSavedView.pitch || state.orthographic != lastSavedView.orthographic)
		{
			lastSavedView = state;
			ImGui::MarkIniSettingsDirty();
		}
	}

	return ret;
}

void ModuleUI::TrackSceneChanges()
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;

	// Escena sustituida entera (cargar, nueva, Stop): el historial de Undo ya no sirve
	if (scene->GetSceneLoadVersion() != lastSceneLoadVersion)
	{
		lastSceneLoadVersion = scene->GetSceneLoadVersion();
		undo.Clear();
		renamingUID = 0;
	}

	const int playState = static_cast<int>(scene->GetPlayState());
	if (playState != lastPlayState)
	{
		const bool enteredPlay = (lastPlayState == static_cast<int>(PlayState::Stopped));
		lastPlayState = playState;
		if (enteredPlay)
		{
			undo.Clear();
			if (clearOnPlay)
			{
				LogSystem::GetInstance().Clear();
				selectedLogIndex = -1;
				consoleFilterDirty = true;
			}
		}
	}
}

// =====================================================================================
// DOCKSPACE Y LAYOUT POR DEFECTO (el "Default" de Unity)
//
//   +-------------+---------------------------+-------------+
//   | Hierarchy   |  Scene | Game             |  Inspector  |
//   |             |                           |  (Config.)  |
//   +-------------+---------------------------+             |
//   | Project | Console | Resources           |             |
//   +-----------------------------------------+-------------+
// =====================================================================================
void ModuleUI::DrawDockSpaceAndLayout()
{
	ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	// Sin el boton de cerrar de cada grupo de pestanas (las ventanas se cierran desde el menu Window)
	const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(ImGui::GetID("EditorDockSpace"), mainViewport, ImGuiDockNodeFlags_NoCloseButton);

	if (!layoutInitialized || resetLayout)
	{
		ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
		if (resetLayout || node == nullptr || node->ChildNodes[0] == nullptr)
		{
			BuildDefaultLayout(dockspaceId);
		}
		layoutInitialized = true;
		resetLayout = false;
	}
}

void ModuleUI::BuildDefaultLayout(unsigned int dockspaceId)
{
	ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, mainViewport->WorkSize);

	ImGuiID dockMain = dockspaceId;
	const ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.24f, nullptr, &dockMain);
	const ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.34f, nullptr, &dockMain);
	const ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.24f, nullptr, &dockMain);

	// En cada grupo la pestana activa es la ULTIMA que se acopla (Scene, Project e Inspector);
	// el orden de las pestanas es el orden en el que se dibujan las ventanas
	ImGui::DockBuilderDockWindow(EditorWindows::HIERARCHY, dockLeft);
	ImGui::DockBuilderDockWindow(EditorWindows::GAME, dockMain);
	ImGui::DockBuilderDockWindow(EditorWindows::SCENE, dockMain);
	ImGui::DockBuilderDockWindow(EditorWindows::RESOURCES, dockBottom);
	ImGui::DockBuilderDockWindow(EditorWindows::CONSOLE, dockBottom);
	ImGui::DockBuilderDockWindow(EditorWindows::PROJECT, dockBottom);
	ImGui::DockBuilderDockWindow(EditorWindows::CONFIGURATION, dockRight);
	ImGui::DockBuilderDockWindow(EditorWindows::INSPECTOR, dockRight);
	ImGui::DockBuilderFinish(dockspaceId);

	showScene = showGame = showHierarchy = showInspector = showConsole = showConfiguration = showProject = showResources = true;
}

// =====================================================================================
// MENU PRINCIPAL
// =====================================================================================
bool ModuleUI::DrawMainMenuBar()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	bool continueRunning = true;
	Scene* scene = Engine::GetInstance().scene.get();
	const bool playing = scene && scene->IsInPlayMode();
	GameObject* active = scene ? scene->GetSelected() : nullptr;
	const bool hasSelection = scene && scene->HasSelection();

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
	if (ImGui::BeginMainMenuBar())
	{
		// ----------------------------------- File -----------------------------------
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene", "Ctrl+N") && scene) scene->RequestNewScene();
			if (ImGui::BeginMenu("Open Scene"))
			{
				std::vector<std::string> files;
				FileSystem::ListFilesRecursive(FileSystem::ASSETS_DIR, files);
				bool anyScene = false;
				for (const std::string& file : files)
				{
					if (!ResourceManager::IsSceneFile(file)) continue;
					anyScene = true;
					if (ImGui::MenuItem(file.c_str()) && scene) scene->RequestLoadScene(file);
				}
				if (!anyScene) ImGui::TextDisabled("No scenes saved in Assets");
				ImGui::EndMenu();
			}
			if (FileSystem::Exists(STREET_MODEL_ASSET) && ImGui::MenuItem("Load Street Environment") && scene) scene->RequestDefaultScene();

			ImGui::Separator();
			if (ImGui::MenuItem("Save", "Ctrl+S", false, !playing) && scene)
			{
				if (scene->GetCurrentScenePath().empty()) openSaveScenePopup = true;
				else scene->SaveScene(scene->GetCurrentScenePath());
			}
			if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !playing)) openSaveScenePopup = true;
			if (playing) ImGui::TextDisabled("(Stop Play mode to save)");

			ImGui::Separator();
			if (ImGui::MenuItem(ICON_FA_GEAR "  Project Settings...", nullptr, false, Project::HasActive())) showProjectSettings = true;
			if (ImGui::MenuItem(ICON_FA_CUBES "  Open Hub...", nullptr, false, !playing))
			{
				// El Hub es otra instancia del ejecutable: se lanza y este editor se cierra
				if (EditorPlatform::LaunchSelf({ "--no-splash" })) continueRunning = false;
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Exit", "Alt+F4")) continueRunning = false;
			ImGui::EndMenu();
		}

		// ----------------------------------- Edit -----------------------------------
		if (ImGui::BeginMenu("Edit"))
		{
			const std::string undoLabel = undo.CanUndo() ? "Undo " + undo.GetUndoName() : std::string("Undo");
			const std::string redoLabel = undo.CanRedo() ? "Redo " + undo.GetRedoName() : std::string("Redo");
			if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, undo.CanUndo())) undo.Undo();
			if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, undo.CanRedo())) undo.Redo();
			ImGui::Separator();
			if (ImGui::MenuItem("Select All", "Ctrl+A") && scene) scene->SelectAll();
			if (ImGui::MenuItem("Deselect All", "Shift+D", false, hasSelection) && scene) scene->ClearSelection();
			ImGui::Separator();
			if (ImGui::MenuItem("Rename", "F2", false, active != nullptr)) BeginRename(active);
			if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection)) DuplicateSelection();
			if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) DeleteSelection();
			ImGui::Separator();
			if (ImGui::MenuItem("Frame Selected", "F", false, hasSelection)) FrameSelection();
			ImGui::Separator();
			if (ImGui::MenuItem(playing ? "Stop" : "Play", "Ctrl+P") && scene)
			{
				if (playing) scene->Stop();
				else scene->Play();
			}
			if (ImGui::MenuItem("Pause", "Ctrl+Shift+P", scene && scene->GetPlayState() == PlayState::Paused, playing) && scene) scene->Pause();
			if (ImGui::MenuItem("Step", "Ctrl+Alt+P", false, scene && scene->GetPlayState() == PlayState::Paused) && scene) scene->Step();
			ImGui::Separator();
			if (ImGui::MenuItem("Preferences...")) showConfiguration = true;
			ImGui::EndMenu();
		}

		// ----------------------------------- Assets -----------------------------------
		if (ImGui::BeginMenu("Assets"))
		{
			if (ImGui::BeginMenu("Create"))
			{
				if (ImGui::MenuItem(ICON_FA_FOLDER "  Folder")) openNewFolderPopup = true;
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Show in Explorer")) RevealInExplorer(selectedAssetPath.empty() ? assetsCurrentFolder : selectedAssetPath);
			ImGui::Separator();
			if (ImGui::MenuItem("Refresh", "Ctrl+R")) QueueAssetOperation(AssetOperation::Refresh, std::string());
			if (ImGui::MenuItem("Reimport", nullptr, false, !selectedAssetPath.empty() && ResourceManager::IsImportable(selectedAssetPath)))
			{
				QueueAssetOperation(AssetOperation::Reimport, selectedAssetPath);
			}
			ImGui::EndMenu();
		}

		// ----------------------------------- GameObject -----------------------------------
		if (ImGui::BeginMenu("GameObject"))
		{
			DrawCreateMenuItems(nullptr);
			if (ImGui::MenuItem("Create Empty Child", "Alt+Shift+N", false, active != nullptr) && scene && active)
			{
				CreateObject("Create Empty Child", [scene, active]() { return scene->CreateEmptyGameObject(active); });
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Align With View", "Ctrl+Shift+F", false, hasSelection)) AlignWithView();
			if (ImGui::MenuItem("Move To View", "Ctrl+Alt+F", false, hasSelection)) MoveToView();
			if (ImGui::MenuItem("Align View to Selected", nullptr, false, active != nullptr)) AlignViewToSelected();
			ImGui::EndMenu();
		}

		// ----------------------------------- Component -----------------------------------
		if (ImGui::BeginMenu("Component"))
		{
			const bool canAdd = active != nullptr;
			if (ImGui::MenuItem(ICON_FA_CUBE "  Mesh Renderer", nullptr, false, canAdd && !active->GetComponent<MeshComponent>()))
			{
				undo.RecordSnapshot("Add Component");
				active->AddComponent(std::make_unique<MeshComponent>(active));
			}
			if (ImGui::MenuItem(ICON_FA_PALETTE "  Material", nullptr, false, canAdd && !active->GetComponent<MaterialComponent>()))
			{
				undo.RecordSnapshot("Add Component");
				active->AddComponent(std::make_unique<MaterialComponent>(active));
			}
			if (ImGui::MenuItem(ICON_FA_VIDEO "  Camera", nullptr, false, canAdd && !active->GetComponent<CameraComponent>()))
			{
				undo.RecordSnapshot("Add Component");
				active->AddComponent(std::make_unique<CameraComponent>(active));
				if (scene && scene->GetGameCameraObject() == nullptr) scene->SetGameCamera(active);
			}
			ImGui::EndMenu();
		}

		// ----------------------------------- Window -----------------------------------
		if (ImGui::BeginMenu("Window"))
		{
			if (ImGui::BeginMenu("Layouts"))
			{
				if (ImGui::MenuItem("Reset Default Layout")) resetLayout = true;
				ImGui::EndMenu();
			}
			ImGui::Separator();
			ImGui::MenuItem(ICON_FA_TABLE_CELLS "  Scene", "Ctrl+1", &showScene);
			ImGui::MenuItem(ICON_FA_GAMEPAD "  Game", "Ctrl+2", &showGame);
			ImGui::MenuItem(ICON_FA_INFO "  Inspector", "Ctrl+3", &showInspector);
			ImGui::MenuItem(ICON_FA_LIST "  Hierarchy", "Ctrl+4", &showHierarchy);
			ImGui::MenuItem(ICON_FA_FOLDER "  Project", "Ctrl+5", &showProject);
			ImGui::MenuItem(ICON_FA_TERMINAL "  Console", "Ctrl+Shift+C", &showConsole);
			ImGui::Separator();
			ImGui::MenuItem(ICON_FA_DATABASE "  Resources", nullptr, &showResources);
			ImGui::MenuItem(ICON_FA_GEAR "  Configuration", nullptr, &showConfiguration);
			if (Render* render = Engine::GetInstance().render.get())
			{
				RenderSettings rs = render->GetSettings();
				bool showDepth = rs.showDepthBuffer;
				if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Z-Buffer Visualizer", nullptr, &showDepth))
				{
					rs.showDepthBuffer = showDepth;
					render->SetSettings(rs);
				}
			}
			ImGui::EndMenu();
		}

		// ----------------------------------- Help -----------------------------------
		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("GitHub Documentation")) OpenWebURL(REPO_URL_DOCS);
			if (ImGui::MenuItem("Report a Bug")) OpenWebURL(REPO_URL_ISSUES);
			if (ImGui::MenuItem("Download Latest")) OpenWebURL(REPO_URL_RELEASES);
			ImGui::Separator();
			ImGui::MenuItem("About " ENGINE_NAME, nullptr, &showAbout);
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
	ImGui::PopStyleVar();

	return continueRunning;
}

void ModuleUI::DrawCreateMenuItems(GameObject* parent)
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;

	// Los objetos nuevos aparecen en el pivote de la vista Scene (como Unity), o en el padre
	const auto placeAtPivot = [this, parent](GameObject* object)
	{
		if (object == nullptr || parent != nullptr) return;
		const CameraComponent* camera = GetEditorCamera();
		TransformComponent* transform = object->GetComponent<TransformComponent>();
		if (camera && transform) transform->SetPosition(camera->GetPivot());
	};

	if (ImGui::MenuItem("Create Empty", parent ? nullptr : "Ctrl+Shift+N"))
	{
		CreateObject("Create Empty", [&]() { GameObject* o = scene->CreateEmptyGameObject(parent); placeAtPivot(o); return o; });
	}
	if (ImGui::BeginMenu("3D Object"))
	{
		const PrimitiveType types[] = { PrimitiveType::Cube, PrimitiveType::Sphere, PrimitiveType::Plane, PrimitiveType::Cylinder, PrimitiveType::Pyramid };
		for (PrimitiveType type : types)
		{
			if (ImGui::MenuItem(GeometryGenerator::GetName(type)))
			{
				CreateObject(std::string("Create ") + GeometryGenerator::GetName(type),
					[&]() { GameObject* o = scene->CreatePrimitive(type, parent); placeAtPivot(o); return o; });
			}
		}
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Camera"))
	{
		CreateObject("Create Camera", [&]() { GameObject* o = scene->CreateCamera(parent); placeAtPivot(o); return o; });
	}
}

// =====================================================================================
// TOOLBAR (Play / Pause / Step en el centro, como Unity 6)
// =====================================================================================
void ModuleUI::DrawToolbar()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	Scene* scene = Engine::GetInstance().scene.get();
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiStyle& style = ImGui::GetStyle();
	const float buttonHeight = ImGui::GetFrameHeight() + 2.0f;
	const float height = buttonHeight + 10.0f;
	const PlayState state = scene ? scene->GetPlayState() : PlayState::Stopped;

	ImGui::PushStyleColor(ImGuiCol_WindowBg, EditorPalette::TOOLBAR_BG);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 5.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
	if (ImGui::BeginViewportSideBar("##MainToolbar", viewport, ImGuiDir_Up, height, flags) && scene)
	{
		// ----- Izquierda: escena abierta -----
		const std::string& scenePath = scene->GetCurrentScenePath();
		ImGui::SetCursorPosY((height - ImGui::GetFontSize()) * 0.5f);
		ImGui::TextColored(EditorPalette::ICON_SCENE, ICON_FA_CLAPPERBOARD);
		ImGui::SameLine();
		ImGui::TextUnformatted(scenePath.empty() ? "Untitled" : FileSystem::GetFileName(scenePath).c_str());
		if (undo.CanUndo())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("*");
			ImGui::SetItemTooltip("Unsaved changes");
		}

		// ----- Centro: Play / Pause / Step como un unico grupo -----
		// Cada boton se coloca en una posicion absoluta: con SameLine la linea de base del texto de la
		// izquierda desplazaba el primero hacia arriba y el grupo no quedaba alineado ni centrado
		const ImVec2 buttonSize(std::floor(buttonHeight * 1.45f), buttonHeight);
		const float buttonGap = 1.0f;
		const float groupWidth = buttonSize.x * 3.0f + buttonGap * 2.0f;
		const float groupX = std::floor((ImGui::GetWindowWidth() - groupWidth) * 0.5f);
		const float groupY = std::floor((height - buttonHeight) * 0.5f);
		const auto placeButton = [&](int index)
		{
			ImGui::SetCursorPos(ImVec2(groupX + index * (buttonSize.x + buttonGap), groupY));
		};

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.28f, 0.28f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.33f, 0.33f, 0.33f, 1.0f));

		const bool inPlay = (state != PlayState::Stopped);
		placeButton(0);
		if (ToggleButton(ICON_FA_PLAY "##Play", inPlay, buttonSize))
		{
			if (inPlay) scene->Stop();
			else scene->Play();
		}
		IconTooltip(inPlay ? "Stop (Ctrl+P)" : "Play (Ctrl+P)");

		placeButton(1);
		if (ToggleButton(ICON_FA_PAUSE "##Pause", state == PlayState::Paused, buttonSize))
		{
			if (state == PlayState::Stopped) { scene->Play(); scene->Pause(); }
			else scene->Pause();
		}
		IconTooltip("Pause (Ctrl+Shift+P)");

		placeButton(2);
		ImGui::BeginDisabled(state != PlayState::Paused);
		if (ImGui::Button(ICON_FA_FORWARD_STEP "##Step", buttonSize)) scene->Step();
		ImGui::EndDisabled();
		IconTooltip("Step (Ctrl+Alt+P)");

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(3);

		// ----- Derecha: tiempo de juego, escala de tiempo y menu de Layout -----
		const float layoutWidth = ImGui::CalcTextSize("Layout").x + 46.0f;
		const float timeWidth = 150.0f;
		ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - layoutWidth - timeWidth - style.WindowPadding.x - style.ItemSpacing.x,
			std::floor((height - ImGui::GetFrameHeight()) * 0.5f)));
		if (inPlay)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled(ICON_FA_CLOCK " %.2fs", scene->GetGameTime());
			ImGui::SameLine();
		}
		else
		{
			ImGui::Dummy(ImVec2(70.0f, 1.0f));
			ImGui::SameLine();
		}
		float timeScale = scene->GetTimeScale();
		ImGui::SetNextItemWidth(timeWidth - 80.0f);
		if (ImGui::DragFloat("##TimeScale", &timeScale, 0.01f, 0.0f, 10.0f, "x%.2f")) scene->SetTimeScale(timeScale);
		ImGui::SetItemTooltip("Time Scale");

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_TABLE_COLUMNS "  Layout  " ICON_FA_CARET_DOWN "##LayoutMenu")) ImGui::OpenPopup("LayoutPopup");
		if (ImGui::BeginPopup("LayoutPopup"))
		{
			if (ImGui::MenuItem("Default")) resetLayout = true;
			ImGui::Separator();
			if (ImGui::MenuItem("Reset All Layouts")) resetLayout = true;
			ImGui::EndPopup();
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
}

void ModuleUI::DrawSceneFilePopups()
{
	Scene* scene = Engine::GetInstance().scene.get();

	if (openSaveScenePopup)
	{
		ImGui::OpenPopup("Save Scene");
		openSaveScenePopup = false;
	}
	if (ImGui::BeginPopupModal("Save Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("The scene will be saved in %s/", FileSystem::SCENES_DIR);
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		const bool enterPressed = ImGui::InputText("Name", saveSceneName, sizeof(saveSceneName), ImGuiInputTextFlags_EnterReturnsTrue);

		const std::string sceneName = saveSceneName;
		const bool validName = !sceneName.empty() && sceneName.find_first_of("/\\:*?\"<>|") == std::string::npos;

		ImGui::BeginDisabled(!validName || scene == nullptr);
		if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)) || (enterPressed && validName && scene != nullptr))
		{
			scene->SaveScene(FileSystem::Join(FileSystem::SCENES_DIR, sceneName + FileSystem::SCENE_EXTENSION));
			InvalidateAssetsCache();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// =====================================================================================
// ATAJOS DE TECLADO (los de Unity)
// =====================================================================================
void ModuleUI::HandleShortcuts()
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput) return;

	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;

	const bool ctrl = io.KeyCtrl;
	const bool shift = io.KeyShift;
	const bool alt = io.KeyAlt;
	const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };

	CameraComponent* camera = GetEditorCamera();
	const bool flyingCamera = (camera != nullptr && camera->IsControllingCamera());

	// ----- Globales -----
	if (ctrl && pressed(ImGuiKey_S) && !scene->IsInPlayMode())
	{
		if (shift || scene->GetCurrentScenePath().empty()) openSaveScenePopup = true;
		else scene->SaveScene(scene->GetCurrentScenePath());
	}
	if (ctrl && !shift && pressed(ImGuiKey_N) && !scene->IsInPlayMode()) scene->RequestNewScene();
	if (ctrl && pressed(ImGuiKey_P))
	{
		if (alt) scene->Step();
		else if (shift) scene->Pause();
		else if (scene->IsInPlayMode()) scene->Stop();
		else scene->Play();
	}
	if (ctrl && !shift && pressed(ImGuiKey_Z)) undo.Undo();
	if (ctrl && ((shift && pressed(ImGuiKey_Z)) || pressed(ImGuiKey_Y))) undo.Redo();
	if (ctrl && pressed(ImGuiKey_R)) QueueAssetOperation(AssetOperation::Refresh, std::string());

	// Ctrl+1..5: mostrar ventanas
	if (ctrl && !shift && pressed(ImGuiKey_1)) { showScene = true; ImGui::SetWindowFocus(EditorWindows::SCENE); }
	if (ctrl && !shift && pressed(ImGuiKey_2)) { showGame = true; ImGui::SetWindowFocus(EditorWindows::GAME); }
	if (ctrl && !shift && pressed(ImGuiKey_3)) { showInspector = true; ImGui::SetWindowFocus(EditorWindows::INSPECTOR); }
	if (ctrl && !shift && pressed(ImGuiKey_4)) { showHierarchy = true; ImGui::SetWindowFocus(EditorWindows::HIERARCHY); }
	if (ctrl && !shift && pressed(ImGuiKey_5)) { showProject = true; ImGui::SetWindowFocus(EditorWindows::PROJECT); }
	if (ctrl && shift && pressed(ImGuiKey_C)) { showConsole = true; ImGui::SetWindowFocus(EditorWindows::CONSOLE); }

	// ----- Crear objetos -----
	if (ctrl && shift && pressed(ImGuiKey_N))
	{
		CreateObject("Create Empty", [this, scene]()
		{
			GameObject* object = scene->CreateEmptyGameObject();
			const CameraComponent* cam = GetEditorCamera();
			if (object && cam) object->GetComponent<TransformComponent>()->SetPosition(cam->GetPivot());
			return object;
		});
	}
	if (alt && shift && pressed(ImGuiKey_N) && scene->GetSelected())
	{
		GameObject* parent = scene->GetSelected();
		CreateObject("Create Empty Child", [scene, parent]() { return scene->CreateEmptyGameObject(parent); });
	}

	// ----- Escena / Hierarchy (con el raton encima de Scene o con el foco en Scene / Hierarchy) -----
	const bool sceneContext = sceneViewHovered || sceneOrHierarchyFocused;
	if (!sceneContext || flyingCamera) return;

	if (ctrl && pressed(ImGuiKey_D)) DuplicateSelection();
	if (pressed(ImGuiKey_Delete) || (pressed(ImGuiKey_Backspace) && ctrl)) DeleteSelection();
	if (ctrl && pressed(ImGuiKey_A)) scene->SelectAll();
	if (shift && !ctrl && pressed(ImGuiKey_D)) scene->ClearSelection();
	if (ctrl && shift && pressed(ImGuiKey_F)) AlignWithView();
	if (ctrl && alt && pressed(ImGuiKey_F)) MoveToView();
	if (!ctrl && !alt && !shift && pressed(ImGuiKey_F)) FrameSelection();
	if (pressed(ImGuiKey_F2) && scene->GetSelected()) BeginRename(scene->GetSelected());

	// Herramientas Q W E R Y y Pivot / Global (Z / X). Nunca a mitad de un arrastre del gizmo.
	if (!gizmoInUse && !ctrl && !alt && !shift)
	{
		if (pressed(ImGuiKey_Q)) currentTool = Tool::View;
		if (pressed(ImGuiKey_W)) currentTool = Tool::Move;
		if (pressed(ImGuiKey_E)) currentTool = Tool::Rotate;
		if (pressed(ImGuiKey_R)) currentTool = Tool::Scale;
		if (pressed(ImGuiKey_Y)) currentTool = Tool::Transform;
		if (pressed(ImGuiKey_X)) gizmoSpace = (gizmoSpace == GizmoSpace::Local) ? GizmoSpace::World : GizmoSpace::Local;
		if (pressed(ImGuiKey_Z)) pivotMode = (pivotMode == PivotMode::Pivot) ? PivotMode::Center : PivotMode::Pivot;
	}
}

// =====================================================================================
// OPERACIONES DE EDICION
// =====================================================================================
CameraComponent* ModuleUI::GetEditorCamera() const
{
	Scene* scene = Engine::GetInstance().scene.get();
	GameObject* cameraObject = scene ? scene->GetEditorCameraObject() : nullptr;
	return cameraObject ? cameraObject->GetComponent<CameraComponent>() : nullptr;
}

void ModuleUI::SelectGameObject(GameObject* object)
{
	if (Scene* scene = Engine::GetInstance().scene.get()) scene->Select(object);
	inspectorShowsAsset = false;
}

void ModuleUI::ClickSelect(GameObject* object)
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;
	const ImGuiIO& io = ImGui::GetIO();
	if (io.KeyCtrl || io.KeyShift)
	{
		if (object) scene->ToggleSelection(object);
	}
	else
	{
		scene->Select(object);
	}
	inspectorShowsAsset = false;
}

GameObject* ModuleUI::CreateObject(const std::string& undoName, const std::function<GameObject*()>& creator)
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return nullptr;
	undo.RecordSnapshot(undoName);
	GameObject* object = creator();
	if (object)
	{
		SelectGameObject(object);
		BeginRename(object);
	}
	return object;
}

void ModuleUI::DeleteSelection()
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;
	const std::vector<GameObject*> objects = scene->GetTopLevelSelection();
	if (objects.empty()) return;

	undo.RecordSnapshot(objects.size() == 1 ? "Delete " + objects[0]->GetName() : std::string("Delete Objects"));
	for (GameObject* object : objects) scene->DestroyGameObject(object);
	scene->ClearSelection();
}

void ModuleUI::DuplicateSelection()
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;
	const std::vector<GameObject*> objects = scene->GetTopLevelSelection();
	if (objects.empty()) return;

	undo.RecordSnapshot("Duplicate");
	std::vector<GameObject*> duplicates;
	GameObject* activeDuplicate = nullptr;
	for (GameObject* object : objects)
	{
		const int index = object->GetSiblingIndex();
		GameObject* duplicate = scene->DuplicateGameObject(object);
		if (duplicate == nullptr) continue;
		duplicate->SetSiblingIndex(index + 1); // justo debajo del original, como en Unity
		duplicates.push_back(duplicate);
		if (object == scene->GetSelected() || activeDuplicate == nullptr) activeDuplicate = duplicate;
	}
	scene->SetSelection(duplicates, activeDuplicate);
	inspectorShowsAsset = false;
}

void ModuleUI::FrameSelection()
{
	Scene* scene = Engine::GetInstance().scene.get();
	CameraComponent* camera = GetEditorCamera();
	if (scene == nullptr || camera == nullptr || !scene->HasSelection()) return;

	AABB bounds;
	for (GameObject* object : scene->GetSelection())
	{
		const AABB box = object->GetWorldAABB(true);
		if (box.IsValid()) bounds.Enclose(box);
		else
		{
			const glm::vec3 position = glm::vec3(object->GetWorldMatrix()[3]);
			bounds.Enclose(AABB(position - glm::vec3(0.5f), position + glm::vec3(0.5f)));
		}
	}
	camera->FrameBounds(bounds, true);
}

void ModuleUI::AlignWithView()
{
	Scene* scene = Engine::GetInstance().scene.get();
	const CameraComponent* camera = GetEditorCamera();
	if (scene == nullptr || camera == nullptr) return;
	const std::vector<GameObject*> objects = scene->GetTopLevelSelection();
	if (objects.empty()) return;

	// Matriz de mundo con la posicion y la orientacion de la vista (el -Z del objeto mira hacia donde mira la vista)
	glm::mat4 viewWorld(1.0f);
	viewWorld[0] = glm::vec4(camera->GetRight(), 0.0f);
	viewWorld[1] = glm::vec4(camera->GetUp(), 0.0f);
	viewWorld[2] = glm::vec4(-camera->GetFront(), 0.0f);
	viewWorld[3] = glm::vec4(camera->GetWorldPosition(), 1.0f);

	undo.BeginTransform(objects, "Align With View");
	for (GameObject* object : objects)
	{
		TransformComponent* transform = object->GetComponent<TransformComponent>();
		if (transform == nullptr) continue;
		const GameObject* parent = object->GetParent();
		const glm::mat4 parentWorld = parent ? parent->GetWorldMatrix() : glm::mat4(1.0f);
		const glm::vec3 scale = transform->GetScale();
		const glm::mat4 world = viewWorld * glm::scale(glm::mat4(1.0f), scale);
		glm::vec3 position, rotation, newScale;
		if (transform->DecomposeLocalMatrix(glm::inverse(parentWorld) * world, position, rotation, newScale))
		{
			transform->SetPosition(position);
			transform->SetRotation(rotation);
		}
	}
	undo.EndTransform();
}

void ModuleUI::MoveToView()
{
	Scene* scene = Engine::GetInstance().scene.get();
	const CameraComponent* camera = GetEditorCamera();
	if (scene == nullptr || camera == nullptr) return;
	const std::vector<GameObject*> objects = scene->GetTopLevelSelection();
	if (objects.empty()) return;

	undo.BeginTransform(objects, "Move To View");
	for (GameObject* object : objects)
	{
		TransformComponent* transform = object->GetComponent<TransformComponent>();
		if (transform == nullptr) continue;
		const GameObject* parent = object->GetParent();
		const glm::mat4 parentWorld = parent ? parent->GetWorldMatrix() : glm::mat4(1.0f);
		transform->SetPosition(glm::vec3(glm::inverse(parentWorld) * glm::vec4(camera->GetPivot(), 1.0f)));
	}
	undo.EndTransform();
}

void ModuleUI::AlignViewToSelected()
{
	Scene* scene = Engine::GetInstance().scene.get();
	CameraComponent* camera = GetEditorCamera();
	GameObject* active = scene ? scene->GetSelected() : nullptr;
	if (camera == nullptr || active == nullptr) return;

	const glm::mat4 world = active->GetWorldMatrix();
	const glm::vec3 position = glm::vec3(world[3]);
	const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
	camera->LookAtFrom(position, position + forward * std::max(camera->GetOrbitDistance(), 1.0f));
}

// =====================================================================================
// RENDER DE LA UI
// =====================================================================================
bool ModuleUI::PostUpdate()
{
#ifdef _DEBUG
	ZoneScoped;
#endif
	RenderUI();
	return true;
}

bool ModuleUI::CleanUp()
{
	LOG("[ImGui] Shutting down Dear ImGui...");
	ReleaseThumbnails();
	consoleViewCache.clear();
	consoleViewCache.shrink_to_fit();

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	return true;
}

void ModuleUI::RenderUI()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		SDL_Window* backupWindow = SDL_GL_GetCurrentWindow();
		SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		SDL_GL_MakeCurrent(backupWindow, backupContext);
	}
}

// =====================================================================================
// CONSOLE (estilo Unity: barra con Clear / Collapse / Clear on Play, buscador y contadores)
// =====================================================================================
void ModuleUI::DrawConsolePanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!showConsole) return;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin(EditorWindows::CONSOLE, &showConsole);
	ImGui::PopStyleVar();
	if (!open)
	{
		ImGui::End();
		return;
	}

	LogSystem& logSys = LogSystem::GetInstance();

	// ---------------------------------- Barra superior ----------------------------------
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorPalette::PANEL_TOOLBAR);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
	ImGui::BeginChild("ConsoleToolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + 4.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	{
		if (FlatButton("Clear##ConsoleClear"))
		{
			logSys.Clear();
			selectedLogIndex = -1;
			consoleFilterDirty = true;
		}
		ImGui::SameLine(0.0f, 0.0f);
		if (FlatButton(ICON_FA_CARET_DOWN "##ConsoleClearMenu")) ImGui::OpenPopup("ConsoleClearPopup");
		if (ImGui::BeginPopup("ConsoleClearPopup"))
		{
			ImGui::MenuItem("Clear on Play", nullptr, &clearOnPlay);
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		if (FlatButton("Collapse##ConsoleCollapse", ImVec2(0, 0), collapseLogs))
		{
			collapseLogs = !collapseLogs;
			consoleFilterDirty = true;
		}
		ImGui::SameLine();
		if (FlatButton("Auto-scroll##ConsoleAutoScroll", ImVec2(0, 0), autoScroll)) autoScroll = !autoScroll;

		// Contadores (derecha) y buscador
		const std::string infoLabel = std::string(ICON_FA_CIRCLE_INFO " ") + std::to_string(logSys.GetInfoCount()) + "###ConsoleInfo";
		const std::string warnLabel = std::string(ICON_FA_TRIANGLE_EXCLAMATION " ") + std::to_string(logSys.GetWarningCount()) + "###ConsoleWarn";
		const std::string errLabel = std::string(ICON_FA_CIRCLE_XMARK " ") + std::to_string(logSys.GetErrorCount()) + "###ConsoleError";
		const ImGuiStyle& style = ImGui::GetStyle();
		const auto buttonWidth = [&](const std::string& label) { return ImGui::CalcTextSize(label.c_str(), nullptr, true).x + style.FramePadding.x * 2.0f; };
		const float countersWidth = buttonWidth(infoLabel) + buttonWidth(warnLabel) + buttonWidth(errLabel) + style.ItemSpacing.x * 2.0f;
		const float searchWidth = std::min(240.0f, std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.35f));
		ImGui::SameLine(ImGui::GetWindowWidth() - countersWidth - searchWidth - style.ItemSpacing.x - 6.0f);
		ImGui::SetNextItemWidth(searchWidth);
		if (ImGui::InputTextWithHint("##ConsoleSearch", ICON_FA_MAGNIFYING_GLASS " Search", consoleFilter, sizeof(consoleFilter)))
		{
			consoleFilterDirty = true;
		}

		const auto counterButton = [&](const std::string& label, bool& enabled, const ImVec4& color)
		{
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, enabled ? color : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			if (FlatButton(label.c_str(), ImVec2(0, 0), enabled))
			{
				enabled = !enabled;
				consoleFilterDirty = true;
			}
			ImGui::PopStyleColor();
		};
		counterButton(infoLabel, showInfo, ImGui::GetStyleColorVec4(ImGuiCol_Text));
		counterButton(warnLabel, showWarnings, EditorPalette::LEVEL_WARNING);
		counterButton(errLabel, showErrors, EditorPalette::LEVEL_ERROR);
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();

	// ---------------------- Lista de mensajes (cache de indices visibles) ----------------------
	const uint64_t currentRev = logSys.GetRevision();
	if (currentRev != lastConsoleRevision || consoleFilterDirty)
	{
		if (currentRev != lastConsoleRevision && autoScroll) scrollToBottom = true;
		lastConsoleRevision = currentRev;
		consoleFilterDirty = false;

		consoleViewCache.clear();
		const std::string searchLower = ToLower(consoleFilter);
		std::string prevText;
		LogType prevType = LogType::Info;

		logSys.ForEach([&](int index, const LogEntry& entry)
		{
			if (entry.type == LogType::Info && !showInfo) return;
			if (entry.type == LogType::Warning && !showWarnings) return;
			if (entry.type == LogType::Error && !showErrors) return;
			if (!ContainsCaseInsensitive(entry.text, searchLower)) return;

			if (collapseLogs && !consoleViewCache.empty() && entry.type == prevType && entry.text == prevText)
			{
				consoleViewCache.back().collapsedCount++;
				return;
			}
			consoleViewCache.push_back({ index, 1 });
			prevText = entry.text;
			prevType = entry.type;
		});
	}

	LogEntry selectedEntry;
	const bool hasSelectedEntry = (selectedLogIndex >= 0) && logSys.GetEntry(selectedLogIndex, selectedEntry);
	if (!hasSelectedEntry) selectedLogIndex = -1;

	const float detailsHeight = hasSelectedEntry ? 90.0f : 0.0f;
	const float listHeight = ImGui::GetContentRegionAvail().y - detailsHeight;

	ImGui::BeginChild("LogScrollRegion", ImVec2(0, listHeight), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	{
		const float rowHeight = ImGui::GetTextLineHeight() + 8.0f;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		LogEntry entry;
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(consoleViewCache.size()), rowHeight);
		while (clipper.Step())
		{
			for (int viewIdx = clipper.DisplayStart; viewIdx < clipper.DisplayEnd; ++viewIdx)
			{
				const ConsoleDisplayItem& item = consoleViewCache[viewIdx];
				if (!logSys.GetEntry(item.logIndex, entry)) continue;

				ImVec4 iconColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
				const char* icon = ICON_FA_CIRCLE_INFO;
				if (entry.type == LogType::Warning) { iconColor = EditorPalette::LEVEL_WARNING; icon = ICON_FA_TRIANGLE_EXCLAMATION; }
				if (entry.type == LogType::Error) { iconColor = EditorPalette::LEVEL_ERROR; icon = ICON_FA_CIRCLE_XMARK; }

				ImGui::PushID(item.logIndex);
				const ImVec2 rowPos = ImGui::GetCursorScreenPos();
				const float rowWidth = std::max(ImGui::GetContentRegionAvail().x, ImGui::GetWindowWidth());
				// Filas alternas como en Unity
				if (viewIdx % 2 == 1)
				{
					drawList->AddRectFilled(rowPos, ImVec2(rowPos.x + rowWidth, rowPos.y + rowHeight), IM_COL32(255, 255, 255, 6));
				}

				const bool isSelected = (selectedLogIndex == item.logIndex);
				if (ImGui::Selectable("##row", isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, rowHeight)))
				{
					selectedLogIndex = isSelected ? -1 : item.logIndex;
				}
				const float textY = rowPos.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
				drawList->AddText(ImVec2(rowPos.x + 8.0f, textY), ImGui::GetColorU32(iconColor), icon);
				const std::string line = "[" + entry.timestamp + "] " + entry.text;
				drawList->AddText(ImVec2(rowPos.x + 30.0f, textY), ImGui::GetColorU32(ImGuiCol_Text), line.c_str());

				if (collapseLogs && item.collapsedCount > 1)
				{
					const std::string count = std::to_string(item.collapsedCount);
					const ImVec2 countSize = ImGui::CalcTextSize(count.c_str());
					const float badgeRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - 8.0f;
					const ImVec2 badgeMin(badgeRight - countSize.x - 12.0f, textY - 1.0f);
					const ImVec2 badgeMax(badgeRight, textY + countSize.y + 1.0f);
					drawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(90, 90, 90, 255), 8.0f);
					drawList->AddText(ImVec2(badgeMin.x + 6.0f, textY), ImGui::GetColorU32(ImGuiCol_Text), count.c_str());
				}
				ImGui::PopID();
			}
		}

		if (scrollToBottom || (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
		{
			ImGui::SetScrollHereY(1.0f);
			scrollToBottom = false;
		}
	}
	ImGui::EndChild();

	// ---------------------- Detalle del mensaje seleccionado ----------------------
	if (hasSelectedEntry)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.15f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
		ImGui::BeginChild("LogDetailsRegion", ImVec2(0, detailsHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::TextWrapped("%s", selectedEntry.text.c_str());
		if (!selectedEntry.file.empty()) ImGui::TextDisabled("%s:%d", selectedEntry.file.c_str(), selectedEntry.line);
		if (ImGui::SmallButton(ICON_FA_COPY " Copy"))
		{
			std::string fullText = "[" + selectedEntry.timestamp + "] " + selectedEntry.text;
			if (!selectedEntry.file.empty()) fullText += " (" + selectedEntry.file + ":" + std::to_string(selectedEntry.line) + ")";
			ImGui::SetClipboardText(fullText.c_str());
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}

	ImGui::End();
}

// =====================================================================================
// CONFIGURATION (preferencias del motor)
// =====================================================================================
void ModuleUI::DrawConfigurationPanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!showConfiguration) return;

	if (ImGui::Begin(EditorWindows::CONFIGURATION, &showConfiguration))
	{
		if (ImGui::BeginTabBar("ConfigurationTabBar", ImGuiTabBarFlags_FittingPolicyScroll))
		{
			// ------------------------------- Application -------------------------------
			if (ImGui::BeginTabItem("Application"))
			{
				const auto& perf = Engine::GetInstance().GetPerformanceData();
				ImGui::Text("Engine: MotorVideojuego 3D");
				ImGui::BulletText("Uptime: %.1f s", perf.uptimeSeconds);
				ImGui::BulletText("Total frames: %zu", perf.totalFrames);
				ImGui::BulletText("Process RAM: %zu MB (peak: %zu MB)", perf.processMemoryMB, perf.processPeakMemoryMB);

				ImGui::SeparatorText("Frame rate");
				int targetFps = Engine::GetInstance().GetTargetFrameRate();
				if (ImGui::SliderInt("FPS limit (0 = unlimited)", &targetFps, 0, 240)) Engine::GetInstance().SetTargetFrameRate(targetFps);
				bool vsync = Engine::GetInstance().render->GetVSync();
				if (ImGui::Checkbox("V-Sync", &vsync)) Engine::GetInstance().render->SetVSync(vsync);

				ImGui::SeparatorText("Performance");
				char fpsOverlay[128];
				sprintf_s(fpsOverlay, sizeof(fpsOverlay), "FPS: %.1f (min %.1f, max %.1f, avg %.1f)", perf.currentFps, perf.minFps, perf.maxFps, perf.avgFps);
				ImGui::PlotLines("##FPSGraph", perf.fpsHistory, (int)PerformanceData::HISTORY_CAPACITY, (int)perf.historyIndex, fpsOverlay, 0.0f, 140.0f, ImVec2(-FLT_MIN, 80));
				char msOverlay[128];
				sprintf_s(msOverlay, sizeof(msOverlay), "Frame time: %.2f ms (min %.2f, max %.2f, avg %.2f)", perf.currentMs, perf.minMs, perf.maxMs, perf.avgMs);
				ImGui::PlotLines("##MSGraph", perf.msHistory, (int)PerformanceData::HISTORY_CAPACITY, (int)perf.historyIndex, msOverlay, 0.0f, 35.0f, ImVec2(-FLT_MIN, 80));
				ImGui::EndTabItem();
			}

			// ------------------------------- Window -------------------------------
			if (ImGui::BeginTabItem("Window"))
			{
				auto win = Engine::GetInstance().window;
				bool applyTitle = ImGui::InputText("Title", windowTitleBuffer, sizeof(windowTitleBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				if (ImGui::Button("Apply")) applyTitle = true;
				if (applyTitle && windowTitleBuffer[0] != '\0') win->SetTitle(windowTitleBuffer);

				int w = win->GetWidth();
				int h = win->GetHeight();
				bool sizeChanged = ImGui::DragInt("Width (px)", &w, 2.0f, Window::MIN_WIDTH, 3840);
				sizeChanged |= ImGui::DragInt("Height (px)", &h, 2.0f, Window::MIN_HEIGHT, 2160);
				if (sizeChanged) win->SetSize(w, h);

				bool isFull = win->IsFullscreen();
				if (ImGui::Checkbox("Fullscreen", &isFull)) win->SetFullscreen(isFull);
				bool isRes = win->IsResizable();
				if (ImGui::Checkbox("Resizable", &isRes)) win->SetResizable(isRes);
				bool isBrd = win->IsBorderless();
				if (ImGui::Checkbox("Borderless", &isBrd)) win->SetBorderless(isBrd);

				ImGui::SeparatorText("Context (requires restart)");
				ImGui::TextDisabled("OpenGL 4.6 Core Profile | Double buffer | Depth 24 bits | Stencil 8 bits");
				ImGui::EndTabItem();
			}

			// ------------------------------- Renderer -------------------------------
			if (ImGui::BeginTabItem("Renderer"))
			{
				auto rnd = Engine::GetInstance().render;
				RenderSettings rs = rnd->GetSettings();
				bool changed = false;

				ImGui::SeparatorText("OpenGL pipeline");
				changed |= ImGui::Checkbox("Depth Test", &rs.depthTest);
				changed |= ImGui::Checkbox("Face Culling", &rs.cullFace);
				changed |= ImGui::Checkbox("Lighting", &rs.lighting);
				int shading = static_cast<int>(rs.shading);
				const char* shadingModes[] = { "Shaded", "Wireframe", "Shaded Wireframe" };
				if (ImGui::Combo("Scene Shading", &shading, shadingModes, IM_ARRAYSIZE(shadingModes)))
				{
					rs.shading = static_cast<ShadingMode>(shading);
					changed = true;
				}
				changed |= ImGui::Checkbox("Selection Outline", &rs.highlightSelection);
				if (rs.highlightSelection) changed |= ImGui::ColorEdit3("Outline Color", glm::value_ptr(rs.outlineColor), ImGuiColorEditFlags_PickerHueWheel);
				changed |= ImGui::Checkbox("Show Z-Buffer Window", &rs.showDepthBuffer);

				ImGui::SeparatorText("Grid");
				changed |= ImGui::Checkbox("Show Grid", &rs.showGrid);
				changed |= ImGui::Checkbox("Show Axis Lines", &rs.showAxis);

				ImGui::SeparatorText("Normals (debug)");
				static const char* normalModes[] = { "None", "Per Vertex (yellow)", "Per Face (cyan)", "Both" };
				int normalsMode = (int)rs.normals;
				if (ImGui::Combo("Normals", &normalsMode, normalModes, IM_ARRAYSIZE(normalModes)))
				{
					rs.normals = (NormalsMode)normalsMode;
					changed = true;
				}
				changed |= ImGui::Checkbox("Normals on every object", &rs.normalsAllObjects);

				SDL_Color bg = rnd->GetBackgroundColor();
				float bgFloat[3] = { bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f };
				if (ImGui::ColorEdit3("Background", bgFloat, ImGuiColorEditFlags_PickerHueWheel))
				{
					rnd->SetBackgroundColor({ (Uint8)(bgFloat[0] * 255.0f), (Uint8)(bgFloat[1] * 255.0f), (Uint8)(bgFloat[2] * 255.0f), 255 });
				}

				ImGui::SeparatorText("Frustum culling and octree");
				changed |= ImGui::Checkbox("Culling in Scene view (editor camera)", &rs.editorFrustumCulling);
				changed |= ImGui::Checkbox("Preview Main Camera culling in Scene view", &rs.cullingPreview);
				changed |= ImGui::Checkbox("Draw every mesh AABB (green = visible, red = culled)", &rs.showAABBs);
				changed |= ImGui::Checkbox("Draw selection AABB", &rs.showSelectedAABB);
				changed |= ImGui::Checkbox("Draw Main Camera frustum", &rs.showCameraFrustum);
				changed |= ImGui::Checkbox("Draw octree nodes", &rs.showOctree);
				changed |= ImGui::Checkbox("Draw picking ray", &rs.showPickRay);
				if (changed) rnd->SetSettings(rs);

				if (Scene* scene = Engine::GetInstance().scene.get())
				{
					bool useOctree = scene->IsUsingOctree();
					if (ImGui::Checkbox("Use octree (off = brute force)", &useOctree)) scene->SetUseOctree(useOctree);
					const Octree& octree = scene->GetOctree();
					ImGui::BulletText("Octree: %d nodes, depth %d, %zu objects (rebuilt %d times)",
						octree.GetNodeCount(), octree.GetDepth(), octree.GetObjectCount(), scene->GetOctreeRebuildCount());
					const RenderStats& renderStats = rnd->GetStats();
					ImGui::BulletText("Scene view: %d / %d drawn (%d nodes visited, %d AABBs tested)",
						renderStats.sceneDrawn, renderStats.sceneCandidates, renderStats.sceneQuery.nodesVisited, renderStats.sceneQuery.objectsTested);
					ImGui::BulletText("Game view:  %d / %d drawn (%d nodes visited, %d AABBs tested)",
						renderStats.gameDrawn, renderStats.gameCandidates, renderStats.gameQuery.nodesVisited, renderStats.gameQuery.objectsTested);
					const PickStats& pick = scene->GetLastPickStats();
					ImGui::BulletText("Last pick: %d candidates, %d meshes, %d triangles (%.3f ms)",
						pick.candidates, pick.meshesTested, pick.trianglesTested, pick.milliseconds);
				}

				ImGui::SeparatorText("Framebuffer");
				ImGui::BulletText("Viewport: %d x %d px | FBO %u | color texture %u | %s", rnd->GetViewportWidth(), rnd->GetViewportHeight(),
					rnd->GetFramebufferID(), rnd->GetSceneTexture(), rnd->IsFramebufferComplete() ? "complete" : "INCOMPLETE");
				ImGui::EndTabItem();
			}

			// ------------------------------- Scene camera -------------------------------
			if (ImGui::BeginTabItem("Scene Camera"))
			{
				CameraComponent* cam = GetEditorCamera();
				if (cam)
				{
					ImGui::SeparatorText("Projection");
					ImGui::SliderFloat("Field of View", &cam->fov, 10.0f, 120.0f, "%.1f deg");
					bool ortho = cam->IsOrthographic();
					if (ImGui::Checkbox("Isometric (orthographic)", &ortho)) cam->SetOrthographic(ortho);
					ImGui::DragFloat("Near Clip", &cam->nearPlane, 0.01f, 0.01f, 10.0f, "%.2f");
					ImGui::DragFloat("Far Clip", &cam->farPlane, 1.0f, 10.0f, 10000.0f, "%.1f");
					ImGui::Checkbox("Frustum Culling", &cam->frustumCulling);

					ImGui::SeparatorText("Navigation");
					float speed = cam->GetMovementSpeed();
					if (ImGui::SliderFloat("Camera Speed", &speed, 0.05f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) cam->SetMovementSpeed(speed);
					float sens = cam->GetMouseSensitivity();
					if (ImGui::SliderFloat("Look Sensitivity", &sens, 0.01f, 1.0f, "%.2f")) cam->SetMouseSensitivity(sens);
					float dist = cam->GetOrbitDistance();
					if (ImGui::DragFloat("Pivot Distance", &dist, 0.1f, 0.01f, 10000.0f, "%.2f")) cam->SetOrbitDistance(dist);
					if (ImGui::Button("Reset Camera")) cam->ResetCamera();
					ImGui::SameLine();
					if (ImGui::Button("Frame Selected (F)")) FrameSelection();

					ImGui::SeparatorText("Controls");
					ImGui::BulletText("Right mouse: look around (WASD / QE to fly, Shift = faster, wheel = speed)");
					ImGui::BulletText("Alt + left mouse: orbit | Middle mouse or Q: pan | Alt + right mouse: zoom");
					ImGui::BulletText("Wheel: zoom | F: frame selection | Axis gizmo: snap view, center = Persp / Iso");
				}
				ImGui::EndTabItem();
			}

			// ------------------------------- Input -------------------------------
			if (ImGui::BeginTabItem("Input"))
			{
				auto inp = Engine::GetInstance().input;
				ImGui::BulletText("Mouse position: (%d, %d)", inp->GetMouseX(), inp->GetMouseY());
				ImGui::BulletText("Mouse motion: (%d, %d)", inp->GetMouseMotionX(), inp->GetMouseMotionY());
				ImGui::BulletText("Wheel: %.2f", inp->GetMouseScrollDelta());
				ImGui::BulletText("Buttons: %s %s %s", inp->IsMouseHeld(1) ? "[Left]" : "Left", inp->IsMouseHeld(2) ? "[Middle]" : "Middle", inp->IsMouseHeld(3) ? "[Right]" : "Right");
				ImGui::SeparatorText("Gamepad");
				const bool gpConnected = inp->IsGamepadConnected();
				ImGui::BulletText("Status: %s", gpConnected ? "Connected" : "Not detected");
				if (gpConnected)
				{
					ImGui::BulletText("Left stick: (%.2f, %.2f)", inp->GetGamepadAxis(GAMEPAD_AXIS_LSTICK_X), inp->GetGamepadAxis(GAMEPAD_AXIS_LSTICK_Y));
					ImGui::BulletText("Right stick: (%.2f, %.2f)", inp->GetGamepadAxis(GAMEPAD_AXIS_RSTICK_X), inp->GetGamepadAxis(GAMEPAD_AXIS_RSTICK_Y));
					ImGui::BulletText("Triggers (LT / RT): (%.2f / %.2f)", inp->GetGamepadAxis(GAMEPAD_AXIS_LT), inp->GetGamepadAxis(GAMEPAD_AXIS_RT));
				}
				ImGui::EndTabItem();
			}

			// ------------------------------- Textures -------------------------------
			if (ImGui::BeginTabItem("Textures"))
			{
				const auto& hw = Engine::GetInstance().GetHardwareMetrics();
				ImGui::BulletText("Loader: DevIL v%s (PNG, JPG, DDS, TGA, BMP)", hw.devilVersion.c_str());
				ImGui::BulletText("Library format: .tex (header + RGB/RGBA pixels ready for OpenGL)");
				if (const ResourceManager* resourceManager = Engine::GetInstance().resources.get())
				{
					ImGui::BulletText("Imported textures: %zu", resourceManager->GetResourcesOfType(ResourceType::Texture).size());
					ImGui::BulletText("Textures in VRAM: %zu", resourceManager->GetLoadedCount(ResourceType::Texture));
				}
				ImGui::TextWrapped("Filtering, wrapping, flipping and max size are per texture: select it in the Project window and change its import settings in the Inspector.");
				ImGui::EndTabItem();
			}

			// ------------------------------- Audio -------------------------------
			if (ImGui::BeginTabItem("Audio"))
			{
				auto aud = Engine::GetInstance().audio;
				float musicVol = aud->GetMusicVolume();
				if (ImGui::SliderFloat("Music Volume", &musicVol, 0.0f, 1.0f, "%.2f")) aud->SetMusicVolume(musicVol);
				float sfxVol = aud->GetSFXVolume();
				if (ImGui::SliderFloat("SFX Volume", &sfxVol, 0.0f, 1.0f, "%.2f")) aud->SetSFXVolume(sfxVol);
				ImGui::BulletText("Driver: SDL3 audio subsystem");
				ImGui::EndTabItem();
			}

			// ------------------------------- Hardware -------------------------------
			if (ImGui::BeginTabItem("Hardware"))
			{
				const auto& hw = Engine::GetInstance().GetHardwareMetrics();
				const auto& perf = Engine::GetInstance().GetPerformanceData();
				ImGui::SeparatorText("CPU");
				ImGui::BulletText("Logical cores: %d | System RAM: %d MB | Cache line: %d bytes", hw.cpuCores, hw.systemRamMB, hw.cpuCacheLineSize);
				std::string caps;
				if (hw.hasAVX) caps += "AVX ";
				if (hw.hasAVX2) caps += "AVX2 ";
				if (hw.hasAVX512) caps += "AVX512 ";
				if (hw.hasSSE) caps += "SSE ";
				if (hw.hasSSE2) caps += "SSE2 ";
				if (hw.hasSSE3) caps += "SSE3 ";
				if (hw.hasSSE41) caps += "SSE4.1 ";
				if (hw.hasSSE42) caps += "SSE4.2 ";
				ImGui::BulletText("Instruction sets: %s", caps.c_str());
				ImGui::SeparatorText("GPU");
				ImGui::BulletText("Vendor: %s", hw.gpuVendor.c_str());
				ImGui::BulletText("Renderer: %s", hw.gpuRenderer.c_str());
				ImGui::BulletText("OpenGL: %s | GLSL: %s", hw.gpuVersion.c_str(), hw.glslVersion.c_str());
				ImGui::BulletText("Max texture: %d px | Max viewport: %d x %d", hw.maxTextureSize, hw.maxViewportDims[0], hw.maxViewportDims[1]);
				ImGui::SeparatorText("Memory");
				ImGui::BulletText("Process RAM: %zu MB (peak %zu MB)", perf.processMemoryMB, perf.processPeakMemoryMB);
				if (hw.vramTotalMB > 0) ImGui::BulletText("VRAM: %d MB total, %d MB available", hw.vramTotalMB, hw.vramAvailableMB);
				else ImGui::BulletText("VRAM: not reported by the driver");
				ImGui::SeparatorText("Software");
				ImGui::BulletText("OS: %s", hw.osPlatform.c_str());
				ImGui::BulletText("SDL3 %s | OpenGL %s | DevIL %s | Assimp %s | Dear ImGui %s", hw.sdlVersion.c_str(), hw.openglVersion.c_str(),
					hw.devilVersion.c_str(), hw.assimpVersion.c_str(), hw.imguiVersion.c_str());
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void ModuleUI::DrawAboutPanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!showAbout) return;

	ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("About " ENGINE_NAME "###About", &showAbout))
	{
		AboutContent::Draw(static_cast<float>(ImGui::GetTime()));
	}
	ImGui::End();
}

void ModuleUI::DrawDepthBufferPanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	Render* render = Engine::GetInstance().render.get();
	if (!render) return;

	RenderSettings rs = render->GetSettings();
	if (!rs.showDepthBuffer) return;

	bool open = rs.showDepthBuffer;
	if (ImGui::Begin(ICON_FA_LAYER_GROUP "  Z-Buffer###Z-Buffer Visualizer", &open))
	{
		const unsigned int depthTexture = render->GetDepthTexture();
		if (depthTexture != 0)
		{
			ImVec2 availSize = ImGui::GetContentRegionAvail();
			if (availSize.x < 50.0f) availSize.x = 200.0f;
			if (availSize.y < 50.0f) availSize.y = 100.0f;
			ImGui::Image((ImTextureID)(intptr_t)depthTexture, availSize, ImVec2(0, 1), ImVec2(1, 0));
		}
		else
		{
			ImGui::TextDisabled("Depth texture not initialized.");
		}
	}
	ImGui::End();

	if (!open)
	{
		rs.showDepthBuffer = false;
		render->SetSettings(rs);
	}
}
