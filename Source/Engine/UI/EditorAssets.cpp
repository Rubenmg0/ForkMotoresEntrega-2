// =====================================================================================
// Ventanas del editor relacionadas con los recursos (parte de ModuleUI):
//   - "Project": como la de Unity. Arbol de carpetas (con Favoritos) a la izquierda y, a la
//     derecha, ruta clicable, cuadricula de iconos con miniaturas (o lista, con el tamano
//     minimo del deslizador), buscador, renombrar (F2), mover arrastrando a una carpeta...
//   - "Resources": todos los recursos cargados con su conteo de referencias
//   - Inspector de un asset: informacion + opciones de importacion (se guardan en el .meta)
// =====================================================================================
#include "ModuleUI.h"
#include "EditorDragDrop.h"
#include "EditorIcons.h"
#include "EditorWidgets.h"
#include "../Engine.h"
#include "../Scene/Scene.h"
#include "../Scene/GameObjects/GameObject.h"
#include "../ResourceManagement/ResourceManager.h"
#include "../ResourceManagement/ResourceMesh.h"
#include "../ResourceManagement/ResourceTexture.h"
#include "../ResourceManagement/Importers/ImporterManager.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>

using namespace EditorWidgets;

namespace
{
	enum class AssetKind { Folder, Texture, Model, Scene, Other };

	constexpr uint64_t ASSETS_CACHE_LIFETIME_MS = 1000; // el listado de carpetas se relee cada segundo
	constexpr float LIST_MODE_ICON_SIZE = 20.0f;         // con el deslizador al minimo: vista de lista
	constexpr size_t MAX_THUMBNAILS = 256;

	// Lista de todos los ficheros de Assets (para el buscador y los favoritos)
	std::vector<std::string> allAssetFiles;
	bool allAssetFilesValid = false;

	AssetKind GetAssetKind(const FileSystem::DirectoryEntry& entry)
	{
		if (entry.isDirectory) return AssetKind::Folder;
		if (ResourceManager::IsTextureFile(entry.path)) return AssetKind::Texture;
		if (ResourceManager::IsModelFile(entry.path)) return AssetKind::Model;
		if (ResourceManager::IsSceneFile(entry.path)) return AssetKind::Scene;
		return AssetKind::Other;
	}

	const char* KindIcon(AssetKind kind)
	{
		switch (kind)
		{
		case AssetKind::Folder:  return ICON_FA_FOLDER;
		case AssetKind::Texture: return ICON_FA_IMAGE;
		case AssetKind::Model:   return ICON_FA_CUBES;
		case AssetKind::Scene:   return ICON_FA_CLAPPERBOARD;
		default:                 return ICON_FA_FILE;
		}
	}

	ImVec4 KindColor(AssetKind kind)
	{
		switch (kind)
		{
		case AssetKind::Folder:  return EditorPalette::ICON_FOLDER;
		case AssetKind::Texture: return EditorPalette::ICON_TEXTURE;
		case AssetKind::Model:   return EditorPalette::ICON_MODEL;
		case AssetKind::Scene:   return EditorPalette::ICON_SCENE;
		default:                 return EditorPalette::ICON_NEUTRAL;
		}
	}

	const char* KindName(AssetKind kind)
	{
		switch (kind)
		{
		case AssetKind::Folder:  return "Folder";
		case AssetKind::Texture: return "Texture";
		case AssetKind::Model:   return "Model";
		case AssetKind::Scene:   return "Scene";
		default:                 return "File";
		}
	}

	std::string FormatBytes(size_t bytes)
	{
		char buffer[64];
		const double value = static_cast<double>(bytes);
		if (bytes >= 1024 * 1024) std::snprintf(buffer, sizeof(buffer), "%.2f MB", value / (1024.0 * 1024.0));
		else if (bytes >= 1024) std::snprintf(buffer, sizeof(buffer), "%.1f KB", value / 1024.0);
		else std::snprintf(buffer, sizeof(buffer), "%zu B", bytes);
		return buffer;
	}

	// Imagen de una textura cargada (en OpenGL la fila 0 es la de abajo: se invierte la V)
	void DrawTexturePreview(const ResourceTexture* texture, float maxSide)
	{
		if (texture == nullptr || texture->GetGLID() == 0 || texture->GetWidth() <= 0 || texture->GetHeight() <= 0) return;
		const float aspect = static_cast<float>(texture->GetWidth()) / static_cast<float>(texture->GetHeight());
		const ImVec2 size = (aspect >= 1.0f) ? ImVec2(maxSide, maxSide / aspect) : ImVec2(maxSide * aspect, maxSide);
		ImGui::Image((ImTextureID)(intptr_t)texture->GetGLID(), size, ImVec2(0, 1), ImVec2(1, 0));
	}

	const ResourceTexture* GetTextureResource(const ResourceManager* resources, UID uid)
	{
		const Resource* resource = resources ? resources->GetResource(uid) : nullptr;
		return (resource && resource->GetType() == ResourceType::Texture) ? static_cast<const ResourceTexture*>(resource) : nullptr;
	}

	// Texto recortado con "..." para que quepa en 'width' (nombres bajo los iconos)
	std::string Ellipsize(const std::string& text, float width)
	{
		if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
		std::string result = text;
		while (!result.empty() && ImGui::CalcTextSize((result + "...").c_str()).x > width) result.pop_back();
		return result + "...";
	}

	bool IsValidFileName(const std::string& name)
	{
		return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:*?\"<>|") == std::string::npos;
	}

	FileSystem::DirectoryEntry MakeEntry(const std::string& path)
	{
		FileSystem::DirectoryEntry entry;
		entry.path = path;
		entry.name = FileSystem::GetFileName(path);
		entry.isDirectory = FileSystem::IsDirectory(path);
		return entry;
	}
}

// =====================================================================================
// OPERACIONES DIFERIDAS
// =====================================================================================
void ModuleUI::QueueAssetOperation(AssetOperation operation, const std::string& path, const std::string& newPath)
{
	PendingAssetOperation pending;
	pending.operation = operation;
	pending.path = path;
	pending.newPath = newPath;
	pending.textureSettings = editingTextureSettings;
	pending.modelSettings = editingModelSettings;
	pendingAssetOperations.push_back(std::move(pending));
}

void ModuleUI::ProcessPendingAssetOperations()
{
	if (pendingAssetOperations.empty()) return;

	ResourceManager* resources = Engine::GetInstance().resources.get();
	if (resources == nullptr)
	{
		pendingAssetOperations.clear();
		return;
	}

	// Las miniaturas pueden ser de texturas que se van a borrar o reimportar
	ReleaseThumbnails();

	std::vector<PendingAssetOperation> operations;
	operations.swap(pendingAssetOperations);

	for (const PendingAssetOperation& pending : operations)
	{
		switch (pending.operation)
		{
		case AssetOperation::Delete:
			resources->DeleteAsset(pending.path);
			break;
		case AssetOperation::DeleteFolder:
			resources->DeleteFolder(pending.path);
			break;
		case AssetOperation::Reimport:
			resources->ImportAsset(pending.path, true);
			break;
		case AssetOperation::ApplyTextureSettings:
			if (resources->SetTextureImportSettings(pending.path, pending.textureSettings)) savedTextureSettings = pending.textureSettings;
			break;
		case AssetOperation::ApplyModelSettings:
			if (resources->SetModelImportSettings(pending.path, pending.modelSettings)) savedModelSettings = pending.modelSettings;
			break;
		case AssetOperation::Refresh:
			resources->RefreshAssets();
			break;
		case AssetOperation::Rename:
			if (resources->RenameAsset(pending.path, pending.newPath))
			{
				const auto remap = [&](std::string& path)
				{
					if (path == pending.path) path = pending.newPath;
					else if (!path.empty() && FileSystem::IsInsideFolder(path, pending.path)) path = pending.newPath + path.substr(pending.path.size());
				};
				remap(selectedAssetPath);
				remap(assetsCurrentFolder);
				remap(editingSettingsPath);
			}
			break;
		}

		// Si lo que se ha borrado estaba seleccionado, el Inspector deja de mostrarlo
		if ((pending.operation == AssetOperation::Delete || pending.operation == AssetOperation::DeleteFolder) &&
			!selectedAssetPath.empty() && FileSystem::IsInsideFolder(selectedAssetPath, pending.path))
		{
			selectedAssetPath.clear();
			editingSettingsPath.clear();
			inspectorShowsAsset = false;
		}
	}
	InvalidateAssetsCache();
}

// =====================================================================================
// CACHE DE CARPETAS, SELECCION Y ABRIR
// =====================================================================================
std::vector<FileSystem::DirectoryEntry> ModuleUI::GetCachedDirectory(const std::string& folder)
{
	auto it = assetsDirectoryCache.find(folder);
	if (it == assetsDirectoryCache.end())
	{
		std::vector<FileSystem::DirectoryEntry> entries = FileSystem::ListDirectory(folder);
		// Los .meta son ficheros internos del motor: no se muestran
		entries.erase(std::remove_if(entries.begin(), entries.end(), [](const FileSystem::DirectoryEntry& entry)
			{
				return !entry.isDirectory && FileSystem::GetExtensionLower(entry.name) == FileSystem::META_EXTENSION;
			}), entries.end());
		it = assetsDirectoryCache.emplace(folder, std::move(entries)).first;
	}
	return it->second; // copia: el que llama puede invalidar la cache sin problemas
}

std::vector<FileSystem::DirectoryEntry> ModuleUI::GetProjectSearchResults()
{
	if (!allAssetFilesValid)
	{
		allAssetFiles.clear();
		FileSystem::ListFilesRecursive(FileSystem::ASSETS_DIR, allAssetFiles);
		std::sort(allAssetFiles.begin(), allAssetFiles.end());
		allAssetFilesValid = true;
	}

	const std::string searchLower = ToLower(projectSearch);
	std::vector<FileSystem::DirectoryEntry> results;
	for (const std::string& file : allAssetFiles)
	{
		if (FileSystem::GetExtensionLower(file) == FileSystem::META_EXTENSION) continue;
		if (projectFavorite == 0 && !ResourceManager::IsModelFile(file)) continue;
		if (projectFavorite == 1 && !ResourceManager::IsTextureFile(file)) continue;
		if (projectFavorite == 2 && !ResourceManager::IsSceneFile(file)) continue;
		if (!ContainsCaseInsensitive(FileSystem::GetFileName(file), searchLower)) continue;
		results.push_back(MakeEntry(file));
	}
	return results;
}

void ModuleUI::SelectAsset(const std::string& path)
{
	selectedAssetPath = path;
	inspectorShowsAsset = true;
}

void ModuleUI::SetProjectFolder(const std::string& folder)
{
	if (folder == assetsCurrentFolder && projectFavorite < 0 && projectSearch[0] == '\0') return;
	assetsCurrentFolder = folder;
	projectFavorite = -1;
	projectSearch[0] = '\0';
	renamingAssetPath.clear();
	ReleaseThumbnails();
}

void ModuleUI::OpenAsset(const std::string& path, bool isDirectory)
{
	ResourceManager* resources = Engine::GetInstance().resources.get();
	Scene* scene = Engine::GetInstance().scene.get();
	if (resources == nullptr || scene == nullptr) return;

	if (isDirectory)
	{
		SetProjectFolder(path);
		return;
	}

	if (ResourceManager::IsModelFile(path))
	{
		// Doble clic en un modelo: se instancia en la escena (en el pivote de la vista)
		undo.RecordSnapshot("Instantiate Model");
		if (GameObject* instance = scene->InstantiateModel(resources->GetAssetUID(path)))
		{
			SelectGameObject(instance);
		}
	}
	else if (ResourceManager::IsTextureFile(path))
	{
		// Doble clic en una textura: se aplica al GameObject seleccionado
		if (scene->GetSelected() != nullptr)
		{
			undo.RecordSnapshot("Assign Texture");
			ImporterManager::AssignTextureToHierarchy(resources->GetAssetUID(path), scene->GetSelected());
		}
		else
		{
			LOG_WARNING("[Assets] Select a GameObject to apply the texture to");
		}
	}
	else if (ResourceManager::IsSceneFile(path))
	{
		scene->RequestLoadScene(path);
	}
}

// =====================================================================================
// MINIATURAS (texturas cargadas en la GPU mientras se ven en el Project)
// =====================================================================================
unsigned int ModuleUI::GetAssetThumbnail(UID textureUID)
{
	ResourceManager* resources = Engine::GetInstance().resources.get();
	if (resources == nullptr || textureUID == INVALID_UID) return 0;

	const ResourceTexture* texture = GetTextureResource(resources, textureUID);
	if (texture == nullptr) return 0;
	if (texture->IsLoaded()) return texture->GetGLID();

	// Se pide la textura (+1 referencia) para poder ver la miniatura; se suelta al cambiar de carpeta
	if (thumbnailRefs.size() >= MAX_THUMBNAILS) return 0;
	if (thumbnailRefs.insert(textureUID).second)
	{
		resources->RequestResource(textureUID);
	}
	texture = GetTextureResource(resources, textureUID);
	return (texture && texture->IsLoaded()) ? texture->GetGLID() : 0;
}

void ModuleUI::ReleaseThumbnails()
{
	ResourceManager* resources = Engine::GetInstance().resources.get();
	if (resources != nullptr)
	{
		for (UID uid : thumbnailRefs) resources->ReleaseResource(uid);
	}
	thumbnailRefs.clear();
}

// =====================================================================================
// RENOMBRAR (F2)
// =====================================================================================
void ModuleUI::BeginAssetRename(const std::string& path)
{
	if (path.empty() || path == FileSystem::ASSETS_DIR) return;
	renamingAssetPath = path;
	const std::string name = FileSystem::IsDirectory(path) ? FileSystem::GetFileName(path) : FileSystem::GetFileNameNoExtension(path);
	strncpy_s(assetRenameBuffer, sizeof(assetRenameBuffer), name.c_str(), _TRUNCATE);
	assetRenameFocusPending = true;
}

void ModuleUI::CommitAssetRename()
{
	if (renamingAssetPath.empty()) return;
	const std::string oldPath = renamingAssetPath;
	renamingAssetPath.clear();

	const std::string newName = assetRenameBuffer;
	if (!IsValidFileName(newName)) return;

	const bool isFolder = FileSystem::IsDirectory(oldPath);
	const std::string extension = isFolder ? std::string() : FileSystem::GetExtensionLower(oldPath);
	const std::string originalExtension = isFolder ? std::string() : oldPath.substr(oldPath.size() - extension.size());
	const std::string newPath = FileSystem::Join(FileSystem::GetParentPath(oldPath), newName + originalExtension);
	if (newPath == oldPath) return;
	if (FileSystem::Exists(newPath))
	{
		LOG_WARNING("[Assets] '%s' already exists", newPath.c_str());
		return;
	}
	QueueAssetOperation(AssetOperation::Rename, oldPath, newPath);
}

// =====================================================================================
// VENTANA PROJECT
// =====================================================================================
void ModuleUI::DrawProjectPanel()
{
	projectFocused = false;
	if (!showProject) return;

	ResourceManager* resources = Engine::GetInstance().resources.get();

	// El listado de carpetas se cachea y se relee como mucho una vez por segundo: asi la ventana
	// "reacciona" sola si se anaden o borran ficheros desde fuera del motor
	const uint64_t now = SDL_GetTicks();
	if (assetsCacheInvalid || now - assetsCacheTime > ASSETS_CACHE_LIFETIME_MS)
	{
		assetsDirectoryCache.clear();
		allAssetFilesValid = false;
		assetsCacheTime = now;
		assetsCacheInvalid = false;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin(EditorWindows::PROJECT, &showProject);
	ImGui::PopStyleVar();
	if (!open)
	{
		ImGui::End();
		return;
	}
	projectFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	// Si la carpeta abierta ya no existe (borrada desde fuera) volvemos a Assets
	if (!FileSystem::IsDirectory(assetsCurrentFolder)) assetsCurrentFolder = FileSystem::ASSETS_DIR;
	if (resources) resources->SetImportFolder(assetsCurrentFolder);

	DrawProjectToolbar();

	// --------------------- Arbol de carpetas (izquierda) --------------------
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
	ImGui::BeginChild("ProjectTree", ImVec2(220.0f, 0.0f), ImGuiChildFlags_ResizeX | ImGuiChildFlags_AlwaysUseWindowPadding);
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));

		// Favoritos (busquedas guardadas, como en Unity)
		const ImVec2 favPos = ImGui::GetCursorScreenPos();
		if (ImGui::TreeNodeEx("##Favorites", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding, "%s", ""))
		{
			EditorIcons::DrawIconLabel(ImVec2(favPos.x + ImGui::GetTreeNodeToLabelSpacing(), favPos.y + 2.0f), ICON_FA_STAR, EditorPalette::ICON_FOLDER, "Favorites", ImGui::GetStyleColorVec4(ImGuiCol_Text));
			const char* favorites[] = { "All Models", "All Textures", "All Scenes" };
			for (int i = 0; i < 3; ++i)
			{
				const ImVec2 pos = ImGui::GetCursorScreenPos();
				if (ImGui::TreeNodeEx(favorites[i], ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
					ImGuiTreeNodeFlags_FramePadding | (projectFavorite == i ? ImGuiTreeNodeFlags_Selected : 0), "%s", ""))
				{
				}
				EditorIcons::DrawIconLabel(ImVec2(pos.x + ImGui::GetTreeNodeToLabelSpacing(), pos.y + 2.0f), ICON_FA_MAGNIFYING_GLASS, ImVec4(0.7f, 0.7f, 0.7f, 1.0f), favorites[i], ImGui::GetStyleColorVec4(ImGuiCol_Text));
				if (ImGui::IsItemClicked())
				{
					ReleaseThumbnails();
					projectFavorite = i;
					projectSearch[0] = '\0';
				}
			}
			ImGui::TreePop();
		}
		else
		{
			EditorIcons::DrawIconLabel(ImVec2(favPos.x + ImGui::GetTreeNodeToLabelSpacing(), favPos.y + 2.0f), ICON_FA_STAR, EditorPalette::ICON_FOLDER, "Favorites", ImGui::GetStyleColorVec4(ImGuiCol_Text));
		}
		ImGui::Spacing();
		DrawProjectFolderTree(FileSystem::ASSETS_DIR);
		ImGui::PopStyleVar(2);
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();

	ImGui::SameLine(0.0f, 0.0f);

	// --------------------- Contenido de la carpeta (derecha) -----------------
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.06f));
	ImGui::BeginChild("ProjectContentArea", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleColor();
	DrawProjectContent();
	ImGui::EndChild();

	// Atajos del Project: Supr = borrar, F2 = renombrar, Ctrl+D = duplicar no aplica
	if (projectFocused && !selectedAssetPath.empty() && !ImGui::GetIO().WantTextInput && renamingAssetPath.empty())
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			pendingDeletePath = selectedAssetPath;
			pendingDeleteIsFolder = FileSystem::IsDirectory(selectedAssetPath);
			openDeletePopup = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) BeginAssetRename(selectedAssetPath);
		if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) OpenAsset(selectedAssetPath, FileSystem::IsDirectory(selectedAssetPath));
	}
	if (projectFocused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && assetsCurrentFolder != FileSystem::ASSETS_DIR)
	{
		SetProjectFolder(FileSystem::GetParentPath(assetsCurrentFolder));
	}

	ImGui::End();
}

void ModuleUI::DrawProjectToolbar()
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorPalette::PANEL_TOOLBAR);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
	ImGui::BeginChild("ProjectToolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + 4.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (FlatButton(ICON_FA_PLUS " " ICON_FA_CARET_DOWN "##ProjectCreate")) ImGui::OpenPopup("ProjectCreatePopup");
	IconTooltip("Create");
	if (ImGui::BeginPopup("ProjectCreatePopup"))
	{
		if (ImGui::MenuItem(ICON_FA_FOLDER "  Folder")) openNewFolderPopup = true;
		ImGui::Separator();
		if (ImGui::MenuItem(ICON_FA_CLAPPERBOARD "  Scene (save current as...)")) openSaveScenePopup = true;
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	if (FlatButton(ICON_FA_ARROWS_ROTATE "##ProjectRefresh")) QueueAssetOperation(AssetOperation::Refresh, std::string());
	IconTooltip("Refresh (Ctrl+R): import new files and clean deleted ones");

	const float searchWidth = std::min(260.0f, std::max(100.0f, ImGui::GetContentRegionAvail().x * 0.4f));
	ImGui::SameLine(ImGui::GetWindowWidth() - searchWidth - 8.0f);
	ImGui::SetNextItemWidth(searchWidth);
	char previousSearch[sizeof(projectSearch)];
	memcpy(previousSearch, projectSearch, sizeof(projectSearch));
	if (ImGui::InputTextWithHint("##ProjectSearch", ICON_FA_MAGNIFYING_GLASS " Search", projectSearch, sizeof(projectSearch)))
	{
		if (projectSearch[0] == '\0' || previousSearch[0] == '\0') ReleaseThumbnails();
	}

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
}

void ModuleUI::DrawProjectFolderTree(const std::string& folder)
{
	const std::vector<FileSystem::DirectoryEntry> entries = GetCachedDirectory(folder);
	const bool hasSubfolders = std::any_of(entries.begin(), entries.end(),
		[](const FileSystem::DirectoryEntry& entry) { return entry.isDirectory; });

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
	if (folder == FileSystem::ASSETS_DIR) flags |= ImGuiTreeNodeFlags_DefaultOpen;
	if (!hasSubfolders) flags |= ImGuiTreeNodeFlags_Leaf;
	const bool isCurrent = (folder == assetsCurrentFolder && projectFavorite < 0 && projectSearch[0] == '\0');
	if (isCurrent) flags |= ImGuiTreeNodeFlags_Selected;
	// La carpeta abierta (y sus padres) se despliegan
	if (FileSystem::IsInsideFolder(assetsCurrentFolder, folder) && folder != assetsCurrentFolder) ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);

	const ImVec2 nodePos = ImGui::GetCursorScreenPos();
	const bool open = ImGui::TreeNodeEx(folder.c_str(), flags, "%s", "");
	EditorIcons::DrawIconLabel(ImVec2(nodePos.x + ImGui::GetTreeNodeToLabelSpacing(), nodePos.y + 2.0f),
		isCurrent ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER, EditorPalette::ICON_FOLDER,
		FileSystem::GetFileName(folder).c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		SetProjectFolder(folder);
	}

	// Soltar un asset sobre una carpeta del arbol: se mueve ahi
	if (ImGui::BeginDragDropTarget())
	{
		const char* types[] = { EditorDragDrop::ASSET_TEXTURE, EditorDragDrop::ASSET_MODEL, EditorDragDrop::ASSET_SCENE, EditorDragDrop::ASSET_FOLDER, EditorDragDrop::ASSET_OTHER };
		for (const char* type : types)
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type))
			{
				const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(payload->Data, payload->DataSize);
				const std::string target = FileSystem::Join(folder, FileSystem::GetFileName(asset.path));
				if (target != asset.path) QueueAssetOperation(AssetOperation::Rename, asset.path, target);
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (open)
	{
		for (const FileSystem::DirectoryEntry& entry : entries)
		{
			if (entry.isDirectory) DrawProjectFolderTree(entry.path);
		}
		ImGui::TreePop();
	}
}

void ModuleUI::DrawProjectContent()
{
	// ---------------------- Ruta (breadcrumbs) clicable ----------------------
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 3.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
	ImGui::BeginChild("ProjectBreadcrumbs", ImVec2(0.0f, ImGui::GetFrameHeight() + 6.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const bool searching = projectSearch[0] != '\0' || projectFavorite >= 0;
	if (searching)
	{
		ImGui::AlignTextToFramePadding();
		if (projectFavorite >= 0)
		{
			const char* favorites[] = { "All Models", "All Textures", "All Scenes" };
			ImGui::TextUnformatted(favorites[projectFavorite]);
		}
		else
		{
			ImGui::Text("Search: In Assets  \"%s\"", projectSearch);
		}
	}
	else
	{
		// "Assets > Carpeta > Subcarpeta": cada parte lleva a esa carpeta
		std::vector<std::string> parts;
		std::string path = assetsCurrentFolder;
		while (!path.empty())
		{
			parts.push_back(path);
			if (path == FileSystem::ASSETS_DIR) break;
			const std::string parent = FileSystem::GetParentPath(path);
			if (parent == path) break;
			path = parent;
		}
		std::reverse(parts.begin(), parts.end());
		for (size_t i = 0; i < parts.size(); ++i)
		{
			if (i > 0)
			{
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled(ICON_FA_ANGLE_RIGHT);
				ImGui::SameLine();
			}
			ImGui::PushID(static_cast<int>(i));
			const bool last = (i + 1 == parts.size());
			if (last) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
			if (FlatButton(FileSystem::GetFileName(parts[i]).c_str())) SetProjectFolder(parts[i]);
			if (last) ImGui::PopStyleColor();
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(3);

	// ---------------------- Lista / cuadricula ----------------------
	const float bottomBarHeight = ImGui::GetFrameHeight() + 6.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
	ImGui::BeginChild("ProjectItems", ImVec2(0.0f, -bottomBarHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
	{
		const std::vector<FileSystem::DirectoryEntry> entries = searching ? GetProjectSearchResults() : GetCachedDirectory(assetsCurrentFolder);
		if (entries.empty())
		{
			ImGui::TextDisabled(searching ? "No results" : "This folder is empty.\nDrag files from the OS file explorer to import them here.");
		}

		const bool listMode = projectIconSize <= LIST_MODE_ICON_SIZE;
		if (listMode)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
			for (const FileSystem::DirectoryEntry& entry : entries) DrawProjectListItem(entry);
			ImGui::PopStyleVar();
		}
		else
		{
			const float tileWidth = projectIconSize + 20.0f;
			const float availWidth = ImGui::GetContentRegionAvail().x;
			const int columns = std::max(1, static_cast<int>((availWidth + 6.0f) / (tileWidth + 6.0f)));
			int column = 0;
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));

			ResourceManager* resources = Engine::GetInstance().resources.get();
			for (const FileSystem::DirectoryEntry& entry : entries)
			{
				if (column > 0) ImGui::SameLine();
				DrawProjectGridItem(entry, tileWidth);
				column = (column + 1) % columns;

				// Mallas de un modelo desplegado (sub-assets, como en Unity)
				if (GetAssetKind(entry) == AssetKind::Model && expandedModels.count(entry.path) > 0 && resources)
				{
					const std::vector<UID> uids = resources->GetAssetResources(entry.path);
					for (size_t i = 1; i < uids.size(); ++i)
					{
						const Resource* mesh = resources->GetResource(uids[i]);
						if (mesh == nullptr) continue;
						if (column > 0) ImGui::SameLine();
						column = (column + 1) % columns;

						ImGui::PushID(entry.path.c_str());
						ImGui::PushID(static_cast<int>(i));
						const ImVec2 pos = ImGui::GetCursorScreenPos();
						const ImVec2 tile(tileWidth, projectIconSize + ImGui::GetTextLineHeight() + 12.0f);
						ImGui::InvisibleButton("##meshTile", tile);
						ImDrawList* drawList = ImGui::GetWindowDrawList();
						drawList->AddRectFilled(pos, ImVec2(pos.x + tile.x, pos.y + tile.y), IM_COL32(255, 255, 255, ImGui::IsItemHovered() ? 22 : 10), 3.0f);
						const float iconSize = projectIconSize * 0.55f;
						const ImVec2 iconTextSize = ImGui::GetFont()->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, ICON_FA_DRAW_POLYGON);
						drawList->AddText(ImGui::GetFont(), iconSize, ImVec2(pos.x + (tile.x - iconTextSize.x) * 0.5f, pos.y + 4.0f + (projectIconSize - iconTextSize.y) * 0.5f),
							ImGui::GetColorU32(EditorPalette::ICON_MESH), ICON_FA_DRAW_POLYGON);
						const std::string name = Ellipsize(mesh->GetName(), tile.x - 4.0f);
						const float nameWidth = ImGui::CalcTextSize(name.c_str()).x;
						drawList->AddText(ImVec2(pos.x + (tile.x - nameWidth) * 0.5f, pos.y + projectIconSize + 8.0f), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
						if (ImGui::BeginDragDropSource())
						{
							const EditorDragDrop::AssetPayload payload = EditorDragDrop::MakeAssetPayload(mesh->GetUID(), entry.path);
							ImGui::SetDragDropPayload(EditorDragDrop::ASSET_MESH, &payload, sizeof(payload));
							ImGui::TextColored(EditorPalette::ICON_MESH, ICON_FA_DRAW_POLYGON);
							ImGui::SameLine();
							ImGui::TextUnformatted(mesh->GetName().c_str());
							ImGui::EndDragDropSource();
						}
						ImGui::SetItemTooltip("%s (Mesh)\nRefs: %u | Loaded: %s\nDrag it to the Scene view or to a Mesh field",
							mesh->GetName().c_str(), mesh->GetReferenceCount(), mesh->IsLoaded() ? "yes" : "no");
						ImGui::PopID();
						ImGui::PopID();
					}
				}
			}
			ImGui::PopStyleVar();
		}

		// Clic en zona vacia: deselecciona | clic derecho: menu de creacion
		if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			selectedAssetPath.clear();
			renamingAssetPath.clear();
		}
		if (ImGui::BeginPopupContextWindow("ProjectContentContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::BeginMenu("Create"))
			{
				if (ImGui::MenuItem(ICON_FA_FOLDER "  Folder")) openNewFolderPopup = true;
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Show in Explorer"))
			{
				const std::string url = "file:///" + FileSystem::ToDiskPath(assetsCurrentFolder);
				SDL_OpenURL(url.c_str());
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Refresh", "Ctrl+R")) QueueAssetOperation(AssetOperation::Refresh, std::string());
			ImGui::EndPopup();
		}

		// Rueda + Ctrl: cambia el tamano de los iconos (como Unity)
		if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
		{
			projectIconSize = std::clamp(projectIconSize + ImGui::GetIO().MouseWheel * 8.0f, 16.0f, 128.0f);
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();

	// ---------------------- Barra inferior: ruta seleccionada + tamano de iconos ----------------------
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorPalette::PANEL_TOOLBAR);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 3.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
	ImGui::BeginChild("ProjectBottomBar", ImVec2(0.0f, bottomBarHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::AlignTextToFramePadding();
	if (!selectedAssetPath.empty())
	{
		const AssetKind kind = GetAssetKind(MakeEntry(selectedAssetPath));
		ImGui::TextColored(KindColor(kind), "%s", KindIcon(kind));
		ImGui::SameLine();
		ImGui::TextUnformatted(selectedAssetPath.c_str());
	}
	else
	{
		ImGui::TextDisabled(" ");
	}
	ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowWidth() - 120.0f));
	ImGui::SetNextItemWidth(100.0f);
	ImGui::SliderFloat("##IconSize", &projectIconSize, 16.0f, 128.0f, "");
	ImGui::SetItemTooltip("Icon size (minimum = list view). Ctrl + wheel also works.");
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
}

// -------------------------------------------------------------------------------------
// Clic, doble clic, arrastrar, tooltip y menu contextual de un asset (cuadricula y lista)
// -------------------------------------------------------------------------------------
void ModuleUI::HandleAssetItemInteraction(const FileSystem::DirectoryEntry& entry, UID assetUID)
{
	ResourceManager* resources = Engine::GetInstance().resources.get();
	const AssetKind kind = GetAssetKind(entry);

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		SelectAsset(entry.path);
		if (renamingAssetPath != entry.path) renamingAssetPath.clear();
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		OpenAsset(entry.path, entry.isDirectory);
	}

	// Origen de drag & drop (todos los assets y carpetas: se pueden mover a otra carpeta)
	const char* payloadType = EditorDragDrop::ASSET_OTHER;
	if (kind == AssetKind::Texture) payloadType = EditorDragDrop::ASSET_TEXTURE;
	else if (kind == AssetKind::Model) payloadType = EditorDragDrop::ASSET_MODEL;
	else if (kind == AssetKind::Scene) payloadType = EditorDragDrop::ASSET_SCENE;
	else if (kind == AssetKind::Folder) payloadType = EditorDragDrop::ASSET_FOLDER;
	if (renamingAssetPath != entry.path && ImGui::BeginDragDropSource())
	{
		const EditorDragDrop::AssetPayload payload = EditorDragDrop::MakeAssetPayload(assetUID, entry.path);
		ImGui::SetDragDropPayload(payloadType, &payload, sizeof(payload));
		ImGui::TextColored(KindColor(kind), "%s", KindIcon(kind));
		ImGui::SameLine();
		ImGui::TextUnformatted(entry.name.c_str());
		ImGui::EndDragDropSource();
	}

	// Una carpeta acepta assets (se mueven dentro)
	if (kind == AssetKind::Folder && ImGui::BeginDragDropTarget())
	{
		const char* types[] = { EditorDragDrop::ASSET_TEXTURE, EditorDragDrop::ASSET_MODEL, EditorDragDrop::ASSET_SCENE, EditorDragDrop::ASSET_FOLDER, EditorDragDrop::ASSET_OTHER };
		for (const char* type : types)
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type))
			{
				const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(payload->Data, payload->DataSize);
				const std::string target = FileSystem::Join(entry.path, FileSystem::GetFileName(asset.path));
				if (asset.path != entry.path && target != asset.path) QueueAssetOperation(AssetOperation::Rename, asset.path, target);
			}
		}
		ImGui::EndDragDropTarget();
	}

	// Tooltip con informacion del recurso
	if (kind != AssetKind::Folder && ImGui::BeginItemTooltip())
	{
		ImGui::TextUnformatted(entry.name.c_str());
		ImGui::TextDisabled("%s | %s", KindName(kind), entry.path.c_str());
		const Resource* resource = resources ? resources->GetResource(assetUID) : nullptr;
		if (resource)
		{
			ImGui::TextDisabled("UID: %llu | Refs: %u | Loaded: %s", (unsigned long long)resource->GetUID(), resource->GetReferenceCount(), resource->IsLoaded() ? "yes" : "no");
		}
		ImGui::EndTooltip();
	}

	DrawAssetContextMenu(entry);
}

void ModuleUI::DrawProjectGridItem(const FileSystem::DirectoryEntry& entry, float tileWidth)
{
	ResourceManager* resources = Engine::GetInstance().resources.get();
	const AssetKind kind = GetAssetKind(entry);
	const bool isSelected = (selectedAssetPath == entry.path);
	const bool renaming = (renamingAssetPath == entry.path);
	const UID assetUID = (resources && kind != AssetKind::Folder) ? resources->GetAssetUID(entry.path) : INVALID_UID;

	ImGui::PushID(entry.path.c_str());
	// Todo el tile es un grupo: para ImGui cuenta como un unico elemento (las filas quedan alineadas
	// aunque dentro haya otros widgets, como la flecha de los modelos o el campo de renombrar)
	ImGui::BeginGroup();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float textHeight = ImGui::GetTextLineHeight();
	const ImVec2 tile(tileWidth, projectIconSize + textHeight + 12.0f);

	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##tile", tile);
	const bool hovered = ImGui::IsItemHovered();
	HandleAssetItemInteraction(entry, assetUID);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 iconMin(pos.x + (tile.x - projectIconSize) * 0.5f, pos.y + 4.0f);
	const ImVec2 iconMax(iconMin.x + projectIconSize, iconMin.y + projectIconSize);
	if (hovered && !isSelected) drawList->AddRectFilled(pos, ImVec2(pos.x + tile.x, pos.y + tile.y), IM_COL32(255, 255, 255, 14), 3.0f);
	if (isSelected) drawList->AddRectFilled(ImVec2(iconMin.x - 3.0f, iconMin.y - 3.0f), ImVec2(iconMax.x + 3.0f, iconMax.y + 3.0f), IM_COL32(60, 95, 140, 120), 3.0f);

	// ----- Icono o miniatura -----
	const unsigned int thumbnail = (kind == AssetKind::Texture) ? GetAssetThumbnail(assetUID) : 0;
	const ResourceTexture* texture = (kind == AssetKind::Texture) ? GetTextureResource(resources, assetUID) : nullptr;
	if (thumbnail != 0 && texture && texture->GetWidth() > 0 && texture->GetHeight() > 0)
	{
		const float aspect = static_cast<float>(texture->GetWidth()) / static_cast<float>(texture->GetHeight());
		ImVec2 size = (aspect >= 1.0f) ? ImVec2(projectIconSize, projectIconSize / aspect) : ImVec2(projectIconSize * aspect, projectIconSize);
		const ImVec2 imgMin(iconMin.x + (projectIconSize - size.x) * 0.5f, iconMin.y + (projectIconSize - size.y) * 0.5f);
		drawList->AddImage((ImTextureID)(intptr_t)thumbnail, imgMin, ImVec2(imgMin.x + size.x, imgMin.y + size.y), ImVec2(0, 1), ImVec2(1, 0));
	}
	else
	{
		const char* icon = KindIcon(kind);
		const float iconSize = projectIconSize * 0.78f;
		const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
		drawList->AddText(ImGui::GetFont(), iconSize, ImVec2(iconMin.x + (projectIconSize - textSize.x) * 0.5f, iconMin.y + (projectIconSize - textSize.y) * 0.5f),
			ImGui::GetColorU32(KindColor(kind)), icon);
	}

	// ----- Flecha para desplegar las mallas de un modelo -----
	if (kind == AssetKind::Model && resources && resources->GetAssetResources(entry.path).size() > 1)
	{
		const bool expanded = expandedModels.count(entry.path) > 0;
		const float arrowSize = std::max(14.0f, projectIconSize * 0.25f);
		const ImVec2 arrowPos(pos.x + tile.x - arrowSize - 1.0f, iconMin.y + (projectIconSize - arrowSize) * 0.5f);
		const ImVec2 cursorBackup = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(arrowPos);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		if (FlatButton(expanded ? ICON_FA_ANGLE_LEFT "##collapse" : ICON_FA_ANGLE_RIGHT "##expand", ImVec2(arrowSize, arrowSize)))
		{
			if (expanded) expandedModels.erase(entry.path);
			else expandedModels.insert(entry.path);
		}
		ImGui::PopStyleVar();
		IconTooltip(expanded ? "Hide meshes" : "Show meshes");
		ImGui::SetCursorScreenPos(cursorBackup);
	}

	// ----- Nombre (o campo para renombrar) -----
	const std::string displayName = entry.isDirectory ? entry.name : FileSystem::GetFileNameNoExtension(entry.name);
	const float nameY = iconMax.y + 4.0f;
	if (renaming)
	{
		const ImVec2 cursorBackup = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(pos.x, nameY - 2.0f));
		ImGui::SetNextItemWidth(tile.x);
		if (assetRenameFocusPending)
		{
			ImGui::SetKeyboardFocusHere();
			assetRenameFocusPending = false;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
		const bool enter = ImGui::InputText("##assetRename", assetRenameBuffer, sizeof(assetRenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		ImGui::PopStyleVar();
		const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
		if (enter || (ImGui::IsItemDeactivated() && !cancel)) CommitAssetRename();
		else if (cancel) renamingAssetPath.clear();
		ImGui::SetCursorScreenPos(cursorBackup);
	}
	else
	{
		const std::string name = Ellipsize(displayName, tile.x - 4.0f);
		const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
		const ImVec2 namePos(pos.x + (tile.x - nameSize.x) * 0.5f, nameY);
		if (isSelected) drawList->AddRectFilled(ImVec2(namePos.x - 3.0f, namePos.y - 1.0f), ImVec2(namePos.x + nameSize.x + 3.0f, namePos.y + nameSize.y + 1.0f), IM_COL32(44, 93, 135, 255), 2.0f);
		drawList->AddText(namePos, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
	}

	// El grupo ocupa exactamente el tamano del tile
	ImGui::SetCursorScreenPos(pos);
	ImGui::Dummy(tile);
	ImGui::EndGroup();
	ImGui::PopID();
}

void ModuleUI::DrawProjectListItem(const FileSystem::DirectoryEntry& entry)
{
	ResourceManager* resources = Engine::GetInstance().resources.get();
	const AssetKind kind = GetAssetKind(entry);
	const bool isSelected = (selectedAssetPath == entry.path);
	const UID assetUID = (resources && kind != AssetKind::Folder) ? resources->GetAssetUID(entry.path) : INVALID_UID;

	ImGui::PushID(entry.path.c_str());
	const ImVec2 labelPos = ImGui::GetCursorScreenPos();
	ImGui::Selectable("##entry", isSelected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns);
	HandleAssetItemInteraction(entry, assetUID);

	if (renamingAssetPath == entry.path)
	{
		const ImVec2 cursorBackup = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(labelPos.x + 22.0f, labelPos.y - 1.0f));
		ImGui::SetNextItemWidth(220.0f);
		if (assetRenameFocusPending)
		{
			ImGui::SetKeyboardFocusHere();
			assetRenameFocusPending = false;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
		const bool enter = ImGui::InputText("##assetRename", assetRenameBuffer, sizeof(assetRenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		ImGui::PopStyleVar();
		const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
		if (enter || (ImGui::IsItemDeactivated() && !cancel)) CommitAssetRename();
		else if (cancel) renamingAssetPath.clear();
		ImGui::SetCursorScreenPos(cursorBackup);
		ImGui::Dummy(ImVec2(0, 0));
	}
	else
	{
		const std::string displayName = entry.isDirectory ? entry.name : FileSystem::GetFileNameNoExtension(entry.name);
		EditorIcons::DrawIconLabel(labelPos, KindIcon(kind), KindColor(kind), displayName.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
	}
	ImGui::PopID();
}

void ModuleUI::DrawAssetContextMenu(const FileSystem::DirectoryEntry& entry)
{
	if (!ImGui::BeginPopupContextItem("AssetContext")) return;

	Scene* scene = Engine::GetInstance().scene.get();
	const AssetKind kind = GetAssetKind(entry);

	switch (kind)
	{
	case AssetKind::Folder:
		if (ImGui::MenuItem("Open")) SetProjectFolder(entry.path);
		break;
	case AssetKind::Model:
		if (ImGui::MenuItem("Instantiate in Scene")) OpenAsset(entry.path, false);
		if (ImGui::MenuItem("Reimport")) QueueAssetOperation(AssetOperation::Reimport, entry.path);
		break;
	case AssetKind::Texture:
		if (ImGui::MenuItem("Apply to Selected GameObject", nullptr, false, scene && scene->GetSelected())) OpenAsset(entry.path, false);
		if (ImGui::MenuItem("Reimport")) QueueAssetOperation(AssetOperation::Reimport, entry.path);
		break;
	case AssetKind::Scene:
		if (ImGui::MenuItem("Open Scene")) OpenAsset(entry.path, false);
		break;
	default:
		break;
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Rename", "F2")) BeginAssetRename(entry.path);
	if (ImGui::MenuItem("Show in Explorer"))
	{
		const std::string folder = entry.isDirectory ? entry.path : FileSystem::GetParentPath(entry.path);
		const std::string url = "file:///" + FileSystem::ToDiskPath(folder);
		SDL_OpenURL(url.c_str());
	}
	if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(entry.path.c_str());
	ImGui::Separator();
	if (ImGui::MenuItem("Delete", "Del"))
	{
		pendingDeletePath = entry.path;
		pendingDeleteIsFolder = entry.isDirectory;
		openDeletePopup = true;
	}
	ImGui::EndPopup();
}

void ModuleUI::DrawAssetsPopups()
{
	// Se llama fuera de cualquier ventana: OpenPopup y BeginPopupModal comparten asi el mismo ID
	if (openDeletePopup)
	{
		ImGui::OpenPopup("Delete Asset?");
		openDeletePopup = false;
	}
	if (ImGui::BeginPopupModal("Delete Asset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("%s", pendingDeletePath.c_str());
		if (pendingDeleteIsFolder)
		{
			ImGui::TextColored(EditorPalette::LEVEL_WARNING, "The folder and ALL its contents will be deleted.");
		}
		ImGui::TextDisabled("Its .meta and Library files are deleted too. You cannot undo this action.");
		ImGui::TextDisabled("GameObjects using it will lose the mesh / show the 'missing' texture.");
		ImGui::Spacing();

		if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f)))
		{
			QueueAssetOperation(pendingDeleteIsFolder ? AssetOperation::DeleteFolder : AssetOperation::Delete, pendingDeletePath);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (openNewFolderPopup)
	{
		ImGui::OpenPopup("New Folder");
		openNewFolderPopup = false;
	}
	if (ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Inside: %s", assetsCurrentFolder.c_str());
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		const bool enterPressed = ImGui::InputText("Name", newFolderName, sizeof(newFolderName), ImGuiInputTextFlags_EnterReturnsTrue);
		const std::string folderName = newFolderName;
		const bool validName = IsValidFileName(folderName);

		ImGui::BeginDisabled(!validName);
		if (ImGui::Button("Create", ImVec2(120.0f, 0.0f)) || (enterPressed && validName))
		{
			const std::string path = FileSystem::MakeUniquePath(FileSystem::Join(assetsCurrentFolder, folderName));
			if (FileSystem::CreateDirectories(path))
			{
				LOG("[Assets] Folder created: '%s'", path.c_str());
				SelectAsset(path);
				inspectorShowsAsset = false;
			}
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
// INSPECTOR DE UN ASSET (opciones de importacion)
// =====================================================================================
void ModuleUI::DrawAssetInspector()
{
	ResourceManager* resources = Engine::GetInstance().resources.get();
	Scene* scene = Engine::GetInstance().scene.get();
	const std::string path = selectedAssetPath;

	if (resources == nullptr || path.empty() || !FileSystem::Exists(path))
	{
		ImGui::TextDisabled("The selected asset no longer exists.");
		return;
	}

	const bool isFolder = FileSystem::IsDirectory(path);
	const AssetKind kind = GetAssetKind(MakeEntry(path));

	// Cabecera: icono grande + nombre + tipo
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
	ImGui::TextColored(KindColor(kind), "%s", KindIcon(kind));
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f);
	ImGui::TextUnformatted(FileSystem::GetFileName(path).c_str());
	ImGui::PopFont();
	ImGui::TextDisabled("%s Import Settings", KindName(kind));
	ImGui::EndGroup();
	ImGui::Separator();

	if (isFolder)
	{
		ImGui::TextDisabled("%s", path.c_str());
		if (ImGui::Button("Open Folder")) SetProjectFolder(path);
		return;
	}

	// Cuando cambia el asset seleccionado se leen sus opciones del .meta
	if (editingSettingsPath != path)
	{
		editingSettingsPath = path;
		savedTextureSettings = TextureImportSettings();
		savedModelSettings = ModelImportSettings();
		resources->GetTextureImportSettings(path, savedTextureSettings);
		resources->GetModelImportSettings(path, savedModelSettings);
		editingTextureSettings = savedTextureSettings;
		editingModelSettings = savedModelSettings;
	}

	const UID uid = resources->GetAssetUID(path);
	const Resource* resource = resources->GetResource(uid);

	// ------------------------------ TEXTURAS ------------------------------------------
	if (ResourceManager::IsTextureFile(path))
	{
		ImGui::SeparatorText("Import Settings");
		PropertyLabel("Flip X");
		ImGui::Checkbox("##FlipX", &editingTextureSettings.flipX);
		PropertyLabel("Flip Y");
		ImGui::Checkbox("##FlipY", &editingTextureSettings.flipY);

		static const char* minFilters[] = { "Point (no filter)", "Bilinear", "Trilinear" };
		PropertyLabel("Filter Mode (min)");
		ImGui::Combo("##MinFilter", &editingTextureSettings.minFilter, minFilters, IM_ARRAYSIZE(minFilters));
		static const char* magFilters[] = { "Point (no filter)", "Bilinear" };
		PropertyLabel("Filter Mode (mag)");
		ImGui::Combo("##MagFilter", &editingTextureSettings.magFilter, magFilters, IM_ARRAYSIZE(magFilters));
		static const char* wrapModes[] = { "Repeat", "Clamp", "Mirror" };
		PropertyLabel("Wrap Mode");
		ImGui::Combo("##Wrap", &editingTextureSettings.wrapMode, wrapModes, IM_ARRAYSIZE(wrapModes));

		static const int sizes[] = { 256, 512, 1024, 2048, 4096, 8192 };
		static const char* sizeNames[] = { "256", "512", "1024", "2048", "4096", "8192" };
		int sizeIndex = 3;
		for (int i = 0; i < IM_ARRAYSIZE(sizes); ++i)
		{
			if (sizes[i] == editingTextureSettings.maxSize) sizeIndex = i;
		}
		PropertyLabel("Max Size");
		if (ImGui::Combo("##MaxSize", &sizeIndex, sizeNames, IM_ARRAYSIZE(sizeNames))) editingTextureSettings.maxSize = sizes[sizeIndex];

		ImGui::Spacing();
		const bool changed = editingTextureSettings != savedTextureSettings;
		const float buttonWidth = 70.0f;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x);
		ImGui::BeginDisabled(!changed);
		if (ImGui::Button("Revert", ImVec2(buttonWidth, 0))) editingTextureSettings = savedTextureSettings;
		ImGui::SameLine();
		if (ImGui::Button("Apply", ImVec2(buttonWidth, 0))) QueueAssetOperation(AssetOperation::ApplyTextureSettings, path);
		ImGui::EndDisabled();

		ImGui::SeparatorText("Preview");
		if (resource)
		{
			const ResourceTexture* texture = GetTextureResource(resources, uid);
			const unsigned int glid = GetAssetThumbnail(uid);
			if (texture && glid != 0)
			{
				const float side = std::min(ImGui::GetContentRegionAvail().x, 256.0f);
				DrawTexturePreview(texture, side);
				ImGui::TextDisabled("%d x %d %s | %s", texture->GetWidth(), texture->GetHeight(), texture->GetChannels() == 4 ? "RGBA" : "RGB",
					FormatBytes(texture->GetMemoryUsage()).c_str());
			}
			ImGui::TextDisabled("UID %llu | refs %u | %s", (unsigned long long)uid, resource->GetReferenceCount(), resource->GetLibraryPath().c_str());
		}
		else
		{
			ImGui::TextColored(EditorPalette::LEVEL_ERROR, "This texture could not be imported (see the Console).");
		}

		ImGui::Spacing();
		ImGui::BeginDisabled(scene == nullptr || scene->GetSelected() == nullptr);
		if (ImGui::Button("Apply to Selected GameObject", ImVec2(-FLT_MIN, 0))) OpenAsset(path, false);
		ImGui::EndDisabled();
		if (ImGui::Button("Reimport", ImVec2(-FLT_MIN, 0))) QueueAssetOperation(AssetOperation::Reimport, path);
	}
	// ------------------------------ MODELOS -------------------------------------------
	else if (ResourceManager::IsModelFile(path))
	{
		ImGui::SeparatorText("Model");
		PropertyLabel("Scale Factor");
		ImGui::DragFloat("##Scale", &editingModelSettings.globalScale, 0.01f, 0.0001f, 10000.0f, "%.4f");
		if (editingModelSettings.globalScale <= 0.0f) editingModelSettings.globalScale = 0.0001f;
		PropertyLabel("Convert Units");
		ImGui::Checkbox("##ConvertUnits", &editingModelSettings.convertUnits);
		static const char* axes[] = { "+Y (unchanged)", "+Z (3ds Max / Blender)", "+X", "-Z", "-X", "-Y" };
		PropertyLabel("Up Axis");
		ImGui::Combo("##UpAxis", &editingModelSettings.upAxis, axes, IM_ARRAYSIZE(axes));
		PropertyLabel("Import Cameras");
		bool importCameras = !editingModelSettings.ignoreCameras;
		if (ImGui::Checkbox("##ImportCameras", &importCameras)) editingModelSettings.ignoreCameras = !importCameras;
		PropertyLabel("Import Lights");
		bool importLights = !editingModelSettings.ignoreLights;
		if (ImGui::Checkbox("##ImportLights", &importLights)) editingModelSettings.ignoreLights = !importLights;

		ImGui::Spacing();
		const bool changed = editingModelSettings != savedModelSettings;
		const float buttonWidth = 70.0f;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x);
		ImGui::BeginDisabled(!changed);
		if (ImGui::Button("Revert", ImVec2(buttonWidth, 0))) editingModelSettings = savedModelSettings;
		ImGui::SameLine();
		if (ImGui::Button("Apply", ImVec2(buttonWidth, 0))) QueueAssetOperation(AssetOperation::ApplyModelSettings, path);
		ImGui::EndDisabled();
		ImGui::TextDisabled("Changes apply to new instances of the model.");

		if (resource)
		{
			const std::vector<UID> uids = resources->GetAssetResources(path);
			ImGui::SeparatorText("Meshes");
			for (size_t i = 1; i < uids.size(); ++i)
			{
				const Resource* mesh = resources->GetResource(uids[i]);
				if (mesh == nullptr) continue;
				ImGui::TextColored(EditorPalette::ICON_MESH, ICON_FA_DRAW_POLYGON);
				ImGui::SameLine();
				ImGui::Text("%s", mesh->GetName().c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("refs %u %s", mesh->GetReferenceCount(), mesh->IsLoaded() ? "(loaded)" : "");
			}
			ImGui::Spacing();
			if (ImGui::Button("Instantiate in Scene", ImVec2(-FLT_MIN, 0))) OpenAsset(path, false);
		}
		else
		{
			ImGui::TextColored(EditorPalette::LEVEL_ERROR, "This model could not be imported (see the Console).");
		}
		if (ImGui::Button("Reimport", ImVec2(-FLT_MIN, 0))) QueueAssetOperation(AssetOperation::Reimport, path);
	}
	// ------------------------------ ESCENAS -------------------------------------------
	else if (ResourceManager::IsSceneFile(path))
	{
		ImGui::TextDisabled("Engine scene (.scene, JSON)");
		if (scene && ImGui::Button("Open Scene", ImVec2(-FLT_MIN, 0))) scene->RequestLoadScene(path);
	}
	else
	{
		ImGui::TextDisabled("File type not supported by the engine.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	if (ImGui::Button(ICON_FA_TRASH_CAN "  Delete Asset...", ImVec2(-FLT_MIN, 0)))
	{
		pendingDeletePath = path;
		pendingDeleteIsFolder = false;
		openDeletePopup = true;
	}
}

// =====================================================================================
// VENTANA RESOURCES (conteo de referencias)
// =====================================================================================
void ModuleUI::DrawResourcesPanel()
{
	if (!showResources) return;

	if (ImGui::Begin(EditorWindows::RESOURCES, &showResources))
	{
		ResourceManager* resources = Engine::GetInstance().resources.get();
		if (resources == nullptr)
		{
			ImGui::End();
			return;
		}

		const auto& all = resources->GetAllResources();
		ImGui::Text("Registered resources: %zu", all.size());
		ImGui::SameLine();
		ImGui::TextDisabled("|  Loaded memory: %s", FormatBytes(resources->GetTotalMemoryUsage()).c_str());
		ImGui::Text("Loaded -> Textures: %zu   Meshes: %zu   Models: %zu",
			resources->GetLoadedCount(ResourceType::Texture), resources->GetLoadedCount(ResourceType::Mesh), resources->GetLoadedCount(ResourceType::Model));
		ImGui::TextDisabled("Every component using a resource adds 1 reference; at 0 references the resource is unloaded.");

		static const char* filters[] = { "All", "Textures", "Meshes", "Models" };
		ImGui::SetNextItemWidth(140.0f);
		ImGui::Combo("Type", &resourcesFilter, filters, IM_ARRAYSIZE(filters));
		ImGui::SameLine();
		ImGui::Checkbox("Only loaded", &resourcesOnlyLoaded);

		std::vector<const Resource*> list;
		list.reserve(all.size());
		for (const auto& entry : all)
		{
			const Resource* resource = entry.second.get();
			if (resourcesFilter == 1 && resource->GetType() != ResourceType::Texture) continue;
			if (resourcesFilter == 2 && resource->GetType() != ResourceType::Mesh) continue;
			if (resourcesFilter == 3 && resource->GetType() != ResourceType::Model) continue;
			if (resourcesOnlyLoaded && !resource->IsLoaded()) continue;
			list.push_back(resource);
		}
		std::sort(list.begin(), list.end(), [](const Resource* a, const Resource* b)
			{
				if (a->GetType() != b->GetType()) return a->GetType() < b->GetType();
				if (a->GetName() != b->GetName()) return a->GetName() < b->GetName();
				return a->GetUID() < b->GetUID();
			});

		const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
			ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("ResourcesTable", 7, flags, ImVec2(0.0f, 0.0f)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.7f);
			ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthStretch, 0.5f);
			ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthStretch, 0.6f);
			ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthStretch, 0.8f);
			ImGui::TableSetupColumn("UID", ImGuiTableColumnFlags_WidthStretch, 1.3f);
			ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(list.size()));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const Resource* resource = list[row];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(resource->GetName().c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(resource->GetTypeName());
					ImGui::TableSetColumnIndex(2);
					if (resource->GetReferenceCount() > 0) ImGui::TextColored(EditorPalette::STATE_PLAY, "%u", resource->GetReferenceCount());
					else ImGui::TextDisabled("0");
					ImGui::TableSetColumnIndex(3);
					if (resource->IsLoaded()) ImGui::TextColored(EditorPalette::STATE_PLAY, "Yes");
					else ImGui::TextDisabled("No");
					ImGui::TableSetColumnIndex(4);
					if (resource->IsLoaded()) ImGui::TextUnformatted(FormatBytes(resource->GetMemoryUsage()).c_str());
					else ImGui::TextDisabled("-");
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%llu", (unsigned long long)resource->GetUID());
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(resource->GetAssetPath().empty() ? "(deleted)" : resource->GetAssetPath().c_str());
				}
			}
			ImGui::EndTable();
		}
	}
	ImGui::End();
}
