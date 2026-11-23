#include "HubApp.h"
#include "HubStyle.h"
#include "../Engine/Project/HubRegistry.h"
#include "../Engine/UI/EditorTheme.h"
#include "../Engine/UI/EditorPlatform.h"
#include "../Utils/Log.h"
#include "Version.h"

#include <SDL3/SDL.h>
#include <glad/glad.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstring>

namespace
{
	void CopyToBuffer(char* buffer, size_t size, const std::string& text)
	{
		if (size == 0) return;
		const size_t length = std::min(size - 1, text.size());
		std::memcpy(buffer, text.data(), length);
		buffer[length] = '\0';
	}
}

// =====================================================================================
// CICLO DE VIDA
// =====================================================================================
bool HubApp::Run(bool showSplash, std::string& outProjectRoot)
{
	if (!Init(showSplash))
	{
		Shutdown();
		return false;
	}

	while (running)
	{
		ProcessEvents();
		if (!running) break;

		// Minimizada: no se dibuja nada, solo se espera
		if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
		{
			SDL_Delay(16);
			continue;
		}
		Frame();
	}

	Shutdown();
	outProjectRoot = chosenRoot;
	return !chosenRoot.empty();
}

bool HubApp::Init(bool showSplash)
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		LOG_ERROR("[Hub] Could not initialise SDL video: %s", SDL_GetError());
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	stage = showSplash ? Stage::Splash : Stage::Hub;
	const int width = showSplash ? HubStyle::SPLASH_WIDTH : HubStyle::HUB_WIDTH;
	const int height = showSplash ? HubStyle::SPLASH_HEIGHT : HubStyle::HUB_HEIGHT;
	SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;
	flags |= showSplash ? SDL_WINDOW_BORDERLESS : SDL_WINDOW_RESIZABLE;

	const std::string title = std::string(ENGINE_NAME) + " Hub " + ENGINE_VERSION_STRING;
	window = SDL_CreateWindow(title.c_str(), width, height, flags);
	if (window == nullptr)
	{
		LOG_ERROR("[Hub] Could not create the window: %s", SDL_GetError());
		return false;
	}
	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	if (!showSplash) SDL_SetWindowMinimumSize(window, HubStyle::HUB_MIN_WIDTH, HubStyle::HUB_MIN_HEIGHT);

	glContext = SDL_GL_CreateContext(window);
	if (glContext == nullptr)
	{
		LOG_ERROR("[Hub] Could not create the OpenGL context: %s", SDL_GetError());
		return false;
	}
	SDL_GL_MakeCurrent(window, glContext);
	SDL_GL_SetSwapInterval(1);
	if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
	{
		LOG_ERROR("[Hub] Could not load the OpenGL functions");
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // el Hub no guarda layout
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	EditorTheme::LoadFonts();
	EditorTheme::ApplyUnityDark();
	HubStyle::Apply();
	ImGui_ImplSDL3_InitForOpenGL(window, glContext);
	ImGui_ImplOpenGL3_Init("#version 130");
	imguiReady = true;

	startTime = static_cast<double>(SDL_GetTicks()) / 1000.0;
	stageStartTime = startTime;

	// Sin splash la lista se carga ya; con splash se carga durante la animacion
	if (!showSplash)
	{
		HubRegistry::Load();
		RefreshProjects();
	}

	SDL_ShowWindow(window);
	SDL_RaiseWindow(window);
	LOG("[Hub] %s Hub %s started", ENGINE_NAME, ENGINE_VERSION_STRING);
	return true;
}

void HubApp::Shutdown()
{
	if (imguiReady)
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		imguiReady = false;
	}
	if (glContext != nullptr)
	{
		SDL_GL_DestroyContext(glContext);
		glContext = nullptr;
	}
	if (window != nullptr)
	{
		SDL_DestroyWindow(window);
		window = nullptr;
	}
	// El editor vuelve a inicializar SDL en Window::Awake
	SDL_Quit();
}

void HubApp::ProcessEvents()
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		ImGui_ImplSDL3_ProcessEvent(&event);

		if (event.type == SDL_EVENT_QUIT) running = false;
		if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) running = false;

		// El splash se puede saltar con un clic o una tecla
		if (stage == Stage::Splash && (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_KEY_DOWN))
		{
			splashSkipRequested = true;
		}
	}
}

void HubApp::Frame()
{
	time = static_cast<float>(static_cast<double>(SDL_GetTicks()) / 1000.0 - startTime);

	ProcessDialogResult();

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	switch (stage)
	{
	case Stage::Splash:  DrawSplash(); break;
	case Stage::Hub:     DrawHub(); break;
	case Stage::Opening: DrawHub(); DrawOpeningOverlay(); break;
	}

	ImGui::Render();
	const ImGuiIO& io = ImGui::GetIO();
	glViewport(0, 0, static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x), static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
	glClearColor(HubStyle::BACKGROUND.x, HubStyle::BACKGROUND.y, HubStyle::BACKGROUND.z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	SDL_GL_SwapWindow(window);

	// "Opening": se dibujan un par de frames con el aviso y se cierra el Hub
	if (stage == Stage::Opening && ++openingFrames >= 3) running = false;
}

// =====================================================================================
// PROYECTOS
// =====================================================================================
void HubApp::RefreshProjects()
{
	const std::string selectedRoot = (selected >= 0 && selected < static_cast<int>(projects.size())) ? projects[selected].root : std::string();

	projects.clear();
	for (const HubRegistry::Entry& entry : HubRegistry::GetEntries())
	{
		HubProject project;
		project.root = entry.path;
		project.pinned = entry.pinned;
		project.exists = Project::FolderExists(entry.path);
		project.hasProjectFile = project.exists && project.info.Load(entry.path);
		if (!project.hasProjectFile)
		{
			// Carpeta sin project.json (o que ya no existe): se muestra con el nombre de la carpeta
			const size_t slash = entry.path.find_last_of('/');
			project.info.name = (slash == std::string::npos) ? entry.path : entry.path.substr(slash + 1);
		}
		project.lastOpened = std::max(entry.lastOpened, project.info.lastOpenedAt);
		projects.push_back(project);
	}
	SortProjects();

	selected = -1;
	for (int i = 0; i < static_cast<int>(projects.size()); ++i)
	{
		if (projects[i].root == selectedRoot) selected = i;
	}
	detailsBufferIndex = -1;
}

void HubApp::SortProjects()
{
	std::stable_sort(projects.begin(), projects.end(), [](const HubProject& a, const HubProject& b)
	{
		if (a.pinned != b.pinned) return a.pinned;
		return a.lastOpened > b.lastOpened;
	});
}

void HubApp::RequestOpenProject(int index)
{
	if (index < 0 || index >= static_cast<int>(projects.size())) return;
	const HubProject& project = projects[index];
	if (!project.exists)
	{
		ShowToast("The project folder no longer exists", true);
		return;
	}

	// Proyecto guardado con una version del motor mas nueva: se pide confirmacion
	SemVer projectEngine, currentEngine;
	if (project.hasProjectFile && SemVer::Parse(project.info.engineVersion, projectEngine) &&
		SemVer::Parse(ENGINE_VERSION_STRING, currentEngine))
	{
		const bool newer = projectEngine.major != currentEngine.major ? projectEngine.major > currentEngine.major :
			projectEngine.minor != currentEngine.minor ? projectEngine.minor > currentEngine.minor :
			projectEngine.patch > currentEngine.patch;
		if (newer)
		{
			pendingOpenIndex = index;
			openEngineVersionPopup = true;
			return;
		}
	}

	OpenProjectNow(project.root);
}

void HubApp::OpenProjectNow(const std::string& root)
{
	ProjectInfo info;
	if (!Project::LoadOrAdopt(root, info))
	{
		ShowToast("This folder is not a " ENGINE_NAME " project", true);
		return;
	}

	HubRegistry::AddOrTouch(root);
	HubRegistry::Save();

	chosenRoot = root;
	openingName = info.name;
	openingFrames = 0;
	stage = Stage::Opening;
	LOG("[Hub] Opening project '%s' (%s)", info.name.c_str(), root.c_str());
}

void HubApp::AddExistingProject(const std::string& folder)
{
	const std::string root = Project::NormalizeRoot(folder);
	if (!Project::LooksLikeProject(root))
	{
		ShowToast("That folder is not a project (it has no Assets/ or ProjectSettings/project.json)", true);
		return;
	}
	ProjectInfo info;
	if (!Project::LoadOrAdopt(root, info))
	{
		ShowToast("Could not read the project", true);
		return;
	}

	HubRegistry::AddOrTouch(root, false);
	HubRegistry::Save();
	RefreshProjects();
	for (int i = 0; i < static_cast<int>(projects.size()); ++i)
	{
		if (projects[i].root == root) selected = i;
	}
	ShowToast("Added '" + info.name + "'");
}

void HubApp::RemoveProject(int index)
{
	if (index < 0 || index >= static_cast<int>(projects.size())) return;
	const std::string name = projects[index].info.name;
	HubRegistry::Remove(projects[index].root);
	HubRegistry::Save();
	if (selected == index) selected = -1;
	RefreshProjects();
	ShowToast("Removed '" + name + "' from the list (files were not deleted)");
}

void HubApp::SaveProjectInfo(HubProject& project)
{
	if (!project.exists) return;
	project.info.lastModifiedAt = Project::Now();
	if (project.info.engineVersion.empty()) project.info.engineVersion = ENGINE_VERSION_STRING;
	if (project.info.Save(project.root)) project.hasProjectFile = true;
	else ShowToast("Could not save project.json", true);
}

void HubApp::OpenNewProjectModal()
{
	// Nombre libre por defecto: "My Project", "My Project 1"...
	const std::string location = HubRegistry::GetDefaultProjectsDir();
	std::string name = "My Project";
	for (int i = 1; i < 100 && Project::FolderExists(location + "/" + name); ++i) name = "My Project " + std::to_string(i);

	CopyToBuffer(newNameBuffer, sizeof(newNameBuffer), name);
	CopyToBuffer(newLocationBuffer, sizeof(newLocationBuffer), location);
	newDescriptionBuffer[0] = '\0';
	newProjectError.clear();
	openNewProjectPopup = true;
}

void HubApp::ShowToast(const std::string& text, bool error)
{
	toastText = text;
	toastError = error;
	toastTime = time;
	if (error) LOG_WARNING("[Hub] %s", text.c_str());
}

// =====================================================================================
// DIALOGO DE CARPETAS (SDL3, asincrono)
// =====================================================================================
void HubApp::ShowFolderDialog(DialogPurpose purpose, const std::string& defaultLocation)
{
	{
		std::lock_guard<std::mutex> lock(dialogMutex);
		if (dialogOpen) return;
		dialogOpen = true;
		dialogPurpose = purpose;
		dialogHasResult = false;
		dialogResult.clear();
	}
	SDL_ShowOpenFolderDialog(&HubApp::FolderDialogCallback, this, window,
		defaultLocation.empty() ? nullptr : defaultLocation.c_str(), false);
}

void HubApp::FolderDialogCallback(void* userdata, const char* const* filelist, int)
{
	HubApp* hub = static_cast<HubApp*>(userdata);
	if (hub == nullptr) return;

	std::lock_guard<std::mutex> lock(hub->dialogMutex);
	hub->dialogOpen = false;
	if (filelist == nullptr)
	{
		LOG_ERROR("[Hub] Folder dialog error: %s", SDL_GetError());
		hub->dialogPurpose = DialogPurpose::None;
		return;
	}
	if (filelist[0] == nullptr)
	{
		hub->dialogPurpose = DialogPurpose::None; // cancelado
		return;
	}
	hub->dialogResult = filelist[0];
	hub->dialogHasResult = true;
}

void HubApp::ProcessDialogResult()
{
	DialogPurpose purpose = DialogPurpose::None;
	std::string result;
	{
		std::lock_guard<std::mutex> lock(dialogMutex);
		if (!dialogHasResult) return;
		purpose = dialogPurpose;
		result = dialogResult;
		dialogHasResult = false;
		dialogPurpose = DialogPurpose::None;
	}

	switch (purpose)
	{
	case DialogPurpose::AddExisting:
		AddExistingProject(result);
		break;
	case DialogPurpose::NewProjectLocation:
		CopyToBuffer(newLocationBuffer, sizeof(newLocationBuffer), Project::NormalizeRoot(result));
		break;
	case DialogPurpose::DefaultLocation:
		HubRegistry::SetDefaultProjectsDir(result);
		HubRegistry::Save();
		CopyToBuffer(defaultLocationBuffer, sizeof(defaultLocationBuffer), HubRegistry::GetDefaultProjectsDir());
		break;
	default:
		break;
	}
}
