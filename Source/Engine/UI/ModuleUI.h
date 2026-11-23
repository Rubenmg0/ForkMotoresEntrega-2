#pragma once

#include "../Module.h"
#include "../../Utils/Log.h"
#include "../FileSystem/FileSystem.h"
#include "../ResourceManagement/ImportSettings.h"
#include "../ResourceManagement/Resource.h"
#include "../Render/Primitives/GeometryGenerator.h"
#include "UndoSystem.h"

#include <glm/mat4x4.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class GameObject;
class CameraComponent;
struct ImVec2;

// =====================================================================================
// ModuleUI: el editor del motor (Dear ImGui con docking), con el aspecto y el manejo del
// editor de Unity. Cada ventana esta en su propio fichero:
//   ModuleUI.cpp         arranque, layout, menu principal, barra de Play, atajos, Console,
//                        Configuration, About
//   EditorSceneView.cpp  ventanas Scene (navegacion, herramientas, gizmos, seleccion) y Game
//   EditorHierarchy.cpp  ventana Hierarchy
//   EditorInspector.cpp  ventana Inspector (GameObjects)
//   EditorAssets.cpp     ventanas Project y Resources e Inspector de assets
// =====================================================================================
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

	// Estado de la camara del editor guardado en el fichero de layout (lo usa el handler del .ini)
	void ReadEditorCameraSetting(const char* line);
	void WriteEditorCameraSetting(struct ImGuiTextBuffer* buffer);

private:
	// ----------------- Arranque y frame -----------------
	void LoadEditorFonts();
	void RegisterSettingsHandler();
	void RenderUI();
	void DrawDockSpaceAndLayout();
	void BuildDefaultLayout(unsigned int dockspaceId);
	bool DrawMainMenuBar();
	void DrawToolbar();
	void DrawSceneFilePopups();
	void HandleShortcuts();
	void TrackSceneChanges();

	// ----------------- Operaciones de edicion (con Undo) -----------------
	void SelectGameObject(GameObject* object);   // reemplaza la seleccion y el Inspector muestra el objeto
	void ClickSelect(GameObject* object);        // clic con Ctrl / Shift: anade o quita de la seleccion
	void DeleteSelection();
	void DuplicateSelection();
	void FrameSelection();
	void AlignWithView();       // Ctrl+Shift+F: la seleccion se coloca en la camara del editor
	void MoveToView();          // Ctrl+Alt+F: la seleccion se mueve al pivote de la vista
	void AlignViewToSelected(); // la vista mira como el objeto seleccionado
	// Crea un objeto (deshacible), lo selecciona y empieza a renombrarlo en la Hierarchy
	GameObject* CreateObject(const std::string& undoName, const std::function<GameObject*()>& creator);
	void DrawCreateMenuItems(GameObject* parent); // "Create Empty", "3D Object", "Camera"
	CameraComponent* GetEditorCamera() const;

	// ----------------- Scene view (EditorSceneView.cpp) -----------------
	enum class Tool { View, Move, Rotate, Scale, Transform };
	enum class GizmoSpace { Local, World };
	enum class PivotMode { Pivot, Center };
	void DrawScenePanel();
	void DrawSceneToolbar();        // barra superior de la vista Scene
	void DrawToolsOverlay(const ImVec2& imagePos);
	void DrawOrientationGizmo(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera);
	void DrawSceneIcons(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera);
	void DrawSceneOverlays(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera);
	void DrawTransformGizmo(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera);
	void HandleSceneSelection(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera, bool imageHovered);
	void HandleSceneDragDrop(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera);
	void DrawGamePanel();
	void DrawGameImage(unsigned int texture, const ImVec2& regionPos, const ImVec2& regionSize, bool updateRenderSize);
	void DrawMaximizedGameWindow();
	bool WorldToScreen(const glm::vec3& world, const glm::mat4& viewProjection, const ImVec2& imagePos, const ImVec2& imageSize, ImVec2& out) const;

	// ----------------- Hierarchy (EditorHierarchy.cpp) -----------------
	void DrawHierarchyPanel();
	void DrawHierarchyNode(GameObject* node);
	void DrawHierarchyContextMenu(GameObject* node);
	bool AcceptHierarchyDrop(GameObject* target, int dropZone); // 0 = dentro, -1 = antes, +1 = despues
	void BeginRename(GameObject* object);
	void HandleHierarchyKeyboard();
	void HierarchyRowClicked(GameObject* node);

	// ----------------- Inspector (EditorInspector.cpp) -----------------
	void DrawInspectorPanel();
	void DrawGameObjectInspector(GameObject* selected);
	void DrawTransformInspector(GameObject* selected);
	void DrawMeshInspector(GameObject* selected);
	void DrawMaterialInspector(GameObject* selected);
	void DrawCameraInspector(GameObject* selected);
	void DrawComponentMenu(GameObject* selected, int componentType);
	void DrawAddComponentButton(GameObject* selected);
	void InspectorUndoGuard(const char* name);  // Undo de la edicion del ultimo widget del Inspector

	// ----------------- Console / Configuration / About (ModuleUI.cpp) -----------------
	void DrawConsolePanel();
	void DrawConfigurationPanel();
	void DrawAboutPanel();
	void DrawDepthBufferPanel();

	// ----------------- Project y Resources (EditorAssets.cpp) -----------------
	void DrawProjectPanel();
	void DrawProjectToolbar();
	void DrawProjectFolderTree(const std::string& folder);
	void DrawProjectContent();
	void DrawProjectGridItem(const FileSystem::DirectoryEntry& entry, float tileSize);
	void DrawProjectListItem(const FileSystem::DirectoryEntry& entry);
	void HandleAssetItemInteraction(const FileSystem::DirectoryEntry& entry, UID assetUID);
	void DrawAssetContextMenu(const FileSystem::DirectoryEntry& entry);
	void DrawAssetsPopups();
	void DrawAssetInspector();
	void DrawResourcesPanel();
	void OpenAsset(const std::string& path, bool isDirectory);
	void SelectAsset(const std::string& path);
	void SetProjectFolder(const std::string& folder);
	// Abre la carpeta en el explorador de archivos del sistema (un fichero queda seleccionado)
	static void RevealInExplorer(const std::string& projectPath);
	unsigned int GetAssetThumbnail(UID textureUID);   // id de OpenGL (0 si no hay miniatura)
	void ReleaseThumbnails();
	void BeginAssetRename(const std::string& path);
	void CommitAssetRename();
	std::vector<FileSystem::DirectoryEntry> GetCachedDirectory(const std::string& folder);
	std::vector<FileSystem::DirectoryEntry> GetProjectSearchResults();
	void InvalidateAssetsCache() { assetsCacheInvalid = true; }

	// Operaciones sobre assets que se ejecutan al PRINCIPIO del siguiente frame del editor.
	// Borrar o reimportar libera texturas de la GPU: si se hiciera a mitad de frame, ImGui
	// podria intentar dibujar (en ese mismo frame) una textura que ya no existe.
	enum class AssetOperation { Delete, DeleteFolder, Reimport, ApplyTextureSettings, ApplyModelSettings, Refresh, Rename };
	struct PendingAssetOperation
	{
		AssetOperation operation = AssetOperation::Refresh;
		std::string path;
		std::string newPath;
		TextureImportSettings textureSettings;
		ModelImportSettings modelSettings;
	};
	std::vector<PendingAssetOperation> pendingAssetOperations;
	void QueueAssetOperation(AssetOperation operation, const std::string& path, const std::string& newPath = std::string());
	void ProcessPendingAssetOperations();

	// =====================================================================================
	// ESTADO
	// =====================================================================================
	bool resetLayout = false;
	bool layoutInitialized = false;

	// Visibilidad de ventanas del Editor
	bool showScene = true;
	bool showGame = true;
	bool showHierarchy = true;
	bool showInspector = true;
	bool showConsole = true;
	bool showConfiguration = true;
	bool showAbout = false;
	bool showProject = true;
	bool showResources = true;

	UndoSystem undo;
	uint64_t lastSceneLoadVersion = 0;
	int lastPlayState = 0;

	// Foco para atajos de teclado
	bool sceneOrHierarchyFocused = false;
	bool hierarchyFocused = false;

	// ----------------- Vista Scene -----------------
	Tool currentTool = Tool::Move;          // Q W E R Y
	GizmoSpace gizmoSpace = GizmoSpace::World;  // X alterna Local / Global
	PivotMode pivotMode = PivotMode::Pivot;     // Z alterna Pivot / Center
	float snapMove = 0.25f;
	float snapRotate = 15.0f;
	float snapScale = 0.1f;
	bool sceneShowStats = false;
	// Prioridad de input: se recalculan cada frame al dibujar el gizmo
	bool gizmoHovered = false;
	bool gizmoInUse = false;
	bool gizmoWasInUse = false;
	bool sceneViewHovered = false;
	bool sceneOverlayHovered = false;  // raton sobre un overlay (herramientas, ejes...): no es un clic de escena
	// Arrastre del gizmo con varios objetos: matrices de mundo al empezar
	std::vector<std::pair<uint64_t, glm::mat4>> gizmoStartWorld;
	glm::mat4 gizmoStartMatrix = glm::mat4(1.0f);
	glm::mat4 gizmoMatrix = glm::mat4(1.0f);
	// Seleccion por rectangulo (marquee)
	bool marqueePending = false;   // clic izquierdo pulsado en zona vacia, aun sin arrastrar
	bool marqueeActive = false;
	float marqueeStartX = 0.0f, marqueeStartY = 0.0f;
	bool marqueeAdditive = false;
	std::vector<uint64_t> marqueeBaseSelection;

	// ----------------- Vista Game -----------------
	int gameAspectIndex = 0;     // Free Aspect, 16:9, 16:10, 4:3, 5:4
	int gameDisplayIndex = 0;    // Display 1..8 (se ve la camara cuyo Target Display es este)
	float gameScale = 1.0f;
	bool maximizeOnPlay = false;
	bool gameShowStats = false;

	// ----------------- Hierarchy -----------------
	char hierarchySearch[128] = "";
	uint64_t renamingUID = 0;
	char renameBuffer[128] = "";
	bool renameFocusPending = false;
	uint64_t lastSelectionVersion = UINT64_MAX;
	bool revealSelectionInHierarchy = false;
	uint64_t rangeAnchorUID = 0;
	std::vector<GameObject*> hierarchyRows;        // filas visibles (este frame), para Shift + clic
	std::vector<GameObject*> previousHierarchyRows;  // las del frame anterior (ya completas)
	std::unordered_map<uint64_t, bool> pendingOpenState; // flechas izquierda / derecha
	std::unordered_set<uint64_t> openHierarchyNodes;      // nodos desplegados (para el teclado)
	GameObject* hierarchyDropTarget = nullptr;
	int hierarchyDropZone = 0;

	// ----------------- Inspector -----------------
	bool inspectorShowsAsset = false;
	GameObject* lastInspectedObject = nullptr;
	bool inspectorTransformEditing = false;
	char addComponentSearch[64] = "";
	int clipboardComponentType = -1;
	std::string clipboardComponentData;
	char inspectorNameBuffer[128] = "";
	uint64_t inspectorNameUID = 0;

	// ----------------- Console -----------------
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
	bool clearOnPlay = false;
	bool autoScroll = true;
	char consoleFilter[128] = "";
	int selectedLogIndex = -1;

	// ----------------- Configuration -----------------
	char windowTitleBuffer[128] = "Motor Videojuego";

	// ----------------- Project -----------------
	std::string assetsCurrentFolder = FileSystem::ASSETS_DIR;
	std::string selectedAssetPath;
	std::unordered_map<std::string, std::vector<FileSystem::DirectoryEntry>> assetsDirectoryCache;
	uint64_t assetsCacheTime = 0;      // ms (SDL_GetTicks) de la ultima lectura del disco
	bool assetsCacheInvalid = true;
	std::string pendingDeletePath;     // asset/carpeta pendiente de confirmar su borrado
	bool pendingDeleteIsFolder = false;
	bool openDeletePopup = false;
	bool openNewFolderPopup = false;
	char newFolderName[128] = "New Folder";
	bool projectFocused = false;
	float projectIconSize = 64.0f;     // tamano de las miniaturas (el minimo pasa a vista de lista)
	char projectSearch[128] = "";
	int projectFavorite = -1;          // -1 carpeta normal | 0 modelos | 1 texturas | 2 escenas
	std::string renamingAssetPath;
	char assetRenameBuffer[128] = "";
	bool assetRenameFocusPending = false;
	std::unordered_set<UID> thumbnailRefs;  // texturas pedidas al ResourceManager para las miniaturas
	std::unordered_set<std::string> expandedModels; // modelos desplegados en la cuadricula

	// Opciones de importacion que se estan editando en el Inspector (se aplican con "Apply")
	std::string editingSettingsPath;
	TextureImportSettings editingTextureSettings;
	ModelImportSettings editingModelSettings;
	TextureImportSettings savedTextureSettings;  // lo que hay ahora mismo en el .meta
	ModelImportSettings savedModelSettings;

	// ----------------- Resources -----------------
	int resourcesFilter = 0;           // 0 todos, 1 texturas, 2 mallas, 3 modelos
	bool resourcesOnlyLoaded = false;

	// ----------------- Guardar escena como... -----------------
	bool openSaveScenePopup = false;
	char saveSceneName[128] = "NewScene";
};
