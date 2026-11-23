#include "EditorTheme.h"
#include "EditorIcons.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace
{
	// Colores del tema sin tinte (para poder quitar el tinte del modo Play)
	ImVec4 baseColors[ImGuiCol_COUNT];
	bool tintApplied = false;

	ImVec4 Hex(unsigned int rgb, float alpha = 1.0f)
	{
		return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
	}

	ImVec4 WithAlpha(const ImVec4& color, float alpha)
	{
		return ImVec4(color.x, color.y, color.z, alpha);
	}
}

namespace EditorTheme
{
	void ApplyUnityDark()
	{
		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();
		ImGui::StyleColorsDark(&style);

		// ---------------------------------------------------------------------------------
		// Geometria: compacta y casi sin redondeos, como el editor de Unity
		// ---------------------------------------------------------------------------------
		style.WindowPadding = ImVec2(6.0f, 6.0f);
		style.FramePadding = ImVec2(6.0f, 3.0f);
		style.CellPadding = ImVec2(4.0f, 2.0f);
		style.ItemSpacing = ImVec2(6.0f, 4.0f);
		style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
		style.IndentSpacing = 14.0f;
		style.ScrollbarSize = 12.0f;
		style.GrabMinSize = 10.0f;

		style.WindowRounding = 0.0f;
		style.ChildRounding = 0.0f;
		style.FrameRounding = 3.0f;
		style.PopupRounding = 0.0f;
		style.ScrollbarRounding = 6.0f;
		style.GrabRounding = 2.0f;
		style.TabRounding = 3.0f;

		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupBorderSize = 1.0f;
		style.FrameBorderSize = 1.0f;     // los campos de Unity tienen un borde oscuro
		style.TabBorderSize = 0.0f;
		style.TabBarBorderSize = 1.0f;
		style.TabBarOverlineSize = 0.0f;  // Unity no marca la pestana activa con una linea
		style.DockingSeparatorSize = 2.0f;
		style.SeparatorTextBorderSize = 1.0f;
		style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
		style.WindowMenuButtonPosition = ImGuiDir_None;
		style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesNone;
		style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
		// Las pestanas solo ensenan la X al pasar el raton (Unity cierra pestanas desde su menu)
		style.TabCloseButtonMinWidthSelected = 0.0f;
		style.TabCloseButtonMinWidthUnselected = 0.0f;

		// ---------------------------------------------------------------------------------
		// Paleta de Unity (Pro skin)
		// ---------------------------------------------------------------------------------
		const ImVec4 panel      = Hex(0x383838); // fondo de las ventanas
		const ImVec4 tabStrip   = Hex(0x282828); // barra de pestanas / titulo
		const ImVec4 deepest    = Hex(0x191919); // separadores entre paneles, barra principal
		const ImVec4 field      = Hex(0x2A2A2A); // campos de texto / numeros
		const ImVec4 fieldHover = Hex(0x323232);
		const ImVec4 border     = Hex(0x1A1A1A);
		const ImVec4 button     = Hex(0x585858);
		const ImVec4 buttonHov  = Hex(0x676767);
		const ImVec4 buttonAct  = Hex(0x6F6F6F);
		const ImVec4 hover      = Hex(0x454545);
		const ImVec4 selection  = Hex(0x2C5D87);
		const ImVec4 text       = Hex(0xD2D2D2);
		const ImVec4 textDim    = Hex(0x808080);
		const ImVec4& accent    = EditorPalette::ACCENT;

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_Text] = text;
		colors[ImGuiCol_TextDisabled] = textDim;
		colors[ImGuiCol_TextLink] = Hex(0x4C9BE8);
		colors[ImGuiCol_TextSelectedBg] = WithAlpha(accent, 0.55f);
		colors[ImGuiCol_InputTextCursor] = text;

		colors[ImGuiCol_WindowBg] = panel;
		colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_PopupBg] = Hex(0x2D2D2D);
		colors[ImGuiCol_Border] = border;
		colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_MenuBarBg] = tabStrip;
		colors[ImGuiCol_TitleBg] = tabStrip;
		colors[ImGuiCol_TitleBgActive] = tabStrip;
		colors[ImGuiCol_TitleBgCollapsed] = tabStrip;
		colors[ImGuiCol_DockingEmptyBg] = deepest;
		colors[ImGuiCol_DockingPreview] = WithAlpha(accent, 0.55f);

		colors[ImGuiCol_FrameBg] = field;
		colors[ImGuiCol_FrameBgHovered] = fieldHover;
		colors[ImGuiCol_FrameBgActive] = field;
		colors[ImGuiCol_CheckMark] = Hex(0xE0E0E0);
		colors[ImGuiCol_SliderGrab] = Hex(0x999999);
		colors[ImGuiCol_SliderGrabActive] = Hex(0xBBBBBB);

		colors[ImGuiCol_Button] = button;
		colors[ImGuiCol_ButtonHovered] = buttonHov;
		colors[ImGuiCol_ButtonActive] = buttonAct;

		// Header = filas seleccionadas (Hierarchy, Project) y elementos de menu bajo el raton
		colors[ImGuiCol_Header] = selection;
		colors[ImGuiCol_HeaderHovered] = hover;
		colors[ImGuiCol_HeaderActive] = selection;

		// Pestanas: la activa tiene el color del panel, las demas el de la barra
		colors[ImGuiCol_Tab] = tabStrip;
		colors[ImGuiCol_TabHovered] = Hex(0x303030);
		colors[ImGuiCol_TabSelected] = Hex(0x3C3C3C);
		colors[ImGuiCol_TabSelectedOverline] = accent;
		colors[ImGuiCol_TabDimmed] = tabStrip;
		colors[ImGuiCol_TabDimmedSelected] = Hex(0x3C3C3C);
		colors[ImGuiCol_TabDimmedSelectedOverline] = Hex(0x3C3C3C);

		colors[ImGuiCol_Separator] = Hex(0x232323);
		colors[ImGuiCol_SeparatorHovered] = WithAlpha(accent, 0.80f);
		colors[ImGuiCol_SeparatorActive] = accent;
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_ResizeGripHovered] = WithAlpha(accent, 0.60f);
		colors[ImGuiCol_ResizeGripActive] = accent;
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.10f);
		colors[ImGuiCol_ScrollbarGrab] = Hex(0x5F5F5F);
		colors[ImGuiCol_ScrollbarGrabHovered] = Hex(0x707070);
		colors[ImGuiCol_ScrollbarGrabActive] = Hex(0x808080);
		colors[ImGuiCol_TreeLines] = Hex(0x555555);

		colors[ImGuiCol_TableHeaderBg] = Hex(0x3E3E3E);
		colors[ImGuiCol_TableBorderStrong] = border;
		colors[ImGuiCol_TableBorderLight] = Hex(0x2E2E2E);
		colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);
		colors[ImGuiCol_PlotLines] = Hex(0x9A9A9A);
		colors[ImGuiCol_PlotLinesHovered] = accent;
		colors[ImGuiCol_PlotHistogram] = accent;
		colors[ImGuiCol_PlotHistogramHovered] = Hex(0x4C9BE8);

		colors[ImGuiCol_DragDropTarget] = Hex(0x4C9BE8);
		colors[ImGuiCol_NavCursor] = accent;
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);

		// Con multi-viewport una ventana fuera de la principal es una ventana del sistema: opaca
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			colors[ImGuiCol_WindowBg].w = 1.0f;
			colors[ImGuiCol_PopupBg].w = 1.0f;
		}

		for (int i = 0; i < ImGuiCol_COUNT; ++i) baseColors[i] = colors[i];
		tintApplied = false;

		// ---------------------------------------------------------------------------------
		// Gizmo 3D (ImGuizmo) con los colores de los "handles" de Unity
		// ---------------------------------------------------------------------------------
		ImGuizmo::Style& gizmoStyle = ImGuizmo::GetStyle();
		const ImVec4 axisX = ImVec4(0.86f, 0.24f, 0.11f, 1.00f);
		const ImVec4 axisY = ImVec4(0.60f, 0.86f, 0.18f, 1.00f);
		const ImVec4 axisZ = ImVec4(0.23f, 0.48f, 0.97f, 1.00f);
		gizmoStyle.Colors[ImGuizmo::DIRECTION_X] = axisX;
		gizmoStyle.Colors[ImGuizmo::DIRECTION_Y] = axisY;
		gizmoStyle.Colors[ImGuizmo::DIRECTION_Z] = axisZ;
		gizmoStyle.Colors[ImGuizmo::PLANE_X] = WithAlpha(axisX, 0.45f);
		gizmoStyle.Colors[ImGuizmo::PLANE_Y] = WithAlpha(axisY, 0.45f);
		gizmoStyle.Colors[ImGuizmo::PLANE_Z] = WithAlpha(axisZ, 0.45f);
		gizmoStyle.Colors[ImGuizmo::SELECTION] = ImVec4(1.00f, 0.92f, 0.02f, 1.00f);
		gizmoStyle.TranslationLineThickness = 3.0f;
		gizmoStyle.TranslationLineArrowSize = 7.0f;
		gizmoStyle.RotationLineThickness = 2.5f;
		gizmoStyle.RotationOuterLineThickness = 2.5f;
		gizmoStyle.ScaleLineThickness = 3.0f;
		gizmoStyle.ScaleLineCircleSize = 6.0f;
		gizmoStyle.CenterCircleSize = 6.0f;

		// Los ejes apuntan siempre en su sentido positivo (como en Unity)
		ImGuizmo::AllowAxisFlip(false);
	}

	void SetPlayModeTint(bool enabled)
	{
		if (enabled == tintApplied) return;
		tintApplied = enabled;

		// Unity multiplica todo el editor por un color al entrar en Play: aqui un gris azulado
		const ImVec4 tint(0.80f, 0.86f, 1.00f, 1.0f);
		ImVec4* colors = ImGui::GetStyle().Colors;
		for (int i = 0; i < ImGuiCol_COUNT; ++i)
		{
			const ImVec4& base = baseColors[i];
			colors[i] = enabled ? ImVec4(base.x * tint.x, base.y * tint.y, base.z * tint.z, base.w) : base;
		}
	}
}
