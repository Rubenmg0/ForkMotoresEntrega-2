// =====================================================================================
// Ventana Inspector (parte de ModuleUI), como la de Unity:
//   - Cabecera del objeto: icono, casilla de activo y nombre
//   - Un bloque por componente con su cabecera (plegar, activar, menu de tres puntos con
//     Reset / Remove Component / Copy / Paste)
//   - Transform con campos "X Y Z" (las letras se arrastran para cambiar el valor). Con varios
//     objetos seleccionados se edita el Transform de todos ("—" = valores distintos)
//   - "Add Component" con buscador
// Todas las ediciones se pueden deshacer (Ctrl+Z).
// =====================================================================================
#include "ModuleUI.h"
#include "EditorDragDrop.h"
#include "EditorIcons.h"
#include "EditorWidgets.h"
#include "../Engine.h"
#include "../Render/Render.h"
#include "../Scene/Scene.h"
#include "../Scene/GameObjects/GameObject.h"
#include "../Scene/GameObjects/Components/TransformComponent.h"
#include "../Scene/GameObjects/Components/MeshComponent.h"
#include "../Scene/GameObjects/Components/MaterialComponent.h"
#include "../Scene/GameObjects/Components/CameraComponent.h"
#include "../ResourceManagement/ResourceManager.h"
#include "../ResourceManagement/ResourceMesh.h"
#include "../ResourceManagement/ResourceTexture.h"
#include "../ResourceManagement/Importers/ImporterManager.h"

#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>
#include <imgui.h>

#include <cstring>

#ifdef _DEBUG
	#include <tracy/Tracy.hpp>
#endif

using namespace EditorWidgets;

namespace
{
	const char* ComponentTitle(int type)
	{
		switch (type)
		{
		case COMPONENT_TRANSFORM: return "Transform";
		case COMPONENT_MESH:      return "Mesh Renderer";
		case COMPONENT_MATERIAL:  return "Material";
		case COMPONENT_CAMERA:    return "Camera";
		default:                  return "Component";
		}
	}

	// Componente nuevo con los valores por defecto (para "Reset")
	std::unique_ptr<Component> MakeDefaultComponent(int type, GameObject* owner)
	{
		switch (type)
		{
		case COMPONENT_MESH:     return std::make_unique<MeshComponent>(owner);
		case COMPONENT_MATERIAL: return std::make_unique<MaterialComponent>(owner);
		case COMPONENT_CAMERA:   return std::make_unique<CameraComponent>(owner);
		default:                 return nullptr;
		}
	}
}

void ModuleUI::InspectorUndoGuard(const char* name)
{
	// Al empezar a editar el widget (clic en una casilla, empezar a arrastrar, entrar en un campo)
	// se guarda el estado de la escena: Ctrl+Z vuelve a como estaba antes de esta edicion
	if (ImGui::IsItemActivated()) undo.RecordSnapshot(name);
}

// =====================================================================================
// VENTANA INSPECTOR
// =====================================================================================
void ModuleUI::DrawInspectorPanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!showInspector) return;

	if (ImGui::Begin(EditorWindows::INSPECTOR, &showInspector))
	{
		Scene* scene = Engine::GetInstance().scene.get();
		GameObject* selected = scene ? scene->GetSelected() : nullptr;

		// Si cambia el GameObject seleccionado, el Inspector vuelve a mostrar GameObjects
		if (selected != lastInspectedObject)
		{
			lastInspectedObject = selected;
			if (selected != nullptr) inspectorShowsAsset = false;
			if (inspectorTransformEditing)
			{
				undo.EndTransform();
				inspectorTransformEditing = false;
			}
		}

		if (inspectorShowsAsset && !selectedAssetPath.empty())
		{
			DrawAssetInspector();
		}
		else if (selected)
		{
			DrawGameObjectInspector(selected);
		}
		else
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Select a GameObject in the Hierarchy or the Scene view,");
			ImGui::TextDisabled("or an asset in the Project window.");
		}
	}
	ImGui::End();
}

void ModuleUI::DrawGameObjectInspector(GameObject* selected)
{
	Scene* scene = Engine::GetInstance().scene.get();
	const std::vector<GameObject*>& selection = scene->GetSelection();
	const bool multi = selection.size() > 1;

	// ---------------------------------------------------------------------------------
	// Cabecera: icono grande, casilla de activo y nombre
	// ---------------------------------------------------------------------------------
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8f);
	ImGui::TextColored(EditorPalette::ICON_TEXTURE, ICON_FA_CUBE);
	ImGui::PopFont();
	ImGui::SameLine();

	const float headerY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosY(headerY + 4.0f);
	bool isActive = selected->IsActive();
	if (ImGui::Checkbox("##ActiveGO", &isActive))
	{
		for (GameObject* object : selection)
		{
			if (isActive) object->Enable();
			else object->Disable();
		}
	}
	InspectorUndoGuard("Toggle Active");
	IconTooltip("Active");
	ImGui::SameLine();

	// Nombre: se aplica al pulsar Enter o al salir del campo
	const uint64_t uid = static_cast<uint64_t>(selected->GetUUID());
	if (inspectorNameUID != uid)
	{
		strncpy_s(inspectorNameBuffer, sizeof(inspectorNameBuffer), selected->GetName().c_str(), _TRUNCATE);
		inspectorNameUID = uid;
	}
	ImGui::SetCursorPosY(headerY + 4.0f);
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.05f);
	ImGui::InputText("##GOName", inspectorNameBuffer, sizeof(inspectorNameBuffer));
	ImGui::PopFont();
	if (ImGui::IsItemDeactivatedAfterEdit() && inspectorNameBuffer[0] != '\0' && selected->GetName() != inspectorNameBuffer)
	{
		undo.RecordSnapshot("Rename");
		selected->SetName(inspectorNameBuffer);
	}
	if (!ImGui::IsItemActive() && selected->GetName() != inspectorNameBuffer)
	{
		strncpy_s(inspectorNameBuffer, sizeof(inspectorNameBuffer), selected->GetName().c_str(), _TRUNCATE);
	}
	ImGui::SetItemTooltip("UID: %llu", (unsigned long long)uid);

	if (multi)
	{
		ImGui::TextDisabled("%zu objects selected (Transform edits apply to all)", selection.size());
	}
	if (scene->GetGameCameraObject() == selected)
	{
		ImGui::TextColored(EditorPalette::ICON_CAMERA, ICON_FA_VIDEO " Main Camera (renders the Game view)");
	}
	ImGui::Spacing();

	// ---------------------------------------------------------------------------------
	// Componentes
	// ---------------------------------------------------------------------------------
	DrawTransformInspector(selected);
	DrawMeshInspector(selected);
	DrawMaterialInspector(selected);
	DrawCameraInspector(selected);

	// Padre de un modelo (sin malla propia): se puede soltar una textura para todas sus partes
	if (selected->GetComponent<MeshComponent>() == nullptr && !selected->GetChildren().empty())
	{
		const AABB bounds = selected->GetWorldAABB(true);
		if (bounds.IsValid())
		{
			ImGui::Spacing();
			const glm::vec3 size = bounds.GetSize();
			ImGui::TextDisabled("%zu children | bounds %.2f x %.2f x %.2f", selected->GetChildren().size(), size.x, size.y, size.z);
			ImGui::Button(ICON_FA_IMAGE "  Drop a texture here to apply it to every child mesh", ImVec2(-FLT_MIN, 0.0f));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_TEXTURE))
				{
					const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(payload->Data, payload->DataSize);
					undo.RecordSnapshot("Assign Texture");
					ImporterManager::AssignTextureToHierarchy(asset.uid, selected);
				}
				ImGui::EndDragDropTarget();
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	DrawAddComponentButton(selected);
}

// -------------------------------------------------------------------------------------
// Menu de tres puntos de un componente
// -------------------------------------------------------------------------------------
void ModuleUI::DrawComponentMenu(GameObject* selected, int componentType)
{
	char popupId[64];
	snprintf(popupId, sizeof(popupId), "ComponentMenu%d", componentType);
	if (!ImGui::BeginPopup(popupId)) return;

	Scene* scene = Engine::GetInstance().scene.get();
	Component* component = selected->GetComponentByType(static_cast<ComponentType>(componentType));

	if (ImGui::MenuItem("Reset"))
	{
		if (componentType == COMPONENT_TRANSFORM)
		{
			undo.BeginTransform(scene->GetSelection(), "Reset Transform");
			for (GameObject* object : scene->GetSelection())
			{
				if (TransformComponent* t = object->GetComponent<TransformComponent>())
				{
					t->SetPosition(glm::vec3(0.0f));
					t->SetRotation(glm::vec3(0.0f));
					t->SetScale(glm::vec3(1.0f));
				}
			}
			undo.EndTransform();
		}
		else if (component)
		{
			undo.RecordSnapshot("Reset Component");
			std::unique_ptr<Component> defaults = MakeDefaultComponent(componentType, selected);
			if (defaults)
			{
				nlohmann::json data = nlohmann::json::object();
				defaults->Save(data);
				component->Load(data);
			}
		}
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Remove Component", nullptr, false, componentType != COMPONENT_TRANSFORM))
	{
		undo.RecordSnapshot("Remove Component");
		if (componentType == COMPONENT_CAMERA && scene->GetGameCameraObject() == selected) scene->SetGameCamera(nullptr);
		selected->RemoveComponent(static_cast<ComponentType>(componentType));
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Copy Component") && component)
	{
		nlohmann::json data = nlohmann::json::object();
		component->Save(data);
		clipboardComponentType = componentType;
		clipboardComponentData = data.dump();
	}
	if (ImGui::MenuItem("Paste Component Values", nullptr, false, clipboardComponentType == componentType && component != nullptr))
	{
		const nlohmann::json data = nlohmann::json::parse(clipboardComponentData, nullptr, false);
		if (!data.is_discarded())
		{
			undo.RecordSnapshot("Paste Component Values");
			component->Load(data);
		}
	}
	ImGui::EndPopup();
}

// -------------------------------------------------------------------------------------
// Transform (se edita en todos los objetos seleccionados)
// -------------------------------------------------------------------------------------
void ModuleUI::DrawTransformInspector(GameObject* selected)
{
	TransformComponent* transform = selected->GetComponent<TransformComponent>();
	if (transform == nullptr) return;

	const HeaderResult header = ComponentHeader("Transform", ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "Transform", nullptr);
	if (header.menuClicked) ImGui::OpenPopup("ComponentMenu0");
	DrawComponentMenu(selected, COMPONENT_TRANSFORM);
	if (!header.open) return;

	Scene* scene = Engine::GetInstance().scene.get();
	std::vector<GameObject*> targets;
	for (GameObject* object : scene->GetSelection())
	{
		if (object->GetComponent<TransformComponent>()) targets.push_back(object);
	}
	if (targets.empty()) targets.push_back(selected);

	ImGui::Indent(6.0f);
	ImGui::PushID("TransformInspector");

	bool activated = false;
	bool deactivated = false;
	const auto row = [&](const char* label, int channel, float speed)
	{
		const auto get = [channel](const TransformComponent* t) -> glm::vec3
		{
			return channel == 0 ? t->GetPosition() : (channel == 1 ? t->GetRotation() : t->GetScale());
		};
		glm::vec3 values = get(transform);
		bool mixed[3] = { false, false, false };
		for (GameObject* object : targets)
		{
			const glm::vec3 other = get(object->GetComponent<TransformComponent>());
			for (int i = 0; i < 3; ++i) mixed[i] |= (other[i] != values[i]);
		}

		bool rowActivated = false;
		bool rowDeactivated = false;
		const int changedMask = Vec3Field(label, values, speed, mixed, rowActivated, rowDeactivated);
		if (rowActivated && !inspectorTransformEditing)
		{
			undo.BeginTransform(targets, "Transform");
			inspectorTransformEditing = true;
		}
		if (changedMask != 0)
		{
			for (GameObject* object : targets)
			{
				TransformComponent* t = object->GetComponent<TransformComponent>();
				glm::vec3 v = get(t);
				for (int i = 0; i < 3; ++i)
				{
					if (changedMask & (1 << i)) v[i] = values[i];
				}
				if (channel == 0) t->SetPosition(v);
				else if (channel == 1) t->SetRotation(v);
				else t->SetScale(v);
			}
		}
		activated |= rowActivated;
		deactivated |= rowDeactivated;
	};

	row("Position", 0, 0.05f);
	row("Rotation", 1, 0.5f);
	row("Scale", 2, 0.01f);

	if (deactivated && inspectorTransformEditing && !ImGui::IsAnyItemActive())
	{
		undo.EndTransform();
		inspectorTransformEditing = false;
	}

	ImGui::PopID();
	ImGui::Unindent(6.0f);
	ImGui::Spacing();
}

// -------------------------------------------------------------------------------------
// Mesh Renderer
// -------------------------------------------------------------------------------------
void ModuleUI::DrawMeshInspector(GameObject* selected)
{
	MeshComponent* mesh = selected->GetComponent<MeshComponent>();
	if (mesh == nullptr) return;

	bool enabled = mesh->IsActive();
	const HeaderResult header = ComponentHeader("Mesh", ICON_FA_CUBE, EditorPalette::ICON_MESH, "Mesh Renderer", &enabled);
	if (header.enabledActivated) undo.RecordSnapshot("Toggle Component");
	if (header.enabledChanged)
	{
		if (enabled) mesh->Enable();
		else mesh->Disable();
	}
	if (header.menuClicked) ImGui::OpenPopup("ComponentMenu1");
	DrawComponentMenu(selected, COMPONENT_MESH);
	if (selected->GetComponent<MeshComponent>() == nullptr || !header.open) return;

	ResourceManager* resources = Engine::GetInstance().resources.get();
	ImGui::Indent(6.0f);
	ImGui::PushID("MeshComponent");

	// ----- Campo "Mesh" (como un Object Field de Unity: combo + drag & drop) -----
	const ResourceMesh* meshResource = mesh->GetMeshResource();
	const std::string preview = std::string(ICON_FA_DRAW_POLYGON " ") +
		(meshResource ? meshResource->GetName() : (mesh->IsMeshMissing() ? std::string("Missing (Mesh)") : std::string("None (Mesh)")));
	PropertyLabel("Mesh");
	if (ImGui::BeginCombo("##MeshField", preview.c_str(), ImGuiComboFlags_HeightLarge))
	{
		if (ImGui::Selectable("None", mesh->GetMeshUID() == INVALID_UID))
		{
			undo.RecordSnapshot("Change Mesh");
			mesh->SetMesh(INVALID_UID);
		}
		if (resources)
		{
			for (const Resource* resource : resources->GetResourcesOfType(ResourceType::Mesh))
			{
				ImGui::PushID(resource);
				std::string label = resource->GetName();
				label += resource->IsBuiltIn() ? "  (Built-in)" : "  (" + FileSystem::GetFileName(resource->GetAssetPath()) + ")";
				if (ImGui::Selectable(label.c_str(), resource->GetUID() == mesh->GetMeshUID()))
				{
					undo.RecordSnapshot("Change Mesh");
					mesh->SetMesh(resource->GetUID());
				}
				ImGui::PopID();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_MESH))
		{
			const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(payload->Data, payload->DataSize);
			undo.RecordSnapshot("Change Mesh");
			mesh->SetMesh(asset.uid);
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SetItemTooltip("Pick a mesh or drag one from the Project window\n(expand a model to see its meshes)");

	if (meshResource)
	{
		ImGui::TextDisabled("%zu vertices, %zu triangles | refs: %u", meshResource->GetVertexCount(), meshResource->GetTriangleCount(), meshResource->GetReferenceCount());
		const AABB world = selected->GetWorldAABB(false);
		if (world.IsValid())
		{
			const glm::vec3 size = world.GetSize();
			ImGui::TextDisabled("Bounds: %.2f x %.2f x %.2f", size.x, size.y, size.z);
		}
	}
	else if (mesh->IsMeshMissing())
	{
		ImGui::TextColored(EditorPalette::LEVEL_ERROR, ICON_FA_TRIANGLE_EXCLAMATION " The assigned mesh no longer exists");
	}

	// ----- Depuracion de normales -----
	if (Render* render = Engine::GetInstance().render.get())
	{
		if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_SpanAvailWidth))
		{
			RenderSettings rs = render->GetSettings();
			bool vertexNormals = (rs.normals == NormalsMode::Vertex || rs.normals == NormalsMode::Both);
			bool faceNormals = (rs.normals == NormalsMode::Face || rs.normals == NormalsMode::Both);
			bool changed = false;
			changed |= ImGui::Checkbox("Vertex Normals (yellow)", &vertexNormals);
			changed |= ImGui::Checkbox("Face Normals (cyan)", &faceNormals);
			changed |= ImGui::Checkbox("Show on every object", &rs.normalsAllObjects);
			if (changed)
			{
				if (vertexNormals && faceNormals) rs.normals = NormalsMode::Both;
				else if (vertexNormals) rs.normals = NormalsMode::Vertex;
				else if (faceNormals) rs.normals = NormalsMode::Face;
				else rs.normals = NormalsMode::None;
				render->SetSettings(rs);
			}
			ImGui::TreePop();
		}
	}

	ImGui::PopID();
	ImGui::Unindent(6.0f);
	ImGui::Spacing();
}

// -------------------------------------------------------------------------------------
// Material
// -------------------------------------------------------------------------------------
void ModuleUI::DrawMaterialInspector(GameObject* selected)
{
	MaterialComponent* material = selected->GetComponent<MaterialComponent>();
	if (material == nullptr) return;

	bool enabled = material->IsActive();
	const HeaderResult header = ComponentHeader("Material", ICON_FA_PALETTE, ImVec4(0.85f, 0.55f, 0.95f, 1.0f), "Material", &enabled);
	if (header.enabledActivated) undo.RecordSnapshot("Toggle Component");
	if (header.enabledChanged)
	{
		if (enabled) material->Enable();
		else material->Disable();
	}
	if (header.menuClicked) ImGui::OpenPopup("ComponentMenu2");
	DrawComponentMenu(selected, COMPONENT_MATERIAL);
	if (selected->GetComponent<MaterialComponent>() == nullptr || !header.open) return;

	ResourceManager* resources = Engine::GetInstance().resources.get();
	ImGui::Indent(6.0f);
	ImGui::PushID("MaterialComponent");

	// Vista previa de la textura a la izquierda + propiedades (como el material de Unity)
	const unsigned int activeId = material->GetActiveTextureID();
	if (activeId != 0)
	{
		ImGui::Image((ImTextureID)(intptr_t)activeId, ImVec2(48, 48), ImVec2(0, 1), ImVec2(1, 0));
		ImGui::SameLine();
	}
	ImGui::BeginGroup();
	const ResourceTexture* texture = material->GetTexture();
	ImGui::TextUnformatted(texture ? texture->GetName().c_str() : "Default Material");
	if (texture) ImGui::TextDisabled("%d x %d | refs: %u", texture->GetWidth(), texture->GetHeight(), texture->GetReferenceCount());
	ImGui::EndGroup();
	ImGui::Spacing();

	glm::vec4 color = material->GetColor();
	PropertyLabel("Albedo Color");
	if (ImGui::ColorEdit4("##Color", glm::value_ptr(color), ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_AlphaBar)) material->SetColor(color);
	InspectorUndoGuard("Change Color");

	// ----- Campo "Albedo" (textura) -----
	const std::string preview = std::string(ICON_FA_IMAGE " ") +
		(texture ? texture->GetName() : (material->IsTextureMissing() ? std::string("Missing (Texture)") : std::string("None (Texture)")));
	PropertyLabel("Albedo");
	if (ImGui::BeginCombo("##TextureField", preview.c_str(), ImGuiComboFlags_HeightLarge))
	{
		if (ImGui::Selectable("None", material->GetTextureUID() == INVALID_UID))
		{
			undo.RecordSnapshot("Change Texture");
			material->SetTexture(INVALID_UID);
		}
		if (resources)
		{
			for (const Resource* resource : resources->GetResourcesOfType(ResourceType::Texture))
			{
				ImGui::PushID(resource);
				const std::string label = resource->GetName() + "  (" + FileSystem::GetParentPath(resource->GetAssetPath()) + ")";
				if (ImGui::Selectable(label.c_str(), resource->GetUID() == material->GetTextureUID()))
				{
					undo.RecordSnapshot("Change Texture");
					material->SetTexture(resource->GetUID());
					material->SetUseTexture(true);
				}
				ImGui::PopID();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_TEXTURE))
		{
			const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(payload->Data, payload->DataSize);
			undo.RecordSnapshot("Change Texture");
			material->SetTexture(asset.uid);
			material->SetUseTexture(true);
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SetItemTooltip("Pick a texture or drag one from the Project window");

	bool usingTexture = material->GetUseTexture();
	PropertyLabel("Use Texture");
	if (ImGui::Checkbox("##UseTexture", &usingTexture)) material->SetUseTexture(usingTexture);
	InspectorUndoGuard("Toggle Texture");

	bool usingChecker = material->GetUseCheckerboard();
	PropertyLabel("Checkerboard");
	if (ImGui::Checkbox("##Checker", &usingChecker)) material->SetUseCheckerboard(usingChecker);
	InspectorUndoGuard("Toggle Checkerboard");

	if (material->IsTextureMissing())
	{
		ImGui::TextColored(EditorPalette::LEVEL_ERROR, ICON_FA_TRIANGLE_EXCLAMATION " The assigned texture no longer exists");
	}

	ImGui::PopID();
	ImGui::Unindent(6.0f);
	ImGui::Spacing();
}

// -------------------------------------------------------------------------------------
// Camera
// -------------------------------------------------------------------------------------
void ModuleUI::DrawCameraInspector(GameObject* selected)
{
	CameraComponent* cam = selected->GetComponent<CameraComponent>();
	if (cam == nullptr) return;

	bool enabled = cam->IsActive();
	const HeaderResult header = ComponentHeader("Camera", ICON_FA_VIDEO, EditorPalette::ICON_CAMERA, "Camera", &enabled);
	if (header.enabledActivated) undo.RecordSnapshot("Toggle Component");
	if (header.enabledChanged)
	{
		if (enabled) cam->Enable();
		else cam->Disable();
	}
	if (header.menuClicked) ImGui::OpenPopup("ComponentMenu3");
	DrawComponentMenu(selected, COMPONENT_CAMERA);
	if (selected->GetComponent<CameraComponent>() == nullptr || !header.open) return;

	Scene* scene = Engine::GetInstance().scene.get();
	ImGui::Indent(6.0f);
	ImGui::PushID("CameraComponent");

	int projection = cam->orthographic ? 1 : 0;
	const char* projections[] = { "Perspective", "Orthographic" };
	PropertyLabel("Projection");
	if (ImGui::BeginCombo("##Projection", projections[projection]))
	{
		for (int i = 0; i < 2; ++i)
		{
			if (ImGui::Selectable(projections[i], projection == i) && projection != i)
			{
				undo.RecordSnapshot("Change Projection");
				cam->orthographic = (i == 1);
			}
		}
		ImGui::EndCombo();
	}

	if (cam->orthographic)
	{
		PropertyLabel("Size");
		ImGui::DragFloat("##OrthoSize", &cam->orthoSize, 0.05f, 0.01f, 1000.0f, "%.2f");
		InspectorUndoGuard("Change Size");
	}
	else
	{
		PropertyLabel("Field of View");
		ImGui::SliderFloat("##FOV", &cam->fov, 1.0f, 179.0f, "%.1f");
		InspectorUndoGuard("Change Field of View");
	}

	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Clipping Planes");
	ImGui::Indent(12.0f);
	PropertyLabel("Near");
	ImGui::DragFloat("##Near", &cam->nearPlane, 0.01f, 0.01f, 10.0f, "%.2f");
	InspectorUndoGuard("Change Near Plane");
	PropertyLabel("Far");
	ImGui::DragFloat("##Far", &cam->farPlane, 1.0f, 1.0f, 10000.0f, "%.1f");
	InspectorUndoGuard("Change Far Plane");
	ImGui::Unindent(12.0f);
	if (cam->farPlane <= cam->nearPlane) cam->farPlane = cam->nearPlane + 0.1f;

	// Target Display (como Unity): en que display de la vista Game se ve esta camara
	char displayLabel[32];
	snprintf(displayLabel, sizeof(displayLabel), "Display %d", cam->targetDisplay + 1);
	PropertyLabel("Target Display");
	if (ImGui::BeginCombo("##TargetDisplay", displayLabel))
	{
		for (int display = 0; display < CameraComponent::MAX_DISPLAYS; ++display)
		{
			char label[32];
			snprintf(label, sizeof(label), "Display %d", display + 1);
			if (ImGui::Selectable(label, cam->targetDisplay == display) && cam->targetDisplay != display)
			{
				undo.RecordSnapshot("Change Target Display");
				cam->targetDisplay = display;
			}
		}
		ImGui::EndCombo();
	}

	PropertyLabel("Frustum Culling");
	ImGui::Checkbox("##FrustumCulling", &cam->frustumCulling);
	InspectorUndoGuard("Toggle Frustum Culling");
	ImGui::SetItemTooltip("The Game view only draws the objects whose AABB is inside the frustum");

	const bool isMain = scene && scene->GetGameCameraObject() == selected;
	PropertyLabel("Main Camera");
	if (isMain)
	{
		ImGui::TextColored(EditorPalette::ICON_CAMERA, "Yes (Game view)");
	}
	else if (scene && ImGui::Button("Set as Main Camera"))
	{
		undo.RecordSnapshot("Set Main Camera");
		scene->SetGameCamera(selected);
	}

	ImGui::PopID();
	ImGui::Unindent(6.0f);
	ImGui::Spacing();
}

// -------------------------------------------------------------------------------------
// Add Component (boton centrado con buscador, como Unity)
// -------------------------------------------------------------------------------------
void ModuleUI::DrawAddComponentButton(GameObject* selected)
{
	const float width = std::min(260.0f, ImGui::GetContentRegionAvail().x);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - width) * 0.5f);
	if (ImGui::Button("Add Component", ImVec2(width, ImGui::GetFrameHeight() + 4.0f)))
	{
		addComponentSearch[0] = '\0';
		ImGui::OpenPopup("AddComponentPopup");
	}

	ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		ImGui::TextDisabled("Component");
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##AddComponentSearch", ICON_FA_MAGNIFYING_GLASS " Search", addComponentSearch, sizeof(addComponentSearch));
		ImGui::Separator();

		Scene* scene = Engine::GetInstance().scene.get();
		const std::string searchLower = ToLower(addComponentSearch);
		struct Entry { const char* icon; const char* name; const char* category; int type; };
		const Entry entries[] = {
			{ ICON_FA_CUBE, "Mesh Renderer", "Rendering", COMPONENT_MESH },
			{ ICON_FA_PALETTE, "Material", "Rendering", COMPONENT_MATERIAL },
			{ ICON_FA_VIDEO, "Camera", "Rendering", COMPONENT_CAMERA },
		};
		bool any = false;
		for (const Entry& entry : entries)
		{
			if (!ContainsCaseInsensitive(entry.name, searchLower) && !ContainsCaseInsensitive(entry.category, searchLower)) continue;
			any = true;
			const bool present = selected->GetComponentByType(static_cast<ComponentType>(entry.type)) != nullptr;
			const std::string label = std::string(entry.icon) + "  " + entry.name;
			if (ImGui::MenuItem(label.c_str(), entry.category, false, !present))
			{
				undo.RecordSnapshot(std::string("Add ") + entry.name);
				if (entry.type == COMPONENT_MESH) selected->AddComponent(std::make_unique<MeshComponent>(selected));
				else if (entry.type == COMPONENT_MATERIAL) selected->AddComponent(std::make_unique<MaterialComponent>(selected));
				else if (entry.type == COMPONENT_CAMERA)
				{
					selected->AddComponent(std::make_unique<CameraComponent>(selected));
					if (scene && scene->GetGameCameraObject() == nullptr) scene->SetGameCamera(selected);
				}
				ImGui::CloseCurrentPopup();
			}
		}
		if (!any) ImGui::TextDisabled("No components match");
		ImGui::EndPopup();
	}
}
