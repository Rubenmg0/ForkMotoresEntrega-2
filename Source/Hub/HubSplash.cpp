// =====================================================================================
// Splash del Hub: ventana sin bordes con el logo animado, la version y una barra de carga.
// Mientras se anima se hace el trabajo real de arranque (leer hub.json y los project.json).
// =====================================================================================
#include "HubApp.h"
#include "HubStyle.h"
#include "../Engine/Project/HubRegistry.h"
#include "../Engine/UI/BrandLogo.h"
#include "Version.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace
{
	constexpr float SPLASH_DURATION = 2.4f;  // segundos
	constexpr float SPLASH_MIN_TIME = 0.6f;  // no se puede saltar antes (evita parpadeos)
	constexpr float FADE_IN = 0.45f;

	float EaseOutCubic(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		const float inv = 1.0f - t;
		return 1.0f - inv * inv * inv;
	}

	ImU32 ToU32(const ImVec4& color, float alpha)
	{
		return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
	}
}

void HubApp::DrawSplash()
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##Splash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs);
	ImGui::PopStyleVar();

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 origin = viewport->Pos;
	const ImVec2 size = viewport->Size;
	const ImVec2 end(origin.x + size.x, origin.y + size.y);
	const float elapsed = time;
	const float fade = EaseOutCubic(elapsed / FADE_IN);

	// Fondo: degradado azul noche + rejilla tenue en perspectiva
	drawList->AddRectFilledMultiColor(origin, end,
		IM_COL32(22, 30, 46, 255), IM_COL32(16, 20, 30, 255), IM_COL32(10, 12, 18, 255), IM_COL32(14, 22, 36, 255));
	const ImU32 gridColor = IM_COL32(58, 121, 187, static_cast<int>(18 * fade));
	const float horizon = origin.y + size.y * 0.70f;
	for (int i = -12; i <= 12; ++i)
	{
		const float x = origin.x + size.x * 0.5f + i * 26.0f;
		drawList->AddLine(ImVec2(origin.x + size.x * 0.5f + i * 6.0f, horizon), ImVec2(x + i * 40.0f, end.y), gridColor);
	}
	for (int i = 0; i < 6; ++i)
	{
		const float t = static_cast<float>(i) / 6.0f;
		const float y = horizon + (end.y - horizon) * t * t + 2.0f;
		drawList->AddLine(ImVec2(origin.x, y), ImVec2(end.x, y), gridColor);
	}
	// Linea de acento arriba
	drawList->AddRectFilled(origin, ImVec2(end.x, origin.y + 3.0f), ToU32(EditorPalette::ACCENT, fade));

	// Logo (entra con un pequeno zoom) + marca
	const float logoScale = 0.85f + 0.15f * fade;
	const float logoSize = 150.0f * logoScale;
	const ImVec2 logoCenter(origin.x + size.x * 0.5f, origin.y + 118.0f);
	BrandLogo::Draw(drawList, logoCenter, logoSize, elapsed, fade);

	const float baseSize = ImGui::GetStyle().FontSizeBase;
	const float titleSize = baseSize * 3.0f;
	ImFont* font = ImGui::GetFont();
	const ImVec2 titleExtent = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, "MARUJA");
	BrandLogo::DrawWordmark(drawList, ImVec2(logoCenter.x - titleExtent.x * 0.5f, origin.y + 200.0f), titleSize, fade);

	// Version
	char versionText[64];
	snprintf(versionText, sizeof(versionText), "Version %s", ENGINE_VERSION_STRING);
	const ImVec2 versionExtent = ImGui::CalcTextSize(versionText);
	drawList->AddText(ImVec2(logoCenter.x - versionExtent.x * 0.5f, origin.y + 272.0f), ToU32(HubStyle::TEXT_MUTED, fade), versionText);

	// Trabajo real de arranque, repartido en frames para que la animacion no se congele
	const char* status = "Starting...";
	if (splashStep == 0 && elapsed > 0.25f) { status = "Reading project list..."; HubRegistry::Load(); splashStep = 1; }
	else if (splashStep == 1 && elapsed > 0.7f) { status = "Scanning projects..."; RefreshProjects(); splashStep = 2; }
	else if (splashStep == 1) status = "Reading project list...";
	else if (splashStep == 2) status = (elapsed > 1.6f) ? "Ready" : "Preparing the Hub...";

	// Barra de progreso
	const float progress = std::clamp(elapsed / SPLASH_DURATION, 0.0f, 1.0f);
	const float barMargin = 48.0f;
	const float barY = end.y - 44.0f;
	const ImVec2 barStart(origin.x + barMargin, barY);
	const ImVec2 barEnd(end.x - barMargin, barY + 3.0f);
	drawList->AddRectFilled(barStart, barEnd, IM_COL32(255, 255, 255, static_cast<int>(22 * fade)), 2.0f);
	drawList->AddRectFilled(barStart, ImVec2(barStart.x + (barEnd.x - barStart.x) * EaseOutCubic(progress), barEnd.y),
		ToU32(EditorPalette::ACCENT_HOVERED, fade), 2.0f);

	drawList->AddText(ImVec2(barStart.x, barY - 22.0f), ToU32(HubStyle::TEXT_MUTED, fade), status);
	const char* copyright = "(c) 2026 Maruja Engine Team";
	const ImVec2 copyrightExtent = ImGui::CalcTextSize(copyright);
	drawList->AddText(ImVec2(barEnd.x - copyrightExtent.x, barY - 22.0f), ToU32(HubStyle::TEXT_MUTED, fade * 0.7f), copyright);
	drawList->AddText(ImVec2(barStart.x, barY + 12.0f), ToU32(HubStyle::TEXT_MUTED, fade * 0.45f), "Click to skip");

	ImGui::End();

	const bool done = (elapsed >= SPLASH_DURATION) || (splashSkipRequested && elapsed >= SPLASH_MIN_TIME);
	if (done && splashStep >= 2) FinishSplash();
	else if (done && splashStep < 2)
	{
		// Se pidio saltar antes de terminar la carga: se hace ya
		if (splashStep == 0) HubRegistry::Load();
		RefreshProjects();
		splashStep = 2;
		FinishSplash();
	}
}

void HubApp::FinishSplash()
{
	stage = Stage::Hub;
	stageStartTime = time;

	// La ventana pasa a ser una ventana normal del tamano del Hub
	SDL_SetWindowBordered(window, true);
	SDL_SetWindowResizable(window, true);
	SDL_SetWindowMinimumSize(window, HubStyle::HUB_MIN_WIDTH, HubStyle::HUB_MIN_HEIGHT);
	SDL_SetWindowSize(window, HubStyle::HUB_WIDTH, HubStyle::HUB_HEIGHT);
	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}
