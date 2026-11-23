// =====================================================================================
// Ventana Hierarchy (parte de ModuleUI), como la de Unity:
//   - Barra con el menu "+" (crear) y un buscador (al buscar, la lista pasa a ser plana)
//   - Fila de la escena con su nombre y los GameObjects debajo
//   - Seleccion multiple: Ctrl + clic (alternar), Shift + clic (rango), flechas del teclado
//   - Doble clic = encuadrar en la vista Scene, F2 = renombrar
//   - Arrastrar: en el centro de una fila = hacerlo hijo; en el borde de arriba / abajo =
//     colocarlo antes / despues (reordenar hermanos)
// =====================================================================================
#include "ModuleUI.h"
#include "EditorDragDrop.h"
#include "EditorIcons.h"
#include "EditorWidgets.h"
#include "../Engine.h"
#include "../Scene/Scene.h"
#include "../Scene/GameObjects/GameObject.h"
#include "../Scene/GameObjects/Components/MeshComponent.h"
#include "../Scene/GameObjects/Components/CameraComponent.h"
#include "../ResourceManagement/Importers/ImporterManager.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <functional>

#ifdef _DEBUG
	#include <tracy/Tracy.hpp>
#endif

using namespace EditorWidgets;

namespace
{
	// Clic sobre un objeto que ya forma parte de una seleccion multiple: se espera a soltar el
	// boton (si no se ha arrastrado, entonces se selecciona solo ese objeto). Asi se puede
	// arrastrar el grupo entero, como en Unity.
	uint64_t pendingClickUID = 0;

	const ImVec4 SELECTION_FOCUSED = ImVec4(0.173f, 0.365f, 0.529f, 1.0f);   // #2C5D87
	const ImVec4 SELECTION_UNFOCUSED = ImVec4(0.302f, 0.302f, 0.302f, 1.0f); // #4D4D4D

	void GetObjectIcon(const GameObject* node, const char*& icon, ImVec4& color)
	{
		icon = ICON_FA_CUBE;
		color = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
		if (node->GetComponent<CameraComponent>() != nullptr)
		{
			icon = ICON_FA_VIDEO;
			color = EditorPalette::ICON_CAMERA;
		}
		else if (node->GetComponent<MeshComponent>() == nullptr && !node->GetChildren().empty())
		{
			icon = ICON_FA_CUBES;
			color = ImVec4(0.55f, 0.75f, 0.95f, 1.0f); // grupo / modelo (como el azul de los prefabs de Unity)
		}
		else if (node->GetComponent<MeshComponent>() == nullptr)
		{
			icon = ICON_FA_CUBE;
			color = ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
		}
	}

	std::vector<uint64_t> ReadUIDPayload(const ImGuiPayload* payload)
	{
		std::vector<uint64_t> uids;
		if (payload == nullptr || payload->DataSize <= 0 || payload->DataSize % sizeof(uint64_t) != 0) return uids;
		uids.resize(payload->DataSize / sizeof(uint64_t));
		std::memcpy(uids.data(), payload->Data, payload->DataSize);
		return uids;
	}
}

void ModuleUI::BeginRename(GameObject* object)
{
	if (object == nullptr) return;
	renamingUID = static_cast<uint64_t>(object->GetUUID());
	strncpy_s(renameBuffer, sizeof(renameBuffer), object->GetName().c_str(), _TRUNCATE);
	renameFocusPending = true;
	revealSelectionInHierarchy = true;
	showHierarchy = true;
}

void ModuleUI::HierarchyRowClicked(GameObject* node)
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr || node == nullptr) return;
	const ImGuiIO& io = ImGui::GetIO();
	const uint64_t uid = static_cast<uint64_t>(node->GetUUID());

	if (io.KeyShift)
	{
		// Rango entre el ultimo objeto pulsado (ancla) y este, en el orden visible de la lista
		const std::vector<GameObject*>& rows = previousHierarchyRows;
		int anchorIndex = -1, nodeIndex = -1;
		for (int i = 0; i < static_cast<int>(rows.size()); ++i)
		{
			if (static_cast<uint64_t>(rows[i]->GetUUID()) == rangeAnchorUID) anchorIndex = i;
			if (rows[i] == node) nodeIndex = i;
		}
		if (anchorIndex < 0 || nodeIndex < 0)
		{
			scene->Select(node);
			rangeAnchorUID = uid;
		}
		else
		{
			std::vector<GameObject*> range;
			if (io.KeyCtrl) range = scene->GetSelection();
			const int from = std::min(anchorIndex, nodeIndex);
			const int to = std::max(anchorIndex, nodeIndex);
			for (int i = from; i <= to; ++i) range.push_back(rows[i]);
			scene->SetSelection(range, node);
		}
	}
	else if (io.KeyCtrl)
	{
		scene->ToggleSelection(node);
		rangeAnchorUID = uid;
	}
	else
	{
		scene->Select(node);
		rangeAnchorUID = uid;
	}
	inspectorShowsAsset = false;
}

// =====================================================================================
// VENTANA HIERARCHY
// =====================================================================================
void ModuleUI::DrawHierarchyPanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!showHierarchy) return;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin(EditorWindows::HIERARCHY, &showHierarchy);
	ImGui::PopStyleVar();
	if (!open)
	{
		ImGui::End();
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		sceneOrHierarchyFocused = true;
		hierarchyFocused = true;
	}

	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr)
	{
		ImGui::End();
		return;
	}

	// Al cambiar la seleccion (desde la vista Scene, por ejemplo) se despliegan sus padres
	if (scene->GetSelectionVersion() != lastSelectionVersion)
	{
		lastSelectionVersion = scene->GetSelectionVersion();
		revealSelectionInHierarchy = true;
	}

	// ----------------------------- Barra superior -----------------------------
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorPalette::PANEL_TOOLBAR);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
	ImGui::BeginChild("HierarchyToolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + 4.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (FlatButton(ICON_FA_PLUS " " ICON_FA_CARET_DOWN "##HierarchyCreate")) ImGui::OpenPopup("HierarchyCreatePopup");
	IconTooltip("Create a new GameObject");
	if (ImGui::BeginPopup("HierarchyCreatePopup"))
	{
		DrawCreateMenuItems(nullptr);
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##HierarchySearch", ICON_FA_MAGNIFYING_GLASS " All", hierarchySearch, sizeof(hierarchySearch));
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();

	// ----------------------------- Arbol -----------------------------
	previousHierarchyRows.swap(hierarchyRows);
	hierarchyRows.clear();
	const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 2.0f));
	ImGui::BeginChild("HierarchyTree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
	ImGui::PushStyleColor(ImGuiCol_Header, windowFocused ? SELECTION_FOCUSED : SELECTION_UNFOCUSED);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, windowFocused ? SELECTION_FOCUSED : SELECTION_UNFOCUSED);

	// Fila de la escena (como la de Unity: icono + nombre de la escena + menu)
	const std::string& scenePath = scene->GetCurrentScenePath();
	const std::string sceneName = scenePath.empty() ? std::string("Untitled") : FileSystem::GetFileName(scenePath);
	ImGui::PushStyleColor(ImGuiCol_Header, EditorPalette::RAISED);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorPalette::RAISED_HOVERED);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, EditorPalette::RAISED_ACTIVE);
	const ImVec2 sceneRowPos = ImGui::GetCursorScreenPos();
	const bool sceneOpen = ImGui::TreeNodeEx("##SceneRoot", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
		ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_OpenOnArrow, "%s", "");
	ImGui::PopStyleColor(3);
	EditorIcons::DrawIconLabel(ImVec2(sceneRowPos.x + ImGui::GetTreeNodeToLabelSpacing(), sceneRowPos.y + 2.0f), ICON_FA_CLAPPERBOARD,
		EditorPalette::ICON_SCENE, (sceneName + (undo.CanUndo() ? "*" : "")).c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
	if (ImGui::BeginDragDropTarget())
	{
		AcceptHierarchyDrop(nullptr, 0);
		ImGui::EndDragDropTarget();
	}
	if (ImGui::BeginPopupContextItem("SceneRootContext"))
	{
		if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !scene->IsInPlayMode()))
		{
			if (scenePath.empty()) openSaveScenePopup = true;
			else scene->SaveScene(scenePath);
		}
		if (ImGui::MenuItem("Save Scene As...", nullptr, false, !scene->IsInPlayMode())) openSaveScenePopup = true;
		ImGui::Separator();
		DrawCreateMenuItems(nullptr);
		ImGui::EndPopup();
	}

	if (sceneOpen)
	{
		ImGui::Indent(4.0f);
		const std::string searchLower = ToLower(hierarchySearch);
		if (!searchLower.empty())
		{
			// Busqueda: lista plana con todos los objetos cuyo nombre coincide
			std::function<void(GameObject*)> collect = [&](GameObject* node)
			{
				for (GameObject* child : node->GetChildren())
				{
					if (ContainsCaseInsensitive(child->GetName(), searchLower)) DrawHierarchyNode(child);
					collect(child);
				}
			};
			collect(scene->GetRoot());
		}
		else
		{
			// Copia: dibujar un nodo puede reordenar la lista de hijos (drag & drop)
			const std::vector<GameObject*> rootChildren = scene->GetRoot()->GetChildren();
			for (GameObject* child : rootChildren)
			{
				if (child && child->GetParent() == scene->GetRoot()) DrawHierarchyNode(child);
			}
		}
		ImGui::Unindent(4.0f);
		ImGui::TreePop();
	}
	revealSelectionInHierarchy = false;

	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(2);

	// Zona vacia inferior: clic = deseleccionar, soltar = mover a la raiz, clic derecho = crear
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	ImGui::InvisibleButton("##HierarchyEmpty", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 40.0f)));
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
	{
		scene->ClearSelection();
		renamingUID = 0;
	}
	if (ImGui::BeginDragDropTarget())
	{
		AcceptHierarchyDrop(nullptr, 0);
		ImGui::EndDragDropTarget();
	}
	if (ImGui::BeginPopupContextItem("HierarchyEmptyContext"))
	{
		DrawCreateMenuItems(nullptr);
		ImGui::EndPopup();
	}

	ImGui::EndChild();
	ImGui::PopStyleVar();

	// Clic sobre un objeto ya seleccionado (seleccion multiple): al soltar sin arrastrar, solo ese
	if (pendingClickUID != 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		if (ImGui::GetDragDropPayload() == nullptr)
		{
			if (GameObject* object = scene->FindByUID(pendingClickUID)) HierarchyRowClicked(object);
		}
		pendingClickUID = 0;
	}

	HandleHierarchyKeyboard();
	ImGui::End();
}

void ModuleUI::DrawHierarchyNode(GameObject* node)
{
	if (!node) return;

	Scene* scene = Engine::GetInstance().scene.get();
	const bool searching = hierarchySearch[0] != '\0';
	const bool hasChildren = !node->GetChildren().empty() && !searching;
	const uint64_t uid = static_cast<uint64_t>(node->GetUUID());
	const bool isSelected = scene->IsSelected(node);
	const bool renaming = (renamingUID == uid);

	hierarchyRows.push_back(node);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
	if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
	if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	// Abrir / cerrar desde el teclado, o para mostrar un objeto recien seleccionado
	const auto pending = pendingOpenState.find(uid);
	if (pending != pendingOpenState.end())
	{
		ImGui::SetNextItemOpen(pending->second, ImGuiCond_Always);
		pendingOpenState.erase(pending);
	}
	else if (revealSelectionInHierarchy && hasChildren)
	{
		for (const GameObject* selected : scene->GetSelection())
		{
			if (selected->IsDescendantOf(node))
			{
				ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				break;
			}
		}
	}

	const ImVec2 rowPos = ImGui::GetCursorScreenPos();
	const bool nodeOpen = ImGui::TreeNodeEx((void*)node, flags, "%s", "");
	const ImVec2 rowMin = ImGui::GetItemRectMin();
	const ImVec2 rowMax = ImGui::GetItemRectMax();
	if (nodeOpen && hasChildren) openHierarchyNodes.insert(uid);
	else openHierarchyNodes.erase(uid);

	// Desplazar la lista para que se vea el objeto recien seleccionado
	if (revealSelectionInHierarchy && isSelected && node == scene->GetSelected() && !ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);

	// ----- Icono + nombre (objetos inactivos en gris) -----
	const char* icon = ICON_FA_CUBE;
	ImVec4 iconColor;
	GetObjectIcon(node, icon, iconColor);
	ImVec4 nameColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	if (!node->IsActiveInHierarchy())
	{
		nameColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
		iconColor = ImVec4(iconColor.x * 0.6f, iconColor.y * 0.6f, iconColor.z * 0.6f, 1.0f);
	}
	const ImVec2 labelPos(rowPos.x + ImGui::GetTreeNodeToLabelSpacing(), rowPos.y + ImGui::GetStyle().FramePadding.y);
	if (!renaming)
	{
		EditorIcons::DrawIconLabel(labelPos, icon, iconColor, node->GetName().c_str(), nameColor);
		// Camaras: "Main" o el display al que apuntan (Display 2...)
		if (const CameraComponent* camera = node->GetComponent<CameraComponent>())
		{
			char tag[32];
			if (scene->GetGameCameraObject() == node) snprintf(tag, sizeof(tag), "Main");
			else snprintf(tag, sizeof(tag), "Display %d", camera->targetDisplay + 1);
			ImGui::GetWindowDrawList()->AddText(ImVec2(rowMax.x - ImGui::CalcTextSize(tag).x - 8.0f, labelPos.y),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), tag);
		}
	}

	// ----- Clic, doble clic -----
	const bool toggledOpen = ImGui::IsItemToggledOpen();
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !toggledOpen)
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (isSelected && !io.KeyCtrl && !io.KeyShift && scene->GetSelection().size() > 1)
		{
			pendingClickUID = uid; // se decide al soltar (puede ser el inicio de un arrastre del grupo)
		}
		else
		{
			HierarchyRowClicked(node);
		}
		if (renamingUID != 0 && renamingUID != uid) renamingUID = 0;
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !toggledOpen)
	{
		scene->Select(node);
		FrameSelection();
	}

	// ----- Origen de arrastre: el grupo seleccionado (si se arrastra uno de ellos) o este objeto -----
	if (!renaming && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		std::vector<uint64_t> uids;
		if (scene->IsSelected(node))
		{
			for (const GameObject* object : scene->GetTopLevelSelection()) uids.push_back(static_cast<uint64_t>(object->GetUUID()));
		}
		if (uids.empty()) uids.push_back(uid);
		ImGui::SetDragDropPayload(EditorDragDrop::GAMEOBJECT, uids.data(), uids.size() * sizeof(uint64_t));
		if (uids.size() == 1) ImGui::Text("%s %s", icon, node->GetName().c_str());
		else ImGui::Text(ICON_FA_CUBES " %zu objects", uids.size());
		pendingClickUID = 0;
		ImGui::EndDragDropSource();
	}

	// ----- Destino: arriba / abajo = antes / despues (hermanos) | centro = hijo -----
	if (ImGui::BeginDragDropTarget())
	{
		const float mouseY = ImGui::GetMousePos().y;
		const float h = rowMax.y - rowMin.y;
		int zone = 0;
		if (!searching)
		{
			if (mouseY < rowMin.y + h * 0.25f) zone = -1;
			else if (mouseY > rowMax.y - h * 0.25f && !(nodeOpen && hasChildren)) zone = 1;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 lineColor = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
		const ImGuiPayload* peek = ImGui::GetDragDropPayload();
		if (peek && peek->IsDataType(EditorDragDrop::GAMEOBJECT))
		{
			if (zone == -1) drawList->AddLine(ImVec2(labelPos.x, rowMin.y), ImVec2(rowMax.x, rowMin.y), lineColor, 2.0f);
			else if (zone == 1) drawList->AddLine(ImVec2(labelPos.x, rowMax.y), ImVec2(rowMax.x, rowMax.y), lineColor, 2.0f);
			else drawList->AddRect(rowMin, rowMax, lineColor, 0.0f, 0, 2.0f);
		}
		else
		{
			drawList->AddRect(rowMin, rowMax, lineColor, 0.0f, 0, 2.0f);
		}
		AcceptHierarchyDrop(node, zone);
		ImGui::EndDragDropTarget();
	}

	DrawHierarchyContextMenu(node);

	// ----- Renombrar en la propia fila (F2) -----
	if (renaming)
	{
		const ImVec2 cursorAfter = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(labelPos.x - 2.0f, rowMin.y));
		ImGui::SetNextItemWidth(std::max(rowMax.x - labelPos.x - 4.0f, 60.0f));
		if (renameFocusPending)
		{
			ImGui::SetKeyboardFocusHere();
			renameFocusPending = false;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
		const bool enter = ImGui::InputText("##Rename", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		ImGui::PopStyleVar();
		const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
		const bool deactivated = ImGui::IsItemDeactivated();
		if (enter || (deactivated && !cancel))
		{
			if (renameBuffer[0] != '\0' && node->GetName() != renameBuffer)
			{
				undo.RecordSnapshot("Rename");
				node->SetName(renameBuffer);
			}
			renamingUID = 0;
		}
		else if (cancel || (deactivated && cancel))
		{
			renamingUID = 0;
		}
		ImGui::SetCursorScreenPos(cursorAfter);
		ImGui::Dummy(ImVec2(0.0f, 0.0f));
	}

	if (nodeOpen && hasChildren)
	{
		const std::vector<GameObject*> children = node->GetChildren();
		for (GameObject* child : children)
		{
			if (child && child->GetParent() == node) DrawHierarchyNode(child);
		}
		ImGui::TreePop();
	}
}

void ModuleUI::DrawHierarchyContextMenu(GameObject* node)
{
	Scene* scene = Engine::GetInstance().scene.get();

	// Clic derecho sobre un objeto no seleccionado: pasa a ser la seleccion (como en Unity)
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !scene->IsSelected(node))
	{
		scene->Select(node);
		inspectorShowsAsset = false;
	}

	if (!ImGui::BeginPopupContextItem()) return;

	if (ImGui::MenuItem("Rename", "F2")) BeginRename(node);
	if (ImGui::MenuItem("Duplicate", "Ctrl+D")) DuplicateSelection();
	if (ImGui::MenuItem("Delete", "Del")) DeleteSelection();
	ImGui::Separator();
	if (ImGui::MenuItem("Select Children", nullptr, false, !node->GetChildren().empty()))
	{
		std::vector<GameObject*> all;
		std::function<void(GameObject*)> collect = [&](GameObject* n) { all.push_back(n); for (GameObject* c : n->GetChildren()) collect(c); };
		collect(node);
		scene->SetSelection(all, node);
	}
	if (ImGui::MenuItem("Frame Selected", "F")) FrameSelection();
	if (ImGui::MenuItem("Unparent", nullptr, false, node->GetParent() != scene->GetRoot()))
	{
		undo.RecordSnapshot("Unparent");
		for (GameObject* object : scene->GetTopLevelSelection()) object->SetParent(scene->GetRoot());
	}
	if (node->GetComponent<CameraComponent>() && scene->GetGameCameraObject() != node)
	{
		if (ImGui::MenuItem("Set as Main Camera"))
		{
			undo.RecordSnapshot("Set Main Camera");
			scene->SetGameCamera(node);
		}
	}
	ImGui::Separator();
	ImGui::TextDisabled("Create as child");
	DrawCreateMenuItems(node);
	ImGui::EndPopup();
}

bool ModuleUI::AcceptHierarchyDrop(GameObject* target, int dropZone)
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return false;

	const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

	// ----- GameObjects (uno o varios) -----
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::GAMEOBJECT, flags))
	{
		const std::vector<uint64_t> uids = ReadUIDPayload(payload);
		std::vector<GameObject*> objects;
		for (uint64_t uid : uids)
		{
			GameObject* object = scene->FindByUID(uid);
			if (object == nullptr || object == target) continue;
			// No se puede meter un objeto dentro de uno de sus descendientes
			if (target != nullptr && target->IsDescendantOf(object)) continue;
			objects.push_back(object);
		}
		if (objects.empty()) return false;

		undo.RecordSnapshot(dropZone == 0 ? "Reparent" : "Reorder");
		if (target == nullptr)
		{
			for (GameObject* object : objects) object->SetParent(scene->GetRoot());
		}
		else if (dropZone == 0)
		{
			for (GameObject* object : objects) object->SetParent(target);
			pendingOpenState[static_cast<uint64_t>(target->GetUUID())] = true;
		}
		else
		{
			// Antes / despues de 'target', entre sus hermanos (conservando el orden del grupo)
			GameObject* parent = target->GetParent() ? target->GetParent() : scene->GetRoot();
			GameObject* reference = target;
			for (GameObject* object : objects)
			{
				object->SetParent(parent);
				const int objectIndex = object->GetSiblingIndex();
				const int referenceIndex = reference->GetSiblingIndex();
				int insert = (dropZone < 0) ? referenceIndex : referenceIndex + 1;
				if (objectIndex < referenceIndex) insert -= 1;
				object->SetSiblingIndex(insert);
				if (dropZone > 0) reference = object;
			}
		}
		scene->SetSelection(objects, objects.back());
		return true;
	}

	// ----- Modelo: se instancia como hijo (o en la raiz) -----
	if (const ImGuiPayload* modelPayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_MODEL))
	{
		const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(modelPayload->Data, modelPayload->DataSize);
		undo.RecordSnapshot("Instantiate Model");
		GameObject* parent = (target != nullptr && dropZone == 0) ? target : (target ? target->GetParent() : nullptr);
		SelectGameObject(scene->InstantiateModel(asset.uid, parent));
		return true;
	}

	// ----- Textura: se aplica al objeto y a sus hijos -----
	if (target != nullptr)
	{
		if (const ImGuiPayload* texturePayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_TEXTURE))
		{
			const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(texturePayload->Data, texturePayload->DataSize);
			undo.RecordSnapshot("Assign Texture");
			ImporterManager::AssignTextureToHierarchy(asset.uid, target);
			return true;
		}
	}
	return false;
}

void ModuleUI::HandleHierarchyKeyboard()
{
	if (!hierarchyFocused || ImGui::GetIO().WantTextInput || renamingUID != 0) return;
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;

	GameObject* active = scene->GetSelected();
	const std::vector<GameObject*>& rows = hierarchyRows;
	if (rows.empty()) return;

	int index = -1;
	for (int i = 0; i < static_cast<int>(rows.size()); ++i)
	{
		if (rows[i] == active) { index = i; break; }
	}

	const auto selectRow = [&](int row)
	{
		row = std::clamp(row, 0, static_cast<int>(rows.size()) - 1);
		if (ImGui::GetIO().KeyShift && active)
		{
			scene->AddToSelection(rows[row]);
		}
		else
		{
			scene->Select(rows[row]);
			rangeAnchorUID = static_cast<uint64_t>(rows[row]->GetUUID());
		}
		revealSelectionInHierarchy = true;
		inspectorShowsAsset = false;
	};

	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) selectRow(index < 0 ? 0 : index + 1);
	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) selectRow(index < 0 ? 0 : index - 1);

	if (active != nullptr && !active->GetChildren().empty() && hierarchySearch[0] == '\0')
	{
		const uint64_t uid = static_cast<uint64_t>(active->GetUUID());
		const bool isOpen = openHierarchyNodes.count(uid) > 0;
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
		{
			if (!isOpen) pendingOpenState[uid] = true;
			else scene->Select(active->GetChildren().front());
		}
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
		{
			if (isOpen) pendingOpenState[uid] = false;
			else if (active->GetParent() && active->GetParent() != scene->GetRoot()) scene->Select(active->GetParent());
		}
	}
	else if (active != nullptr && ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && active->GetParent() && active->GetParent() != scene->GetRoot())
	{
		scene->Select(active->GetParent());
	}
}
