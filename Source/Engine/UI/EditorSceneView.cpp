// =====================================================================================
// Ventanas Scene y Game del editor (parte de ModuleUI), con el manejo de Unity:
//   - Barra superior: Pivot / Center, Global / Local, ajuste (snap), modo de dibujo, gizmos,
//     camara y estadisticas
//   - Overlay "Tools" (Q W E R Y), gizmo de orientacion (clic en un eje = vista alineada,
//     centro = Persp / Iso), iconos de camara
//   - Gizmo de transformacion para uno o varios objetos y seleccion por clic o rectangulo
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
#include "../ResourceManagement/Importers/ImporterManager.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <functional>

#ifdef _DEBUG
	#include <tracy/Tracy.hpp>
#endif

using namespace EditorWidgets;

namespace
{
	// Objeto de camara bajo el raton (iconos de la vista Scene), calculado al dibujar los iconos
	GameObject* hoveredSceneIcon = nullptr;

	// Recorre todos los GameObjects de la escena
	void ForEachObject(GameObject* node, const std::function<void(GameObject*)>& callback)
	{
		if (node == nullptr) return;
		for (GameObject* child : node->GetChildren())
		{
			callback(child);
			ForEachObject(child, callback);
		}
	}

	const char* ToolName(int tool)
	{
		static const char* names[] = { "View Tool (Q)", "Move Tool (W)", "Rotate Tool (E)", "Scale Tool (R)", "Transform Tool (Y)" };
		return (tool >= 0 && tool < 5) ? names[tool] : "";
	}

	// Ventana hija que se dibuja ENCIMA de la imagen de la escena, con el aspecto de un overlay de Unity
	bool BeginOverlay(const char* id, const ImVec2& screenPos, const ImVec2& size, float alpha = 0.92f)
	{
		ImGui::SetCursorScreenPos(screenPos);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.16f, alpha));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f, 3.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
		const bool visible = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
		return visible;
	}

	void EndOverlay()
	{
		ImGui::EndChild();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);
	}
}

bool ModuleUI::WorldToScreen(const glm::vec3& world, const glm::mat4& viewProjection, const ImVec2& imagePos, const ImVec2& imageSize, ImVec2& out) const
{
	const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
	if (clip.w <= 1e-5f) return false;
	const glm::vec3 ndc = glm::vec3(clip) / clip.w;
	out.x = imagePos.x + (ndc.x * 0.5f + 0.5f) * imageSize.x;
	out.y = imagePos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * imageSize.y;
	return ndc.z >= -1.0f && ndc.z <= 1.0f;
}

// =====================================================================================
// VENTANA SCENE
// =====================================================================================
void ModuleUI::DrawScenePanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	Render* render = Engine::GetInstance().render.get();
	Scene* scene = Engine::GetInstance().scene.get();
	CameraComponent* camera = GetEditorCamera();

	// El estado del gizmo se recalcula cada frame: si este frame no llega a dibujarse no puede
	// quedarse "enganchado" bloqueando la camara o la seleccion
	gizmoHovered = false;
	gizmoInUse = false;
	sceneViewHovered = false;
	hoveredSceneIcon = nullptr;

	const auto hideView = [&]()
	{
		if (render) render->SetViewportVisible(false);
		if (camera)
		{
			camera->SetViewportHovered(false);
			camera->SetGizmoState(false, false);
		}
		if (gizmoWasInUse)
		{
			undo.EndTransform();
			gizmoWasInUse = false;
		}
	};

	if (!showScene)
	{
		hideView();
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool visible = ImGui::Begin(EditorWindows::SCENE, &showScene, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) sceneOrHierarchyFocused = true;

	if (!visible || render == nullptr || scene == nullptr || camera == nullptr)
	{
		hideView();
		ImGui::End();
		return;
	}
	render->SetViewportVisible(true);

	DrawSceneToolbar();

	const ImVec2 imagePos = ImGui::GetCursorScreenPos();
	ImVec2 imageSize = ImGui::GetContentRegionAvail();
	imageSize.x = std::max(imageSize.x, 1.0f);
	imageSize.y = std::max(imageSize.y, 1.0f);
	render->SetViewportSize(static_cast<int>(imageSize.x), static_cast<int>(imageSize.y));
	camera->SetViewportPixelSize(imageSize.x, imageSize.y);
	camera->SetHandToolActive(currentTool == Tool::View);

	const unsigned int sceneTexture = render->GetSceneTexture();
	bool imageHovered = false;
	if (sceneTexture != 0)
	{
		ImGui::Image((ImTextureID)(intptr_t)sceneTexture, imageSize, ImVec2(0, 1), ImVec2(1, 0));
		HandleSceneDragDrop(imagePos, imageSize, camera);

		// Solo la propia imagen: encima de un overlay (ventana hija) no cuenta como "escena"
		imageHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(imagePos, ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y));

		DrawSceneIcons(imagePos, imageSize, camera);
		DrawTransformGizmo(imagePos, imageSize, camera);
		HandleSceneSelection(imagePos, imageSize, camera, imageHovered);
		DrawSceneOverlays(imagePos, imageSize, camera);
		DrawToolsOverlay(imagePos);
		DrawOrientationGizmo(imagePos, imageSize, camera);
	}

	sceneViewHovered = imageHovered || ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

	// La camara lee este estado en su proximo Update (Scene se actualiza antes que la UI)
	camera->SetGizmoState(gizmoHovered, gizmoInUse);
	const bool mouseDisabledForCamera = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) != 0;
	if (camera->IsControllingCamera() || mouseDisabledForCamera)
	{
		camera->SetViewportHovered(true);
	}
	else
	{
		camera->SetViewportHovered(imageHovered && !marqueeActive);
	}

	// Cursor de la herramienta Hand (como Unity)
	if (imageHovered && currentTool == Tool::View && !camera->IsControllingCamera())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}

	ImGui::End();
}

// -------------------------------------------------------------------------------------
// Barra superior de la vista Scene
// -------------------------------------------------------------------------------------
void ModuleUI::DrawSceneToolbar()
{
	Render* render = Engine::GetInstance().render.get();
	Scene* scene = Engine::GetInstance().scene.get();
	CameraComponent* camera = GetEditorCamera();
	if (!render || !scene || !camera) return;

	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorPalette::PANEL_TOOLBAR);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
	ImGui::BeginChild("SceneToolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + 4.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// ----- Pivot / Center -----
	const char* pivotLabel = (pivotMode == PivotMode::Pivot) ? ICON_FA_LOCATION_CROSSHAIRS " Pivot " ICON_FA_CARET_DOWN "###PivotMode" : ICON_FA_CIRCLE_DOT " Center " ICON_FA_CARET_DOWN "###PivotMode";
	if (FlatButton(pivotLabel)) ImGui::OpenPopup("PivotModePopup");
	IconTooltip("Tool Handle Position (Z)\n  Pivot: at the active object's pivot\n  Center: at the center of the selection");
	if (ImGui::BeginPopup("PivotModePopup"))
	{
		if (ImGui::MenuItem("Pivot", nullptr, pivotMode == PivotMode::Pivot)) pivotMode = PivotMode::Pivot;
		if (ImGui::MenuItem("Center", nullptr, pivotMode == PivotMode::Center)) pivotMode = PivotMode::Center;
		ImGui::EndPopup();
	}

	// ----- Global / Local -----
	ImGui::SameLine();
	const char* spaceLabel = (gizmoSpace == GizmoSpace::Local) ? ICON_FA_CUBE " Local " ICON_FA_CARET_DOWN "###GizmoSpace" : ICON_FA_GLOBE " Global " ICON_FA_CARET_DOWN "###GizmoSpace";
	if (FlatButton(spaceLabel)) ImGui::OpenPopup("GizmoSpacePopup");
	IconTooltip("Tool Handle Rotation (X)\n  Local: the object's axes\n  Global: world axes\nScale always uses local axes.");
	if (ImGui::BeginPopup("GizmoSpacePopup"))
	{
		if (ImGui::MenuItem("Global", nullptr, gizmoSpace == GizmoSpace::World)) gizmoSpace = GizmoSpace::World;
		if (ImGui::MenuItem("Local", nullptr, gizmoSpace == GizmoSpace::Local)) gizmoSpace = GizmoSpace::Local;
		ImGui::EndPopup();
	}

	// ----- Ajuste por incrementos (Ctrl mientras se arrastra) -----
	ImGui::SameLine();
	if (FlatButton(ICON_FA_MAGNET " " ICON_FA_CARET_DOWN "###SnapSettings")) ImGui::OpenPopup("SnapPopup");
	IconTooltip("Increment Snapping\nHold Ctrl while dragging a handle to snap.");
	if (ImGui::BeginPopup("SnapPopup"))
	{
		ImGui::TextDisabled("Increment Snapping (hold Ctrl)");
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Move", &snapMove, 0.01f, 0.001f, 100.0f, "%.3g");
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Rotate", &snapRotate, 0.5f, 0.1f, 180.0f, "%.3g");
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Scale", &snapScale, 0.01f, 0.001f, 10.0f, "%.3g");
		if (ImGui::Button("Reset")) { snapMove = 0.25f; snapRotate = 15.0f; snapScale = 0.1f; }
		ImGui::EndPopup();
	}

	// ----- Derecha: modo de dibujo, gizmos, camara y estadisticas -----
	RenderSettings rs = render->GetSettings();
	const char* shadingNames[] = { "Shaded", "Wireframe", "Shaded Wireframe" };
	const int shadingIndex = static_cast<int>(rs.shading);
	const std::string shadingLabel = std::string(ICON_FA_CIRCLE_HALF_STROKE " ") + shadingNames[shadingIndex] + " " ICON_FA_CARET_DOWN "###ShadingMode";
	const float rightWidth = ImGui::CalcTextSize(shadingLabel.c_str(), nullptr, true).x + 200.0f;
	ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowWidth() - rightWidth));

	if (FlatButton(shadingLabel.c_str())) ImGui::OpenPopup("ShadingPopup");
	IconTooltip("Draw Mode");
	if (ImGui::BeginPopup("ShadingPopup"))
	{
		for (int i = 0; i < 3; ++i)
		{
			if (ImGui::MenuItem(shadingNames[i], nullptr, shadingIndex == i))
			{
				rs.shading = static_cast<ShadingMode>(i);
				render->SetSettings(rs);
			}
		}
		ImGui::Separator();
		bool lighting = rs.lighting;
		if (ImGui::MenuItem("Lighting", nullptr, &lighting))
		{
			rs.lighting = lighting;
			render->SetSettings(rs);
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (FlatButton(ICON_FA_VIDEO " " ICON_FA_CARET_DOWN "###SceneCameraButton")) ImGui::OpenPopup("SceneCameraPopup");
	IconTooltip("Scene Camera settings");
	if (ImGui::BeginPopup("SceneCameraPopup"))
	{
		ImGui::SeparatorText("Scene Camera");
		ImGui::SetNextItemWidth(160.0f);
		ImGui::SliderFloat("Field of View", &camera->fov, 10.0f, 120.0f, "%.0f");
		bool ortho = camera->IsOrthographic();
		if (ImGui::Checkbox("Orthographic (Iso)", &ortho)) camera->SetOrthographic(ortho);
		ImGui::SetNextItemWidth(160.0f);
		ImGui::DragFloat("Near", &camera->nearPlane, 0.01f, 0.01f, 10.0f, "%.2f");
		ImGui::SetNextItemWidth(160.0f);
		ImGui::DragFloat("Far", &camera->farPlane, 1.0f, 10.0f, 10000.0f, "%.0f");
		float speed = camera->GetMovementSpeed();
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::SliderFloat("Camera Speed", &speed, 0.05f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) camera->SetMovementSpeed(speed);
		ImGui::Checkbox("Frustum Culling", &camera->frustumCulling);
		if (ImGui::Button("Reset")) camera->ResetCamera();
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (FlatButton(ICON_FA_EYE " Gizmos " ICON_FA_CARET_DOWN "###SceneGizmosButton")) ImGui::OpenPopup("SceneGizmosPopup");
	if (ImGui::BeginPopup("SceneGizmosPopup"))
	{
		bool changed = false;
		ImGui::SeparatorText("Grid");
		changed |= ImGui::Checkbox("Show Grid", &rs.showGrid);
		changed |= ImGui::Checkbox("Axis Lines", &rs.showAxis);
		ImGui::SeparatorText("Selection");
		changed |= ImGui::Checkbox("Selection Outline", &rs.highlightSelection);
		changed |= ImGui::Checkbox("Selection Bounds", &rs.showSelectedAABB);
		ImGui::SeparatorText("Culling");
		changed |= ImGui::Checkbox("Culling with Scene Camera", &rs.editorFrustumCulling);
		changed |= ImGui::Checkbox("Preview Main Camera Culling", &rs.cullingPreview);
		bool useOctree = scene->IsUsingOctree();
		if (ImGui::Checkbox("Use Octree", &useOctree)) scene->SetUseOctree(useOctree);
		ImGui::SeparatorText("Debug");
		changed |= ImGui::Checkbox("Mesh Bounds (green / red)", &rs.showAABBs);
		changed |= ImGui::Checkbox("Main Camera Frustum", &rs.showCameraFrustum);
		changed |= ImGui::Checkbox("Octree Nodes", &rs.showOctree);
		changed |= ImGui::Checkbox("Picking Ray", &rs.showPickRay);
		if (changed) render->SetSettings(rs);
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (FlatButton(ICON_FA_CHART_SIMPLE "###SceneStats", ImVec2(0, 0), sceneShowStats)) sceneShowStats = !sceneShowStats;
	IconTooltip("Statistics");

	ImGui::EndChild();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor();
}

// -------------------------------------------------------------------------------------
// Overlay "Tools" (vertical, arriba a la izquierda, como Unity 6)
// -------------------------------------------------------------------------------------
void ModuleUI::DrawToolsOverlay(const ImVec2& imagePos)
{
	const float buttonSize = ImGui::GetFrameHeight() + 6.0f;
	const ImVec2 size(buttonSize + 8.0f, buttonSize * 5.0f + 2.0f * 4.0f + 8.0f);
	if (BeginOverlay("##ToolsOverlay", ImVec2(imagePos.x + 8.0f, imagePos.y + 8.0f), size))
	{
		const char* icons[] = { ICON_FA_HAND, ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, ICON_FA_ARROWS_ROTATE, ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, ICON_FA_VECTOR_SQUARE };
		for (int i = 0; i < 5; ++i)
		{
			ImGui::PushID(i);
			const bool active = (static_cast<int>(currentTool) == i);
			if (FlatButton(icons[i], ImVec2(buttonSize, buttonSize), active)) currentTool = static_cast<Tool>(i);
			IconTooltip(ToolName(i));
			ImGui::PopID();
		}
	}
	EndOverlay();
}

// -------------------------------------------------------------------------------------
// Gizmo de orientacion (arriba a la derecha)
// -------------------------------------------------------------------------------------
void ModuleUI::DrawOrientationGizmo(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera)
{
	const float gizmoSize = 96.0f;
	const float labelHeight = ImGui::GetFrameHeight();
	const ImVec2 overlayPos(imagePos.x + imageSize.x - gizmoSize - 10.0f, imagePos.y + 8.0f);

	ImGui::SetCursorScreenPos(overlayPos);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("##OrientationGizmo", ImVec2(gizmoSize, gizmoSize + labelHeight), ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 center(overlayPos.x + gizmoSize * 0.5f, overlayPos.y + gizmoSize * 0.5f);
	const float radius = gizmoSize * 0.34f;
	const ImVec2 mouse = ImGui::GetMousePos();
	const bool windowHovered = ImGui::IsWindowHovered();

	// Rotacion de la vista: direccion de cada eje del mundo en pantalla
	const glm::mat3 viewRotation = glm::mat3(camera->GetViewMatrix());
	struct AxisHandle
	{
		glm::vec3 axis;
		ImVec2 screen;
		float depth;
		ImU32 color;
		const char* label;
		bool positive;
	};
	const ImU32 colors[3] = { IM_COL32(219, 62, 29, 255), IM_COL32(154, 220, 46, 255), IM_COL32(58, 122, 248, 255) };
	const char* labels[3] = { "x", "y", "z" };
	AxisHandle handles[6];
	for (int i = 0; i < 3; ++i)
	{
		for (int s = 0; s < 2; ++s)
		{
			glm::vec3 axis(0.0f);
			axis[i] = (s == 0) ? 1.0f : -1.0f;
			const glm::vec3 v = viewRotation * axis;
			AxisHandle& h = handles[i * 2 + s];
			h.axis = axis;
			h.screen = ImVec2(center.x + v.x * radius, center.y - v.y * radius);
			h.depth = v.z; // > 0: hacia la camara
			h.color = (s == 0) ? colors[i] : IM_COL32(170, 170, 170, 255);
			h.label = labels[i];
			h.positive = (s == 0);
		}
	}

	// Eje bajo el raton (el mas cercano a la camara si se solapan)
	int hovered = -1;
	float bestDepth = -FLT_MAX;
	if (windowHovered)
	{
		for (int i = 0; i < 6; ++i)
		{
			// Un eje que apunta directamente a la camara cae encima del cubo central: ahi manda el cubo
			const float ox = handles[i].screen.x - center.x;
			const float oy = handles[i].screen.y - center.y;
			if (ox * ox + oy * oy < 8.0f * 8.0f) continue;
			const float dx = mouse.x - handles[i].screen.x;
			const float dy = mouse.y - handles[i].screen.y;
			if (dx * dx + dy * dy <= 10.0f * 10.0f && handles[i].depth > bestDepth)
			{
				bestDepth = handles[i].depth;
				hovered = i;
			}
		}
	}
	const bool centerHovered = windowHovered && hovered < 0 &&
		std::abs(mouse.x - center.x) < 9.0f && std::abs(mouse.y - center.y) < 9.0f;

	// Se dibujan de lejos a cerca
	int order[6] = { 0, 1, 2, 3, 4, 5 };
	std::sort(order, order + 6, [&](int a, int b) { return handles[a].depth < handles[b].depth; });

	bool centerDrawn = false;
	const auto drawCenter = [&]()
	{
		const ImU32 cubeColor = centerHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 255);
		drawList->AddRectFilled(ImVec2(center.x - 7.0f, center.y - 7.0f), ImVec2(center.x + 7.0f, center.y + 7.0f), cubeColor, 2.0f);
		drawList->AddRect(ImVec2(center.x - 7.0f, center.y - 7.0f), ImVec2(center.x + 7.0f, center.y + 7.0f), IM_COL32(60, 60, 60, 255), 2.0f);
		centerDrawn = true;
	};

	for (int k = 0; k < 6; ++k)
	{
		const AxisHandle& h = handles[order[k]];
		if (!centerDrawn && h.depth > 0.0f) drawCenter();
		const bool isHovered = (order[k] == hovered);
		const ImU32 color = isHovered ? IM_COL32(255, 235, 60, 255) : h.color;
		if (h.positive)
		{
			drawList->AddLine(center, h.screen, color, 3.0f);
			drawList->AddCircleFilled(h.screen, 8.5f, color);
			const ImVec2 textSize = ImGui::CalcTextSize(h.label);
			drawList->AddText(ImVec2(h.screen.x - textSize.x * 0.5f, h.screen.y - textSize.y * 0.5f - 1.0f), IM_COL32(20, 20, 20, 255), h.label);
		}
		else
		{
			drawList->AddCircleFilled(h.screen, 6.5f, color);
		}
	}
	if (!centerDrawn) drawCenter();

	// ----- Clics: eje -> vista alineada (isometrica, como Unity) | centro -> Persp / Iso -----
	if (windowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (hovered >= 0)
		{
			camera->AlignViewToAxis(handles[hovered].axis, true);
			camera->SetOrthographic(true);
		}
		else if (centerHovered)
		{
			camera->SetOrthographic(!camera->IsOrthographic());
		}
	}

	// ----- Etiqueta: "Persp", "Iso" o el nombre de la vista alineada -----
	const char* viewName = nullptr;
	if (camera->IsOrthographic())
	{
		const glm::vec3 f = camera->GetFront();
		if (f.x < -0.999f) viewName = "Right";
		else if (f.x > 0.999f) viewName = "Left";
		else if (f.y < -0.999f) viewName = "Top";
		else if (f.y > 0.999f) viewName = "Bottom";
		else if (f.z < -0.999f) viewName = "Front";
		else if (f.z > 0.999f) viewName = "Back";
	}
	std::string label = camera->IsOrthographic() ? std::string(ICON_FA_BARS " ") + (viewName ? viewName : "Iso") : std::string(ICON_FA_ANGLE_LEFT " Persp");
	ImGui::SetCursorScreenPos(ImVec2(overlayPos.x, overlayPos.y + gizmoSize - 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
	if (FlatButton((label + "###ProjectionLabel").c_str(), ImVec2(gizmoSize, labelHeight)))
	{
		camera->SetOrthographic(!camera->IsOrthographic());
	}
	ImGui::PopStyleVar();
	IconTooltip("Click to switch between Perspective and Isometric (orthographic)");

	if (hovered >= 0 || centerHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

// -------------------------------------------------------------------------------------
// Iconos de las camaras (se pueden seleccionar con un clic, como en Unity)
// -------------------------------------------------------------------------------------
void ModuleUI::DrawSceneIcons(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera)
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;

	const float aspect = imageSize.x / imageSize.y;
	const glm::mat4 viewProjection = camera->GetProjectionMatrix(aspect) * camera->GetViewMatrix();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 mouse = ImGui::GetMousePos();
	const bool canHover = ImGui::IsWindowHovered() && !camera->IsControllingCamera();

	ImGui::PushClipRect(imagePos, ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y), true);
	ForEachObject(scene->GetRoot(), [&](GameObject* object)
	{
		if (!object->IsActiveInHierarchy() || object->GetComponent<CameraComponent>() == nullptr) return;
		ImVec2 screen;
		if (!WorldToScreen(glm::vec3(object->GetWorldMatrix()[3]), viewProjection, imagePos, imageSize, screen)) return;

		const float r = 11.0f;
		const bool hovered = canHover && std::abs(mouse.x - screen.x) < r && std::abs(mouse.y - screen.y) < r;
		if (hovered) hoveredSceneIcon = object;
		const bool selected = scene->IsSelected(object);
		const ImU32 bg = selected ? IM_COL32(44, 93, 135, 230) : (hovered ? IM_COL32(80, 80, 80, 230) : IM_COL32(40, 40, 40, 200));
		drawList->AddCircleFilled(screen, r, bg);
		drawList->AddCircle(screen, r, IM_COL32(20, 20, 20, 255));
		const ImVec2 textSize = ImGui::CalcTextSize(ICON_FA_VIDEO);
		drawList->AddText(ImVec2(screen.x - textSize.x * 0.5f, screen.y - textSize.y * 0.5f), IM_COL32(230, 230, 230, 255), ICON_FA_VIDEO);
	});
	ImGui::PopClipRect();
}

// -------------------------------------------------------------------------------------
// Avisos sobre la imagen: velocidad de la camara y estadisticas
// -------------------------------------------------------------------------------------
void ModuleUI::DrawSceneOverlays(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera)
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	// Velocidad de vuelo (cambiada con la rueda mientras se mantiene el clic derecho)
	if (camera->GetSpeedOverlayTime() > 0.0f)
	{
		char text[64];
		snprintf(text, sizeof(text), "Camera speed: %.2f", camera->GetMovementSpeed());
		const ImVec2 textSize = ImGui::CalcTextSize(text);
		const ImVec2 pos(imagePos.x + (imageSize.x - textSize.x) * 0.5f, imagePos.y + 12.0f);
		const float alpha = std::min(1.0f, camera->GetSpeedOverlayTime() * 2.0f);
		drawList->AddRectFilled(ImVec2(pos.x - 10.0f, pos.y - 5.0f), ImVec2(pos.x + textSize.x + 10.0f, pos.y + textSize.y + 5.0f),
			ImGui::GetColorU32(ImVec4(0.12f, 0.12f, 0.12f, 0.85f * alpha)), 4.0f);
		drawList->AddText(pos, ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, alpha)), text);
	}

	// Estadisticas (boton de la barra superior)
	if (sceneShowStats)
	{
		Render* render = Engine::GetInstance().render.get();
		Scene* scene = Engine::GetInstance().scene.get();
		if (render == nullptr || scene == nullptr) return;
		const RenderStats& stats = render->GetStats();
		const auto& perf = Engine::GetInstance().GetPerformanceData();

		char lines[512];
		snprintf(lines, sizeof(lines),
			"Statistics\n"
			"FPS: %.0f  (%.2f ms)\n"
			"Drawn: %d / %d %s\n"
			"Octree nodes visited: %d\n"
			"AABBs tested: %d\n"
			"Selected: %zu\n"
			"Camera: %s",
			perf.currentFps, perf.currentMs, stats.sceneDrawn, stats.sceneCandidates, stats.sceneCulling ? "(culled)" : "(no culling)",
			stats.sceneQuery.nodesVisited, stats.sceneQuery.objectsTested, scene->GetSelection().size(),
			camera->IsOrthographic() ? "Isometric" : "Perspective");
		const ImVec2 textSize = ImGui::CalcTextSize(lines);
		const ImVec2 pos(imagePos.x + imageSize.x - textSize.x - 22.0f, imagePos.y + 140.0f);
		drawList->AddRectFilled(ImVec2(pos.x - 8.0f, pos.y - 6.0f), ImVec2(pos.x + textSize.x + 8.0f, pos.y + textSize.y + 6.0f), IM_COL32(40, 40, 40, 225), 4.0f);
		drawList->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), lines);
	}
}

// -------------------------------------------------------------------------------------
// Gizmo de transformacion (ImGuizmo) para toda la seleccion
// -------------------------------------------------------------------------------------
void ModuleUI::DrawTransformGizmo(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera)
{
	Scene* scene = Engine::GetInstance().scene.get();
	GameObject* active = scene ? scene->GetSelected() : nullptr;

	const auto finishDrag = [this]()
	{
		if (gizmoWasInUse)
		{
			undo.EndTransform();
			gizmoWasInUse = false;
			gizmoStartWorld.clear();
		}
	};

	if (active == nullptr || currentTool == Tool::View)
	{
		finishDrag();
		return;
	}

	const std::vector<GameObject*> objects = scene->GetTopLevelSelection();
	if (objects.empty())
	{
		finishDrag();
		return;
	}

	ImGuizmo::SetOrthographic(camera->IsOrthographic());
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(imagePos.x, imagePos.y, imageSize.x, imageSize.y);

	// Mismas matrices con las que Render::RenderScene dibuja la vista (aspect del framebuffer entero)
	const float aspectRatio = static_cast<float>(static_cast<int>(imageSize.x)) / static_cast<float>(static_cast<int>(imageSize.y));
	const glm::mat4 view = camera->GetViewMatrix();
	const glm::mat4 projection = camera->GetProjectionMatrix(aspectRatio);

	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
	float snapStep = snapMove;
	switch (currentTool)
	{
	case Tool::Rotate:    operation = ImGuizmo::ROTATE; snapStep = snapRotate; break;
	case Tool::Scale:     operation = ImGuizmo::SCALE; snapStep = snapScale; break;
	case Tool::Transform: operation = ImGuizmo::UNIVERSAL; snapStep = snapMove; break;
	default: break;
	}

	// ---------------------------------------------------------------------------------
	// Matriz del gizmo. Mientras no se arrastra se reconstruye cada frame a partir de la
	// seleccion; mientras se arrastra se conserva la que va modificando ImGuizmo.
	// ---------------------------------------------------------------------------------
	const bool dragging = ImGuizmo::IsUsing() && gizmoWasInUse;
	if (!dragging)
	{
		const glm::mat4 activeWorld = active->GetWorldMatrix();
		glm::vec3 position = glm::vec3(activeWorld[3]);
		if (pivotMode == PivotMode::Center)
		{
			AABB bounds;
			for (GameObject* object : objects)
			{
				const AABB box = object->GetWorldAABB(true);
				if (box.IsValid()) bounds.Enclose(box);
				else bounds.Enclose(glm::vec3(object->GetWorldMatrix()[3]));
			}
			if (bounds.IsValid()) position = bounds.GetCenter();
		}
		// Orientacion del objeto activo sin escala (el gizmo mide los cambios respecto a ella)
		glm::mat4 rotationOnly(1.0f);
		for (int c = 0; c < 3; ++c)
		{
			const glm::vec3 axis = glm::vec3(activeWorld[c]);
			const float length = glm::length(axis);
			rotationOnly[c] = glm::vec4(length > 1e-6f ? axis / length : glm::vec3(0.0f), 0.0f);
		}
		rotationOnly[3] = glm::vec4(position, 1.0f);
		gizmoMatrix = rotationOnly;
	}

	// Objeto detras de la camara (perspectiva): no hay gizmo que dibujar (ImGuizmo dejaria un
	// clip rect sin cerrar en el draw list si llegara a ese caso)
	if (!camera->IsOrthographic() && !dragging)
	{
		const glm::vec4 originClip = projection * view * glm::vec4(glm::vec3(gizmoMatrix[3]), 1.0f);
		if (originClip.w < 0.001f) return;
	}

	const bool localSpace = (gizmoSpace == GizmoSpace::Local) || (currentTool == Tool::Scale);
	const ImGuizmo::MODE mode = localSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
	const float snapValues[3] = { snapStep, snapStep, snapStep };
	const bool snapping = ImGui::GetIO().KeyCtrl;

	const glm::mat4 matrixBeforeManipulate = gizmoMatrix;
	const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, mode,
		glm::value_ptr(gizmoMatrix), nullptr, snapping ? snapValues : nullptr);

	gizmoInUse = ImGuizmo::IsUsing();
	gizmoHovered = ImGuizmo::IsOver();

	// ----- Inicio / fin del arrastre: entrada de Undo y matrices de partida -----
	if (gizmoInUse && !gizmoWasInUse)
	{
		gizmoWasInUse = true;
		gizmoStartWorld.clear();
		for (GameObject* object : objects) gizmoStartWorld.emplace_back(static_cast<uint64_t>(object->GetUUID()), object->GetWorldMatrix());
		// ImGuizmo puede haber aplicado ya el primer movimiento: el punto de partida es la matriz
		// reconstruida antes de llamar a Manipulate, que es la que coincide con los objetos
		gizmoStartMatrix = matrixBeforeManipulate;
		const char* names[] = { "View", "Move", "Rotate", "Scale", "Transform" };
		undo.BeginTransform(objects, names[static_cast<int>(currentTool)]);
	}
	else if (!gizmoInUse && gizmoWasInUse)
	{
		finishDrag();
		return;
	}

	if (!changed || !gizmoInUse) return;

	// ---------------------------------------------------------------------------------
	// Aplicar: delta = gizmo actual * inversa(gizmo al empezar). Cada objeto pasa a
	// delta * (su matriz de mundo al empezar), convertida a local respecto a su padre.
	// Solo se escriben los canales que puede cambiar la herramienta (descomponer y recomponer
	// matrices acumula error: asi los demas valores no "bailan" en el Inspector).
	// ---------------------------------------------------------------------------------
	const glm::mat4 delta = gizmoMatrix * glm::inverse(gizmoStartMatrix);
	const bool movesAroundPivot = (objects.size() > 1) || (pivotMode == PivotMode::Center);
	for (const auto& [uid, startWorld] : gizmoStartWorld)
	{
		GameObject* object = scene->FindByUID(uid);
		TransformComponent* transform = object ? object->GetComponent<TransformComponent>() : nullptr;
		if (transform == nullptr) continue;

		const GameObject* parent = object->GetParent();
		const glm::mat4 parentWorld = parent ? parent->GetWorldMatrix() : glm::mat4(1.0f);
		const glm::mat4 local = glm::inverse(parentWorld) * (delta * startWorld);

		glm::vec3 position(0.0f), rotation(0.0f), scale(1.0f);
		if (!transform->DecomposeLocalMatrix(local, position, rotation, scale)) continue;

		switch (currentTool)
		{
		case Tool::Move:
			transform->SetPosition(position);
			break;
		case Tool::Rotate:
			transform->SetRotation(rotation);
			if (movesAroundPivot) transform->SetPosition(position);
			break;
		case Tool::Scale:
			transform->SetScale(scale);
			if (movesAroundPivot) transform->SetPosition(position);
			break;
		case Tool::Transform:
			transform->SetPosition(position);
			transform->SetRotation(rotation);
			transform->SetScale(scale);
			break;
		default:
			break;
		}
	}
}

// -------------------------------------------------------------------------------------
// Seleccion con el raton: clic (rayo contra la escena) o rectangulo arrastrando
// -------------------------------------------------------------------------------------
void ModuleUI::HandleSceneSelection(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera, bool imageHovered)
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;
	const ImGuiIO& io = ImGui::GetIO();

	// Empieza un clic de seleccion: no es del gizmo, ni de la camara (Alt / Hand), ni de un overlay
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && imageHovered && !gizmoHovered && !gizmoInUse && !io.KeyAlt &&
		currentTool != Tool::View && !camera->IsControllingCamera())
	{
		marqueePending = true;
		marqueeActive = false;
		marqueeStartX = io.MousePos.x;
		marqueeStartY = io.MousePos.y;
		marqueeAdditive = io.KeyCtrl || io.KeyShift;
		marqueeBaseSelection = scene->GetSelectionUIDs();
	}

	if (!marqueePending) return;

	if (!marqueeActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) marqueeActive = true;

	if (marqueeActive)
	{
		// Rectangulo de seleccion (en vivo, como en Unity)
		const ImVec2 a(std::min(marqueeStartX, io.MousePos.x), std::min(marqueeStartY, io.MousePos.y));
		const ImVec2 b(std::max(marqueeStartX, io.MousePos.x), std::max(marqueeStartY, io.MousePos.y));
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(a, b, IM_COL32(58, 121, 187, 50));
		drawList->AddRect(a, b, IM_COL32(120, 170, 230, 220));

		const float aspect = imageSize.x / imageSize.y;
		const glm::mat4 viewProjection = camera->GetProjectionMatrix(aspect) * camera->GetViewMatrix();

		std::vector<GameObject*> inside;
		const auto testBox = [&](GameObject* object, const AABB& box)
		{
			if (!box.IsValid()) return;
			ImVec2 minScreen(FLT_MAX, FLT_MAX), maxScreen(-FLT_MAX, -FLT_MAX);
			for (const glm::vec3& corner : box.GetCorners())
			{
				ImVec2 s;
				const glm::vec4 clip = viewProjection * glm::vec4(corner, 1.0f);
				if (clip.w <= 1e-5f) return; // parte detras de la camara
				WorldToScreen(corner, viewProjection, imagePos, imageSize, s);
				minScreen.x = std::min(minScreen.x, s.x); minScreen.y = std::min(minScreen.y, s.y);
				maxScreen.x = std::max(maxScreen.x, s.x); maxScreen.y = std::max(maxScreen.y, s.y);
			}
			if (maxScreen.x >= a.x && minScreen.x <= b.x && maxScreen.y >= a.y && minScreen.y <= b.y) inside.push_back(object);
		};

		std::vector<GameObject*> renderables;
		scene->CollectRenderables(renderables);
		for (GameObject* object : renderables) testBox(object, object->GetWorldAABB(false));
		ForEachObject(scene->GetRoot(), [&](GameObject* object)
		{
			if (object->IsActiveInHierarchy() && object->GetComponent<CameraComponent>() && !object->GetComponent<MeshComponent>())
			{
				const glm::vec3 p = glm::vec3(object->GetWorldMatrix()[3]);
				testBox(object, AABB(p - glm::vec3(0.2f), p + glm::vec3(0.2f)));
			}
		});

		std::vector<GameObject*> result;
		if (marqueeAdditive)
		{
			for (uint64_t uid : marqueeBaseSelection)
			{
				if (GameObject* object = scene->FindByUID(uid)) result.push_back(object);
			}
		}
		result.insert(result.end(), inside.begin(), inside.end());
		scene->SetSelection(result, inside.empty() ? nullptr : inside.back());
		inspectorShowsAsset = false;
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		if (!marqueeActive)
		{
			// Clic simple: icono de camara o el objeto mas cercano que atraviesa el rayo
			GameObject* picked = hoveredSceneIcon;
			if (picked == nullptr)
			{
				const ImVec2 local(marqueeStartX - imagePos.x, marqueeStartY - imagePos.y);
				picked = camera->PickAt(local.x, local.y, imageSize.x, imageSize.y);
			}
			if (picked != nullptr || !(io.KeyCtrl || io.KeyShift)) ClickSelect(picked);
		}
		marqueePending = false;
		marqueeActive = false;
		marqueeBaseSelection.clear();
	}
}

// -------------------------------------------------------------------------------------
// Soltar assets sobre la vista Scene
// -------------------------------------------------------------------------------------
void ModuleUI::HandleSceneDragDrop(const ImVec2& imagePos, const ImVec2& imageSize, CameraComponent* camera)
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr || !ImGui::BeginDragDropTarget()) return;

	const ImVec2 mousePos = ImGui::GetMousePos();
	const Ray ray = camera->ScreenPointToRay(mousePos.x - imagePos.x, mousePos.y - imagePos.y, imageSize.x, imageSize.y);
	float hitDistance = 0.0f;

	// Punto donde colocar lo soltado: sobre el objeto que hay debajo, o sobre el suelo (y = 0), o delante
	const auto dropPoint = [&]() -> glm::vec3
	{
		if (scene->PickObject(ray, &hitDistance)) return ray.GetPoint(hitDistance);
		if (std::abs(ray.direction.y) > 1e-4f)
		{
			const float t = -ray.origin.y / ray.direction.y;
			if (t > 0.0f && t < 500.0f) return ray.GetPoint(t);
		}
		return ray.GetPoint(std::max(camera->GetOrbitDistance(), 5.0f));
	};

	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_MODEL))
	{
		const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(payload->Data, payload->DataSize);
		const glm::vec3 position = dropPoint();
		undo.RecordSnapshot("Instantiate Model");
		if (GameObject* instance = scene->InstantiateModel(asset.uid))
		{
			if (TransformComponent* transform = instance->GetComponent<TransformComponent>()) transform->SetPosition(position);
			SelectGameObject(instance);
		}
	}
	else if (const ImGuiPayload* meshPayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_MESH))
	{
		const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(meshPayload->Data, meshPayload->DataSize);
		const glm::vec3 position = dropPoint();
		undo.RecordSnapshot("Create Mesh Object");
		GameObject* created = scene->CreateEmptyGameObject();
		const Resource* meshResource = Engine::GetInstance().resources ? Engine::GetInstance().resources->GetResource(asset.uid) : nullptr;
		if (meshResource) created->SetName(scene->MakeUniqueName(meshResource->GetName()));
		created->AddComponent(std::make_unique<MeshComponent>(created, asset.uid));
		created->AddComponent(std::make_unique<MaterialComponent>(created));
		if (TransformComponent* transform = created->GetComponent<TransformComponent>()) transform->SetPosition(position);
		SelectGameObject(created);
	}
	else if (const ImGuiPayload* texturePayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_TEXTURE))
	{
		const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(texturePayload->Data, texturePayload->DataSize);
		if (GameObject* hit = scene->PickObject(ray, &hitDistance))
		{
			undo.RecordSnapshot("Assign Texture");
			ImporterManager::AssignTextureToHierarchy(asset.uid, hit);
			SelectGameObject(hit);
		}
		else
		{
			LOG_WARNING("[UI] Drop the texture on top of an object with a mesh");
		}
	}
	else if (const ImGuiPayload* scenePayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_SCENE))
	{
		const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(scenePayload->Data, scenePayload->DataSize);
		scene->RequestLoadScene(asset.path);
	}
	ImGui::EndDragDropTarget();
}

// =====================================================================================
// VENTANA GAME
// =====================================================================================
void ModuleUI::DrawGameImage(unsigned int texture, const ImVec2& regionPos, const ImVec2& regionSize, bool updateRenderSize)
{
	Render* render = Engine::GetInstance().render.get();
	if (render == nullptr) return;

	// Relacion de aspecto elegida: la imagen se centra con bandas (letterbox), como Unity
	static const float aspects[] = { 0.0f, 16.0f / 9.0f, 16.0f / 10.0f, 4.0f / 3.0f, 5.0f / 4.0f };
	const float targetAspect = aspects[std::clamp(gameAspectIndex, 0, 4)];
	ImVec2 size = regionSize;
	if (targetAspect > 0.0f)
	{
		if (size.x / size.y > targetAspect) size.x = size.y * targetAspect;
		else size.y = size.x / targetAspect;
	}
	size.x = std::max(std::floor(size.x), 1.0f);
	size.y = std::max(std::floor(size.y), 1.0f);
	const ImVec2 pos(regionPos.x + std::floor((regionSize.x - size.x) * 0.5f), regionPos.y + std::floor((regionSize.y - size.y) * 0.5f));

	if (updateRenderSize) render->SetGameViewportSize(static_cast<int>(size.x), static_cast<int>(size.y));

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(regionPos, ImVec2(regionPos.x + regionSize.x, regionPos.y + regionSize.y), IM_COL32(20, 20, 20, 255));
	if (texture != 0)
	{
		// Escala (zoom): se recorta el centro de la imagen
		const float s = 0.5f / std::max(gameScale, 1.0f);
		drawList->AddImage((ImTextureID)(intptr_t)texture, pos, ImVec2(pos.x + size.x, pos.y + size.y), ImVec2(0.5f - s, 0.5f + s), ImVec2(0.5f + s, 0.5f - s));
	}

	// Estadisticas de Unity ("Stats")
	if (gameShowStats)
	{
		const RenderStats& stats = render->GetStats();
		const auto& perf = Engine::GetInstance().GetPerformanceData();
		char lines[256];
		snprintf(lines, sizeof(lines), "Statistics\nFPS: %.0f (%.2f ms)\nResolution: %d x %d\nDrawn: %d / %d %s",
			perf.currentFps, perf.currentMs, render->GetGameViewportWidth(), render->GetGameViewportHeight(),
			stats.gameDrawn, stats.gameCandidates, stats.gameCulling ? "(frustum culling)" : "");
		const ImVec2 textSize = ImGui::CalcTextSize(lines);
		const ImVec2 textPos(pos.x + size.x - textSize.x - 16.0f, pos.y + 12.0f);
		drawList->AddRectFilled(ImVec2(textPos.x - 8.0f, textPos.y - 6.0f), ImVec2(textPos.x + textSize.x + 8.0f, textPos.y + textSize.y + 6.0f), IM_COL32(40, 40, 40, 220), 4.0f);
		drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), lines);
	}
}

void ModuleUI::DrawGamePanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	Render* render = Engine::GetInstance().render.get();
	Scene* scene = Engine::GetInstance().scene.get();
	const bool maximized = maximizeOnPlay && scene && scene->IsInPlayMode();

	if (!showGame)
	{
		if (render && !maximized) render->SetGameViewportVisible(false);
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool visible = ImGui::Begin(EditorWindows::GAME, &showGame, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	if (!visible || render == nullptr)
	{
		if (render && !maximized) render->SetGameViewportVisible(false);
		ImGui::End();
		return;
	}
	render->SetGameViewportVisible(true);

	// ----------------------------- Barra superior -----------------------------
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorPalette::PANEL_TOOLBAR);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
	ImGui::BeginChild("GameToolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + 4.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	{
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::BeginCombo("##Display", "Display 1"))
		{
			ImGui::Selectable("Display 1", true);
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		static const char* aspectNames[] = { "Free Aspect", "16:9 Aspect", "16:10 Aspect", "4:3 Aspect", "5:4 Aspect" };
		ImGui::SetNextItemWidth(120.0f);
		ImGui::Combo("##Aspect", &gameAspectIndex, aspectNames, IM_ARRAYSIZE(aspectNames));
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Scale");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::SliderFloat("##Scale", &gameScale, 1.0f, 5.0f, "%.1fx");

		const float rightWidth = ImGui::CalcTextSize("Maximize On Play").x + ImGui::CalcTextSize("Stats").x + 40.0f;
		ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowWidth() - rightWidth));
		if (FlatButton("Maximize On Play", ImVec2(0, 0), maximizeOnPlay)) maximizeOnPlay = !maximizeOnPlay;
		ImGui::SameLine();
		if (FlatButton("Stats", ImVec2(0, 0), gameShowStats)) gameShowStats = !gameShowStats;
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();

	// ----------------------------- Imagen -----------------------------
	const ImVec2 regionPos = ImGui::GetCursorScreenPos();
	ImVec2 regionSize = ImGui::GetContentRegionAvail();
	regionSize.x = std::max(regionSize.x, 1.0f);
	regionSize.y = std::max(regionSize.y, 1.0f);
	ImGui::Dummy(regionSize);

	GameObject* gameCam = scene ? scene->GetGameCameraObject() : nullptr;
	const CameraComponent* gameCamComponent = gameCam ? gameCam->GetComponent<CameraComponent>() : nullptr;
	if (!gameCam || !gameCam->IsActiveInHierarchy() || !gameCamComponent || !gameCamComponent->IsActive())
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(regionPos, ImVec2(regionPos.x + regionSize.x, regionPos.y + regionSize.y), IM_COL32(0, 0, 0, 255));
		const char* text = "Display 1\nNo cameras rendering";
		const ImVec2 textSize = ImGui::CalcTextSize(text);
		drawList->AddText(ImVec2(regionPos.x + (regionSize.x - textSize.x) * 0.5f, regionPos.y + (regionSize.y - textSize.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text), text);
	}
	else
	{
		DrawGameImage(render->GetGameTexture(), regionPos, regionSize, !maximized);
	}

	ImGui::End();
}

void ModuleUI::DrawMaximizedGameWindow()
{
	Scene* scene = Engine::GetInstance().scene.get();
	Render* render = Engine::GetInstance().render.get();
	if (!maximizeOnPlay || scene == nullptr || render == nullptr || !scene->IsInPlayMode()) return;

	// "Maximize On Play": la vista Game ocupa todo el editor mientras se juega
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoSavedSettings;
	if (ImGui::Begin("##MaximizedGame", nullptr, flags))
	{
		render->SetGameViewportVisible(true);
		const ImVec2 regionPos = ImGui::GetCursorScreenPos();
		const ImVec2 regionSize = ImGui::GetContentRegionAvail();
		ImGui::Dummy(regionSize);
		DrawGameImage(render->GetGameTexture(), regionPos, regionSize, true);
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}
