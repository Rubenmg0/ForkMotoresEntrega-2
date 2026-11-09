#pragma once

#include "../Module.h"
#include "../../Utils/Log.h"
#include "../FileSystem/FileSystem.h"
#include "../ResourceManagement/ImportSettings.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class GameObject;

class ModuleUI : public Module
{
public:
	ModuleUI();
	virtual ~ModuleUI();

	bool Awake() override;
	bool Start() override;
	bool PreUpdate() override;
	bool Update(float dt) override;
	bool PostUpdate() override;
	bool CleanUp() override;

	void ResetLayout() { resetLayout = true; }

private:
	void RenderUI();
	bool DrawMainMenuBar();
	void DrawToolbar();          // Play / Pause / Step / Stop
	void DrawScenePanel();
	void DrawSceneOverlayBar();  // fila superior de la vista Scene: gizmos + estadisticas de culling
	void DrawGamePanel();
	void DrawHierarchyPanel();
	void DrawHierarchyNode(GameObject* node, GameObject* selected);
	void DrawInspectorPanel();
	void DrawGameObjectInspector(GameObject* selected);
	void DrawMeshInspector(GameObject* selected);
	void DrawMaterialInspector(GameObject* selected);
	void DrawCameraInspector(GameObject* selected);
	void DrawAddComponentButton(GameObject* selected);
	void DrawConsolePanel();
	void DrawConfigurationPanel();
	void DrawAboutPanel();
	void DrawDepthBufferPanel();
	void DrawSceneFilePopups();
	void HandleShortcuts();

	// Selecciona un GameObject y hace que el Inspector lo muestre
	void SelectGameObject(GameObject* object);

	// ----------------- Ventanas Assets y Resources (EditorAssets.cpp) -----------------
	void DrawAssetsPanel();
	void DrawAssetsFolderTree(const std::string& folder);
	void DrawAssetsFolderContent();
	void DrawAssetEntry(const FileSystem::DirectoryEntry& entry);
	void DrawAssetContextMenu(const FileSystem::DirectoryEntry& entry);
	void DrawAssetsPopups();
	void DrawAssetInspector();
	void DrawResourcesPanel();
	void OpenAsset(const std::string& path, bool isDirectory);
	void SelectAsset(const std::string& path);
	std::vector<FileSystem::DirectoryEntry> GetCachedDirectory(const std::string& folder);
	void InvalidateAssetsCache() { assetsCacheInvalid = true; }

	// Operaciones sobre assets que se ejecutan al PRINCIPIO del siguiente frame del editor.
	// Borrar o reimportar libera texturas de la GPU: si se hiciera a mitad de frame, ImGui
	// podria intentar dibujar (en ese mismo frame) una textura que ya no existe.
	enum class AssetOperation { Delete, DeleteFolder, Reimport, ApplyTextureSettings, ApplyModelSettings, Refresh };
	struct PendingAssetOperation
	{
		AssetOperation operation = AssetOperation::Refresh;
		std::string path;
		TextureImportSettings textureSettings;
		ModelImportSettings modelSettings;
	};
	std::vector<PendingAssetOperation> pendingAssetOperations;
	void QueueAssetOperation(AssetOperation operation, const std::string& path);
	void ProcessPendingAssetOperations();

	bool resetLayout = false;

	// Visibilidad de ventanas del Editor
	bool showScene = true;
	bool showGame = true;
	bool showHierarchy = true;
	bool showInspector = true;
	bool showConsole = true;
	bool showConfiguration = true;
	bool showAbout = false;
	bool showAssets = true;
	bool showResources = true;

	// Foco para atajos de teclado (Supr / Delete)
	bool sceneOrHierarchyFocused = false;

	// Hierarchy: al cambiar la seleccion se despliegan sus padres para que se vea (solo ese frame)
	GameObject* lastHierarchySelection = nullptr;
	bool revealSelectionInHierarchy = false;
	bool assetsFocused = false;

	// Estado del panel Console
	struct ConsoleDisplayItem
	{
		int logIndex = -1;
		int collapsedCount = 1;
	};
	std::vector<ConsoleDisplayItem> consoleViewCache;
	uint64_t lastConsoleRevision = UINT64_MAX;
	bool consoleFilterDirty = true;
	bool scrollToBottom = false;

	bool showInfo = true;
	bool showWarnings = true;
	bool showErrors = true;
	bool collapseLogs = false;
	bool autoScroll = true;
	char consoleFilter[128] = "";
	int selectedLogIndex = -1;

	// Estado del panel Configuration
	char windowTitleBuffer[128] = "Motor Videojuego";

	// Inspector: muestra un GameObject o un asset (lo ultimo que se haya seleccionado)
	bool inspectorShowsAsset = false;
	GameObject* lastInspectedObject = nullptr;

	// Estado de la ventana Assets
	std::string assetsCurrentFolder = FileSystem::ASSETS_DIR;
	std::string selectedAssetPath;
	std::unordered_map<std::string, std::vector<FileSystem::DirectoryEntry>> assetsDirectoryCache;
	uint64_t assetsCacheTime = 0;      // ms (SDL_GetTicks) de la ultima lectura del disco
	bool assetsCacheInvalid = true;
	std::string pendingDeletePath;     // asset/carpeta pendiente de confirmar su borrado
	bool pendingDeleteIsFolder = false;
	bool openDeletePopup = false;
	bool openNewFolderPopup = false;
	char newFolderName[128] = "NewFolder";

	// Opciones de importacion que se estan editando en el Inspector (se aplican con "Apply")
	std::string editingSettingsPath;
	TextureImportSettings editingTextureSettings;
	ModelImportSettings editingModelSettings;
	TextureImportSettings savedTextureSettings;  // lo que hay ahora mismo en el .meta
	ModelImportSettings savedModelSettings;

	// Estado de la ventana Resources
	int resourcesFilter = 0;           // 0 todos, 1 texturas, 2 mallas, 3 modelos
	bool resourcesOnlyLoaded = false;

	// Guardar escena como...
	bool openSaveScenePopup = false;
	char saveSceneName[128] = "NuevaEscena";
};
