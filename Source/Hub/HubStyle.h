#pragma once

#include "../Engine/UI/EditorIcons.h"

#include <imgui.h>

// =====================================================================================
// Estilo del Hub: parte del tema oscuro del editor (EditorTheme::ApplyUnityDark) y lo suaviza
// un poco (esquinas mas redondeadas, mas aire) para que parezca un lanzador moderno.
// =====================================================================================
namespace HubStyle
{
	constexpr int SPLASH_WIDTH = 640;
	constexpr int SPLASH_HEIGHT = 380;
	constexpr int HUB_WIDTH = 1200;
	constexpr int HUB_HEIGHT = 720;
	constexpr int HUB_MIN_WIDTH = 940;
	constexpr int HUB_MIN_HEIGHT = 560;

	constexpr float SIDEBAR_WIDTH = 230.0f;
	constexpr float DETAILS_WIDTH = 320.0f;
	constexpr float ROW_HEIGHT = 58.0f;

	const ImVec4 BACKGROUND    = ImVec4(0.125f, 0.125f, 0.133f, 1.0f); // #202022
	const ImVec4 SIDEBAR_BG    = ImVec4(0.090f, 0.090f, 0.098f, 1.0f); // #171719
	const ImVec4 PANEL_BG      = ImVec4(0.157f, 0.157f, 0.165f, 1.0f); // #28282A
	const ImVec4 CARD_BG       = ImVec4(0.188f, 0.188f, 0.200f, 1.0f); // #303033
	const ImVec4 TEXT_MUTED    = ImVec4(0.60f, 0.62f, 0.66f, 1.0f);
	const ImVec4 TEXT_STRONG   = ImVec4(0.94f, 0.95f, 0.97f, 1.0f);
	const ImVec4 DANGER        = ImVec4(0.90f, 0.36f, 0.33f, 1.0f);
	const ImVec4 WARNING       = ImVec4(1.00f, 0.76f, 0.28f, 1.0f);
	const ImVec4 SUCCESS       = ImVec4(0.38f, 0.82f, 0.50f, 1.0f);

	inline void Apply()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 0.0f;
		style.ChildRounding = 6.0f;
		style.FrameRounding = 5.0f;
		style.PopupRounding = 6.0f;
		style.GrabRounding = 4.0f;
		style.TabRounding = 5.0f;
		style.FramePadding = ImVec2(10.0f, 6.0f);
		style.ItemSpacing = ImVec2(8.0f, 8.0f);
		style.WindowPadding = ImVec2(16.0f, 16.0f);
		style.WindowBorderSize = 0.0f;
		style.ChildBorderSize = 0.0f;
		style.PopupBorderSize = 1.0f;
		style.Colors[ImGuiCol_WindowBg] = BACKGROUND;
		style.Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
		style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
	}

	// Boton principal (azul de acento)
	inline bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0))
	{
		ImGui::PushStyleColor(ImGuiCol_Button, EditorPalette::ACCENT);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorPalette::ACCENT_HOVERED);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorPalette::ACCENT_ACTIVE);
		ImGui::PushStyleColor(ImGuiCol_Text, TEXT_STRONG);
		const bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor(4);
		return pressed;
	}
}
