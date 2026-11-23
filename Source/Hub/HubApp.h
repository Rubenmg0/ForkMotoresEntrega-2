#pragma once

#include "../Engine/Project/ProjectInfo.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_GLContextState;
struct ImDrawList;
struct ImVec2;

// =====================================================================================
// Maruja Engine Hub.
//
// Es lo primero que se ve al arrancar el motor sin argumentos: un splash con el logo y despues
// el lanzador de proyectos (lista de proyectos recientes, crear, anadir, gestionar su version).
//
// Vive en el mismo ejecutable que el editor pero NO usa el singleton Engine: tiene su propia
// ventana SDL, su contexto OpenGL y su contexto de ImGui. Cuando el usuario elige un proyecto,
// Run() lo destruye todo y devuelve la carpeta; main.cpp arranca entonces el editor con ella.
// =====================================================================================
class HubApp
{
public:
	// Ejecuta el Hub hasta que se elige un proyecto (true, outProjectRoot relleno) o se cierra (false)
	bool Run(bool showSplash, std::string& outProjectRoot);

private:
	enum class Stage { Splash, Hub, Opening };
	enum class Page { Projects, Settings, About };
	enum class DialogPurpose { None, AddExisting, NewProjectLocation, DefaultLocation };

	// Proyecto tal y como se muestra en la lista
	struct HubProject
	{
		std::string root;
		ProjectInfo info;
		bool exists = false;       // la carpeta existe
		bool hasProjectFile = false;
		bool pinned = false;
		int64_t lastOpened = 0;
	};

	// ------------------------------- Ciclo de vida -------------------------------
	bool Init(bool showSplash);
	void Shutdown();
	void ProcessEvents();
	void Frame();

	// ------------------------------- Splash (HubSplash.cpp) -------------------------------
	void DrawSplash();
	void FinishSplash();

	// ------------------------------- Vistas (HubViews.cpp) -------------------------------
	void DrawHub();
	void DrawSidebar();
	void DrawProjectsPage();
	void DrawProjectsTable(const std::vector<int>& visible);
	void DrawEmptyState();
	void DrawDetailsPanel();
	void DrawSettingsPage();
	void DrawAboutPage();
	void DrawNewProjectModal();
	void DrawEngineVersionModal();
	void DrawProjectContextMenu(int index);
	void ProcessListAction();
	void DrawOpeningOverlay();

	// ------------------------------- Acciones -------------------------------
	void RefreshProjects();
	void SortProjects();
	void RequestOpenProject(int index);
	void OpenProjectNow(const std::string& root);
	void AddExistingProject(const std::string& folder);
	void RemoveProject(int index);
	void SaveProjectInfo(HubProject& project);
	void OpenNewProjectModal();
	void ShowFolderDialog(DialogPurpose purpose, const std::string& defaultLocation);
	void ProcessDialogResult();
	void ShowToast(const std::string& text, bool error = false);

	static void FolderDialogCallback(void* userdata, const char* const* filelist, int filter);

	// ------------------------------- Estado -------------------------------
	SDL_Window* window = nullptr;
	SDL_GLContextState* glContext = nullptr;
	bool running = true;
	bool imguiReady = false;

	Stage stage = Stage::Splash;
	Page page = Page::Projects;
	double startTime = 0.0;          // segundos (SDL_GetTicks) al arrancar
	double stageStartTime = 0.0;
	float time = 0.0f;               // segundos desde el arranque (animaciones)
	int splashStep = 0;              // trabajo real que se hace durante el splash
	bool splashSkipRequested = false;

	std::vector<HubProject> projects;
	int selected = -1;

	// Acciones del menu contextual que modifican la lista (se aplican tras dibujar la tabla)
	enum class ListAction { None, Remove, TogglePin };
	ListAction pendingListAction = ListAction::None;
	int pendingListIndex = -1;
	char searchBuffer[128] = "";

	// Modal "New project"
	bool openNewProjectPopup = false;
	char newNameBuffer[128] = "";
	char newLocationBuffer[512] = "";
	char newDescriptionBuffer[512] = "";
	std::string newProjectError;

	// Modal de aviso por version del motor
	bool openEngineVersionPopup = false;
	int pendingOpenIndex = -1;

	// Edicion de la descripcion en el panel de detalle
	char detailsDescriptionBuffer[512] = "";
	int detailsBufferIndex = -1;

	// Ajustes
	char defaultLocationBuffer[512] = "";

	// Proyecto elegido
	std::string chosenRoot;
	std::string openingName;
	int openingFrames = 0;

	// Dialogo de carpetas de SDL (asincrono: el resultado llega por callback, quiza desde otro hilo)
	std::mutex dialogMutex;
	DialogPurpose dialogPurpose = DialogPurpose::None;
	bool dialogHasResult = false;
	std::string dialogResult;
	bool dialogOpen = false;

	// Aviso temporal en la esquina inferior
	std::string toastText;
	bool toastError = false;
	float toastTime = 0.0f;
};
