// =====================================================================================
// Ventanas del editor relacionadas con los recursos (parte de ModuleUI):
//   - "Assets":    explorador de la carpeta Assets (carpetas, ficheros, drag & drop, borrar...)
//   - "Resources": todos los recursos cargados con su conteo de referencias
//   - Inspector de un asset: informacion + opciones de importacion (se guardan en el .meta)
// =====================================================================================
#include "ModuleUI.h"
#include "EditorDragDrop.h"
#include "EditorIcons.h"
#include "../Engine.h"
#include "../Scene/Scene.h"
#include "../Scene/GameObjects/GameObject.h"
#include "../ResourceManagement/ResourceManager.h"
#include "../ResourceManagement/ResourceMesh.h"
#include "../ResourceManagement/ResourceTexture.h"
#include "../ResourceManagement/Importers/ImporterManager.h"

#include <imgui.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

namespace
{
	enum class AssetKind { Folder, Texture, Model, Scene, Other };

	constexpr uint64_t ASSETS_CACHE_LIFETIME_MS = 1000; // el listado de carpetas se relee cada segundo

	AssetKind GetAssetKind(const FileSystem::DirectoryEntry& entry)
	{
		if (entry.isDirectory) return AssetKind::Folder;
		if (ResourceManager::IsTextureFile(entry.path)) return AssetKind::Texture;
		if (ResourceManager::IsModelFile(entry.path)) return AssetKind::Model;
		if (ResourceManager::IsSceneFile(entry.path)) return AssetKind::Scene;
		return AssetKind::Other;
	}

	// Icono (glifo de Font Awesome) de cada tipo de elemento del explorador:
	//   carpeta | textura (.png, .tga, .jpg...) | modelo 3D (.fbx...) | escena (.scene) | otro fichero
	const char* KindIcon(AssetKind kind)
	{
		switch (kind)
		{
		case AssetKind::Folder:  return ICON_FA_FOLDER;
		case AssetKind::Texture: return ICON_FA_IMAGE;
		case AssetKind::Model:   return ICON_FA_CUBE;
		case AssetKind::Scene:   return ICON_FA_CLAPPERBOARD;
		default:                 return ICON_FA_FILE;
		}
	}

	// Color del icono: el nombre del fichero se pinta siempre con el color de texto normal
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
}

// =====================================================================================
// OPERACIONES DIFERIDAS
// =====================================================================================
void ModuleUI::QueueAssetOperation(AssetOperation operation, const std::string& path)
{
	PendingAssetOperation pending;
	pending.operation = operation;
	pending.path = path;
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

	// Se mueve a una variable local por si alguna operacion encola otra
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
// VENTANA ASSETS
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

void ModuleUI::SelectAsset(const std::string& path)
{
	selectedAssetPath = path;
	inspectorShowsAsset = true;
}

void ModuleUI::OpenAsset(const std::string& path, bool isDirectory)
{
	ResourceManager* resources = Engine::GetInstance().resources.get();
	Scene* scene = Engine::GetInstance().scene.get();
	if (resources == nullptr || scene == nullptr) return;

	if (isDirectory)
	{
		assetsCurrentFolder = path;
		return;
	}

	if (ResourceManager::IsModelFile(path))
	{
		// Doble clic en un modelo: se instancia en la escena
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
			ImporterManager::AssignTextureToHierarchy(resources->GetAssetUID(path), scene->GetSelected());
		}
		else
		{
			LOG_WARNING("[Assets] Selecciona un GameObject para aplicarle la textura");
		}
	}
	else if (ResourceManager::IsSceneFile(path))
	{
		scene->RequestLoadScene(path);
	}
}

void ModuleUI::DrawAssetsPanel()
{
	assetsFocused = false;
	if (!showAssets) return;

	ResourceManager* resources = Engine::GetInstance().resources.get();

	// El listado de carpetas se cachea y se relee como mucho una vez por segundo: asi la ventana
	// "reacciona" sola si se anaden o borran ficheros desde fuera del motor
	const uint64_t now = SDL_GetTicks();
	if (assetsCacheInvalid || now - assetsCacheTime > ASSETS_CACHE_LIFETIME_MS)
	{
		assetsDirectoryCache.clear();
		assetsCacheTime = now;
		assetsCacheInvalid = false;
	}

	if (ImGui::Begin("Assets", &showAssets))
	{
		assetsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		// Si la carpeta abierta ya no existe (borrada desde fuera) volvemos a Assets
		if (!FileSystem::IsDirectory(assetsCurrentFolder)) assetsCurrentFolder = FileSystem::ASSETS_DIR;
		if (resources) resources->SetImportFolder(assetsCurrentFolder);

		// --------------------------- Barra superior -----------------------------
		ImGui::BeginDisabled(assetsCurrentFolder == FileSystem::ASSETS_DIR);
		if (ImGui::Button(ICON_FA_ARROW_UP "##AssetsParentFolder"))
		{
			assetsCurrentFolder = FileSystem::GetParentPath(assetsCurrentFolder);
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Carpeta superior");
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(EditorPalette::ICON_FOLDER, ICON_FA_FOLDER_OPEN);
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::TextUnformatted(assetsCurrentFolder.c_str());
		ImGui::SameLine();
		// "###" separa el texto visible (con icono) del ID del boton
		if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Refresh###AssetsRefresh"))
		{
			QueueAssetOperation(AssetOperation::Refresh, std::string());
		}
		ImGui::SetItemTooltip("Vuelve a revisar la carpeta Assets: importa lo nuevo y limpia lo borrado");
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_FOLDER_PLUS "  Nueva carpeta###AssetsNewFolder"))
		{
			openNewFolderPopup = true;
		}
		ImGui::Separator();

		// --------------------- Arbol de carpetas (izquierda) --------------------
		ImGui::BeginChild("AssetsTree", ImVec2(200.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
		DrawAssetsFolderTree(FileSystem::ASSETS_DIR);
		ImGui::EndChild();

		ImGui::SameLine();

		// --------------------- Contenido de la carpeta (derecha) -----------------
		ImGui::BeginChild("AssetsContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
		DrawAssetsFolderContent();
		ImGui::EndChild();

		// Supr: borrar el asset seleccionado
		if (assetsFocused && !selectedAssetPath.empty() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			pendingDeletePath = selectedAssetPath;
			pendingDeleteIsFolder = FileSystem::IsDirectory(selectedAssetPath);
			openDeletePopup = true;
		}
	}
	ImGui::End();
}

void ModuleUI::DrawAssetsFolderTree(const std::string& folder)
{
	const std::vector<FileSystem::DirectoryEntry> entries = GetCachedDirectory(folder);
	const bool hasSubfolders = std::any_of(entries.begin(), entries.end(),
		[](const FileSystem::DirectoryEntry& entry) { return entry.isDirectory; });

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (folder == FileSystem::ASSETS_DIR) flags |= ImGuiTreeNodeFlags_DefaultOpen;
	if (!hasSubfolders) flags |= ImGuiTreeNodeFlags_Leaf;
	if (folder == assetsCurrentFolder) flags |= ImGuiTreeNodeFlags_Selected;

	// La ruta completa sirve de ID unico del nodo. El nodo se crea sin etiqueta y encima se pinta
	// "icono + nombre" en dos colores; la carpeta cuyo contenido se esta viendo usa el icono de carpeta abierta.
	const ImVec2 nodePos = ImGui::GetCursorScreenPos();
	const bool open = ImGui::TreeNodeEx(folder.c_str(), flags, "%s", "");
	EditorIcons::DrawIconLabel(ImVec2(nodePos.x + ImGui::GetTreeNodeToLabelSpacing(), nodePos.y),
		(folder == assetsCurrentFolder) ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER, EditorPalette::ICON_FOLDER,
		FileSystem::GetFileName(folder).c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		assetsCurrentFolder = folder;
	}

	if (open)
	{
		for (const FileSystem::DirectoryEntry& entry : entries)
		{
			if (entry.isDirectory) DrawAssetsFolderTree(entry.path);
		}
		ImGui::TreePop();
	}
}

void ModuleUI::DrawAssetsFolderContent()
{
	const std::vector<FileSystem::DirectoryEntry> entries = GetCachedDirectory(assetsCurrentFolder);
	if (entries.empty())
	{
		ImGui::TextDisabled("(carpeta vacia)");
	}

	for (const FileSystem::DirectoryEntry& entry : entries)
	{
		DrawAssetEntry(entry);
	}

	// Menu contextual al hacer clic derecho en una zona vacia
	if (ImGui::BeginPopupContextWindow("AssetsContentContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::MenuItem("Nueva carpeta")) openNewFolderPopup = true;
		if (ImGui::MenuItem("Refresh")) QueueAssetOperation(AssetOperation::Refresh, std::string());
		ImGui::EndPopup();
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Arrastra ficheros (fbx, png, tga, dds...) desde el explorador del sistema para importarlos en esta carpeta.");
	ImGui::TextDisabled("Arrastra modelos a la escena/jerarquia, mallas al campo Malla y texturas al campo Textura del Inspector.");
}

void ModuleUI::DrawAssetEntry(const FileSystem::DirectoryEntry& entry)
{
	ResourceManager* resources = Engine::GetInstance().resources.get();
	const AssetKind kind = GetAssetKind(entry);
	const bool isSelected = (selectedAssetPath == entry.path);
	const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);

	ImGui::PushID(entry.path.c_str());

	// El widget de la fila (Selectable o TreeNode) se crea SIN etiqueta y despues se pinta encima
	// "icono + nombre": el icono con el color de su tipo y el nombre con el color de texto normal.
	// La posicion se lee antes de crear el widget (ver EditorIcons::DrawIconLabel).
	ImVec2 labelPos = ImGui::GetCursorScreenPos();

	// Los modelos se pueden desplegar para ver (y arrastrar) cada una de sus mallas
	bool modelOpen = false;
	if (kind == AssetKind::Model)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
		modelOpen = ImGui::TreeNodeEx("##model", flags, "%s", "");
	}
	else
	{
		ImGui::Selectable("##entry", isSelected, ImGuiSelectableFlags_AllowDoubleClick);
	}
	// Un nodo de arbol deja a la izquierda el hueco de su flecha. Se deja ese mismo hueco tambien en
	// las filas normales, asi todos los iconos de la lista quedan alineados en la misma columna.
	labelPos.x += ImGui::GetTreeNodeToLabelSpacing();
	EditorIcons::DrawIconLabel(labelPos, KindIcon(kind), KindColor(kind), entry.name.c_str(), textColor);

	// Clic: seleccionar (el Inspector muestra el asset). Doble clic: abrir / usar.
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		SelectAsset(entry.path);
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		OpenAsset(entry.path, entry.isDirectory);
	}

	// Origen de drag & drop (texturas, modelos y escenas)
	const char* payloadType = nullptr;
	if (kind == AssetKind::Texture) payloadType = EditorDragDrop::ASSET_TEXTURE;
	else if (kind == AssetKind::Model) payloadType = EditorDragDrop::ASSET_MODEL;
	else if (kind == AssetKind::Scene) payloadType = EditorDragDrop::ASSET_SCENE;

	const UID assetUID = (resources && kind != AssetKind::Folder) ? resources->GetAssetUID(entry.path) : INVALID_UID;
	if (payloadType != nullptr && ImGui::BeginDragDropSource())
	{
		const EditorDragDrop::AssetPayload payload = EditorDragDrop::MakeAssetPayload(assetUID, entry.path);
		ImGui::SetDragDropPayload(payloadType, &payload, sizeof(payload));
		// Vista previa que acompana al raton mientras se arrastra: icono del tipo + nombre
		ImGui::TextColored(KindColor(kind), "%s", KindIcon(kind));
		ImGui::SameLine();
		ImGui::TextUnformatted(entry.name.c_str());
		ImGui::EndDragDropSource();
	}

	// Tooltip con informacion del recurso (y miniatura si la textura esta en memoria)
	if (kind == AssetKind::Texture || kind == AssetKind::Model)
	{
		if (ImGui::BeginItemTooltip())
		{
			const Resource* resource = resources ? resources->GetResource(assetUID) : nullptr;
			if (resource)
			{
				ImGui::Text("UID: %llu", (unsigned long long)resource->GetUID());
				ImGui::Text("Referencias: %u  |  En memoria: %s", resource->GetReferenceCount(), resource->IsLoaded() ? "Si" : "No");
				if (kind == AssetKind::Texture)
				{
					const ResourceTexture* texture = GetTextureResource(resources, assetUID);
					if (texture && texture->IsLoaded()) DrawTexturePreview(texture, 96.0f);
				}
			}
			else
			{
				ImGui::TextDisabled("No importado");
			}
			ImGui::EndTooltip();
		}
	}

	DrawAssetContextMenu(entry);

	// Mallas del modelo desplegado
	if (modelOpen)
	{
		const std::vector<UID> uids = resources ? resources->GetAssetResources(entry.path) : std::vector<UID>();
		for (size_t i = 1; i < uids.size(); ++i) // el primero es el propio modelo
		{
			const Resource* mesh = resources->GetResource(uids[i]);
			if (mesh == nullptr) continue;

			ImGui::PushID(static_cast<int>(i));
			// Misma tecnica que en las filas de assets: Selectable sin etiqueta + icono y nombre pintados encima
			// (con el mismo hueco a la izquierda, para que la malla quede sangrada respecto a su modelo)
			ImVec2 meshLabelPos = ImGui::GetCursorScreenPos();
			meshLabelPos.x += ImGui::GetTreeNodeToLabelSpacing();
			ImGui::Selectable("##mesh", false);
			EditorIcons::DrawIconLabel(meshLabelPos, ICON_FA_DRAW_POLYGON, EditorPalette::ICON_MESH, mesh->GetName().c_str(), textColor);
			if (ImGui::BeginDragDropSource())
			{
				const EditorDragDrop::AssetPayload payload = EditorDragDrop::MakeAssetPayload(mesh->GetUID(), entry.path);
				ImGui::SetDragDropPayload(EditorDragDrop::ASSET_MESH, &payload, sizeof(payload));
				ImGui::TextColored(EditorPalette::ICON_MESH, ICON_FA_DRAW_POLYGON);
				ImGui::SameLine();
				ImGui::TextUnformatted(mesh->GetName().c_str());
				ImGui::EndDragDropSource();
			}
			ImGui::SetItemTooltip("Malla UID %llu | Referencias: %u | En memoria: %s\nArrastrala al campo Malla del Inspector o a la escena",
				(unsigned long long)mesh->GetUID(), mesh->GetReferenceCount(), mesh->IsLoaded() ? "Si" : "No");
			ImGui::PopID();
		}
		ImGui::TreePop();
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
		if (ImGui::MenuItem("Abrir")) assetsCurrentFolder = entry.path;
		break;
	case AssetKind::Model:
		if (ImGui::MenuItem("Instanciar en la escena")) OpenAsset(entry.path, false);
		if (ImGui::MenuItem("Reimportar")) QueueAssetOperation(AssetOperation::Reimport, entry.path);
		break;
	case AssetKind::Texture:
		if (ImGui::MenuItem("Aplicar al GameObject seleccionado", nullptr, false, scene && scene->GetSelected())) OpenAsset(entry.path, false);
		if (ImGui::MenuItem("Reimportar")) QueueAssetOperation(AssetOperation::Reimport, entry.path);
		break;
	case AssetKind::Scene:
		if (ImGui::MenuItem("Abrir escena")) OpenAsset(entry.path, false);
		break;
	default:
		break;
	}

	if (kind != AssetKind::Other) ImGui::Separator();
	if (ImGui::MenuItem(entry.isDirectory ? "Borrar carpeta..." : "Borrar..."))
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
		ImGui::OpenPopup("Confirmar borrado");
		openDeletePopup = false;
	}
	if (ImGui::BeginPopupModal("Confirmar borrado", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Borrar '%s'?", pendingDeletePath.c_str());
		if (pendingDeleteIsFolder)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Se borrara la carpeta con TODO su contenido.");
		}
		ImGui::TextDisabled("Tambien se borran los .meta y los ficheros de Library asociados.");
		ImGui::TextDisabled("Los GameObjects que lo usen se quedaran sin malla / con textura 'missing'.");
		ImGui::Spacing();

		if (ImGui::Button("Borrar", ImVec2(120.0f, 0.0f)))
		{
			QueueAssetOperation(pendingDeleteIsFolder ? AssetOperation::DeleteFolder : AssetOperation::Delete, pendingDeletePath);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancelar", ImVec2(120.0f, 0.0f)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (openNewFolderPopup)
	{
		ImGui::OpenPopup("Nueva carpeta");
		openNewFolderPopup = false;
	}
	if (ImGui::BeginPopupModal("Nueva carpeta", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Dentro de: %s", assetsCurrentFolder.c_str());
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		const bool enterPressed = ImGui::InputText("Nombre", newFolderName, sizeof(newFolderName), ImGuiInputTextFlags_EnterReturnsTrue);

		// Un nombre de carpeta no puede contener separadores ni ser "." o ".."
		const std::string folderName = newFolderName;
		const bool validName = !folderName.empty() && folderName != "." && folderName != ".." &&
			folderName.find_first_of("/\\:*?\"<>|") == std::string::npos;

		ImGui::BeginDisabled(!validName);
		if (ImGui::Button("Crear", ImVec2(120.0f, 0.0f)) || (enterPressed && validName))
		{
			const std::string path = FileSystem::MakeUniquePath(FileSystem::Join(assetsCurrentFolder, folderName));
			if (FileSystem::CreateDirectories(path))
			{
				LOG("[Assets] Carpeta creada: '%s'", path.c_str());
				assetsCurrentFolder = path;
			}
			InvalidateAssetsCache();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancelar", ImVec2(120.0f, 0.0f)))
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
		ImGui::TextDisabled("El asset seleccionado ya no existe.");
		return;
	}

	const bool isFolder = FileSystem::IsDirectory(path);
	ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", FileSystem::GetFileName(path).c_str());
	ImGui::TextDisabled("%s", path.c_str());
	ImGui::Separator();

	if (isFolder)
	{
		ImGui::Text("Carpeta");
		if (ImGui::Button("Abrir carpeta")) assetsCurrentFolder = path;
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
		ImGui::SeparatorText("Recurso (Library)");
		if (resource)
		{
			ImGui::BulletText("UID: %llu", (unsigned long long)uid);
			ImGui::BulletText("Fichero propio: %s", resource->GetLibraryPath().c_str());
			ImGui::BulletText("Referencias: %u", resource->GetReferenceCount());
			ImGui::BulletText("En memoria: %s", resource->IsLoaded() ? "Si" : "No (nadie la usa)");

			const ResourceTexture* texture = GetTextureResource(resources, uid);
			if (texture && texture->IsLoaded())
			{
				ImGui::BulletText("Tamano: %d x %d (%s)", texture->GetWidth(), texture->GetHeight(), texture->GetChannels() == 4 ? "RGBA" : "RGB");
				ImGui::BulletText("Memoria GPU aprox.: %s", FormatBytes(texture->GetMemoryUsage()).c_str());
				DrawTexturePreview(texture, 160.0f);
			}
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "No se pudo importar esta textura (ver Console).");
		}

		ImGui::SeparatorText("Opciones de importacion (.meta)");
		ImGui::Checkbox("Flip X (espejo horizontal)", &editingTextureSettings.flipX);
		ImGui::Checkbox("Flip Y (voltear vertical)", &editingTextureSettings.flipY);

		static const char* minFilters[] = { "Nearest", "Linear", "Trilinear (mipmaps)" };
		ImGui::Combo("Filtro al alejar", &editingTextureSettings.minFilter, minFilters, IM_ARRAYSIZE(minFilters));
		static const char* magFilters[] = { "Nearest (pixelado)", "Linear (suave)" };
		ImGui::Combo("Filtro al acercar", &editingTextureSettings.magFilter, magFilters, IM_ARRAYSIZE(magFilters));
		static const char* wrapModes[] = { "Repeat", "Clamp to edge", "Mirrored repeat" };
		ImGui::Combo("Wrapping", &editingTextureSettings.wrapMode, wrapModes, IM_ARRAYSIZE(wrapModes));

		static const int sizes[] = { 256, 512, 1024, 2048, 4096, 8192 };
		static const char* sizeNames[] = { "256", "512", "1024", "2048", "4096", "8192" };
		int sizeIndex = 3;
		for (int i = 0; i < IM_ARRAYSIZE(sizes); ++i)
		{
			if (sizes[i] == editingTextureSettings.maxSize) sizeIndex = i;
		}
		if (ImGui::Combo("Tamano maximo", &sizeIndex, sizeNames, IM_ARRAYSIZE(sizeNames)))
		{
			editingTextureSettings.maxSize = sizes[sizeIndex];
		}

		const bool changed = editingTextureSettings != savedTextureSettings;
		ImGui::BeginDisabled(!changed);
		if (ImGui::Button("Apply")) QueueAssetOperation(AssetOperation::ApplyTextureSettings, path);
		ImGui::SameLine();
		if (ImGui::Button("Revert")) editingTextureSettings = savedTextureSettings;
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Reimportar")) QueueAssetOperation(AssetOperation::Reimport, path);
		ImGui::TextDisabled("Al aplicar se guarda el .meta y se regenera el fichero de Library.");

		ImGui::Spacing();
		ImGui::BeginDisabled(scene == nullptr || scene->GetSelected() == nullptr);
		if (ImGui::Button("Aplicar al GameObject seleccionado")) OpenAsset(path, false);
		ImGui::EndDisabled();
	}
	// ------------------------------ MODELOS -------------------------------------------
	else if (ResourceManager::IsModelFile(path))
	{
		ImGui::SeparatorText("Recurso (Library)");
		if (resource)
		{
			ImGui::BulletText("UID: %llu", (unsigned long long)uid);
			ImGui::BulletText("Fichero propio: %s", resource->GetLibraryPath().c_str());

			const std::vector<UID> uids = resources->GetAssetResources(path);
			ImGui::BulletText("Mallas: %zu", uids.empty() ? size_t(0) : uids.size() - 1);
			if (uids.size() > 1 && ImGui::TreeNode("Mallas del modelo (referencias)"))
			{
				for (size_t i = 1; i < uids.size(); ++i)
				{
					const Resource* mesh = resources->GetResource(uids[i]);
					if (mesh == nullptr) continue;
					ImGui::BulletText("%s  |  refs: %u  |  %s", mesh->GetName().c_str(), mesh->GetReferenceCount(), mesh->IsLoaded() ? "cargada" : "no cargada");
				}
				ImGui::TreePop();
			}

			if (ImGui::Button("Instanciar en la escena")) OpenAsset(path, false);
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "No se pudo importar este modelo (ver Console).");
		}

		ImGui::SeparatorText("Opciones de importacion (.meta)");
		ImGui::DragFloat("Escala global", &editingModelSettings.globalScale, 0.01f, 0.0001f, 10000.0f, "%.4f");
		if (editingModelSettings.globalScale <= 0.0f) editingModelSettings.globalScale = 0.0001f;
		ImGui::Checkbox("Convertir unidades a metros (FBX)", &editingModelSettings.convertUnits);
		// Mismo orden que ModelImportSettings::UpAxis
		static const char* axes[] = { "+Y (sin cambios)", "+Z (3ds Max / Blender)", "+X", "-Z", "-X", "-Y" };
		ImGui::Combo("Eje vertical del fichero", &editingModelSettings.upAxis, axes, IM_ARRAYSIZE(axes));
		ImGui::Checkbox("Ignorar camaras", &editingModelSettings.ignoreCameras);
		ImGui::Checkbox("Ignorar luces", &editingModelSettings.ignoreLights);

		const bool changed = editingModelSettings != savedModelSettings;
		ImGui::BeginDisabled(!changed);
		if (ImGui::Button("Apply")) QueueAssetOperation(AssetOperation::ApplyModelSettings, path);
		ImGui::SameLine();
		if (ImGui::Button("Revert")) editingModelSettings = savedModelSettings;
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Reimportar")) QueueAssetOperation(AssetOperation::Reimport, path);
		ImGui::TextDisabled("Los cambios se aplican a las nuevas instancias del modelo.");
	}
	// ------------------------------ ESCENAS -------------------------------------------
	else if (ResourceManager::IsSceneFile(path))
	{
		ImGui::Text("Escena del motor (.scene, JSON)");
		if (scene && ImGui::Button("Abrir escena")) scene->RequestLoadScene(path);
	}
	else
	{
		ImGui::TextDisabled("Tipo de fichero no soportado por el motor.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	if (ImGui::Button("Borrar asset..."))
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

	if (ImGui::Begin("Resources", &showResources))
	{
		ResourceManager* resources = Engine::GetInstance().resources.get();
		if (resources == nullptr)
		{
			ImGui::End();
			return;
		}

		const auto& all = resources->GetAllResources();
		ImGui::Text("Recursos registrados: %zu", all.size());
		ImGui::SameLine();
		ImGui::TextDisabled("|  Memoria aprox. de los cargados: %s", FormatBytes(resources->GetTotalMemoryUsage()).c_str());
		ImGui::Text("En memoria ->  Texturas: %zu   Mallas: %zu   Modelos: %zu",
			resources->GetLoadedCount(ResourceType::Texture), resources->GetLoadedCount(ResourceType::Mesh), resources->GetLoadedCount(ResourceType::Model));
		ImGui::TextDisabled("Cada componente que usa un recurso le suma 1 referencia. Una textura usada por 20 GameObjects esta 1 sola vez en memoria.");
		ImGui::TextDisabled("Cuando las referencias llegan a 0 el recurso se descarga de memoria.");

		static const char* filters[] = { "Todos", "Texturas", "Mallas", "Modelos" };
		ImGui::SetNextItemWidth(140.0f);
		ImGui::Combo("Tipo", &resourcesFilter, filters, IM_ARRAYSIZE(filters));
		ImGui::SameLine();
		ImGui::Checkbox("Solo los que estan en memoria", &resourcesOnlyLoaded);

		// Lista filtrada y ordenada (por tipo y nombre)
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

		const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
			ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("ResourcesTable", 7, flags, ImVec2(0.0f, 0.0f)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Nombre", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Tipo", ImGuiTableColumnFlags_WidthStretch, 0.7f);
			ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthStretch, 0.5f);
			ImGui::TableSetupColumn("En memoria", ImGuiTableColumnFlags_WidthStretch, 0.7f);
			ImGui::TableSetupColumn("Memoria", ImGuiTableColumnFlags_WidthStretch, 0.8f);
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
					if (resource->GetReferenceCount() > 0) ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%u", resource->GetReferenceCount());
					else ImGui::TextDisabled("0");

					ImGui::TableSetColumnIndex(3);
					if (resource->IsLoaded()) ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Si");
					else ImGui::TextDisabled("No");

					ImGui::TableSetColumnIndex(4);
					if (resource->IsLoaded()) ImGui::TextUnformatted(FormatBytes(resource->GetMemoryUsage()).c_str());
					else ImGui::TextDisabled("-");

					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%llu", (unsigned long long)resource->GetUID());

					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(resource->GetAssetPath().empty() ? "(borrado)" : resource->GetAssetPath().c_str());
				}
			}
			ImGui::EndTable();
		}
	}
	ImGui::End();
}
