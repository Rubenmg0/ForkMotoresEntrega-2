// =====================================================================================
// Interfaz del PLAYER (build de un juego). No hay editor: la camara principal ocupa toda la
// ventana y ImGui solo se usa para el overlay de las Development Builds (como el aviso
// "Development Build" de Unity) y el mensaje "No cameras rendering".
//
// Teclas:  Esc          salir del juego
//          F11 / Alt+Enter   pantalla completa
//          F1           (Development Build) mostrar / ocultar estadisticas
// =====================================================================================
#include "ModuleUI.h"
#include "EditorTheme.h"
#include "../Engine.h"
#include "../Window/Window.h"
#include "../Render/Render.h"
#include "../Scene/Scene.h"
#include "../Build/BuildManifest.h"
#include "../../Utils/Log.h"
#include "Version.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <SDL3/SDL.h>

bool ModuleUI::StartPlayerUI()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // un juego no guarda layout de ImGui
	io.Fonts->AddFontDefault();
	EditorTheme::ApplyUnityDark();

	ImGui_ImplSDL3_InitForOpenGL(Engine::GetInstance().window->GetWindow(), Engine::GetInstance().window->GetGLContext());
	ImGui_ImplOpenGL3_Init("#version 130");

	const BuildManifest& build = Runtime::GetBuild();
	showPlayerStats = build.development;
	LOG("[Player] %s %s (%s build, %s %s)", build.productName.c_str(), build.version.c_str(),
		build.development ? "development" : "release", ENGINE_NAME, build.engineVersion.c_str());
	return true;
}

bool ModuleUI::UpdatePlayerUI()
{
	Engine& engine = Engine::GetInstance();
	Window* window = engine.window.get();
	Render* render = engine.render.get();
	Scene* scene = engine.scene.get();
	const BuildManifest& build = Runtime::GetBuild();

	// La vista de juego ocupa toda la ventana (Render la copia a pantalla en Update)
	if (render && window)
	{
		render->SetViewportVisible(false);
		render->SetGameViewportVisible(true);
		render->SetGameViewportSize(window->GetWidth(), window->GetHeight());
	}

	// ------------------------------- Teclas -------------------------------
	const ImGuiIO& io = ImGui::GetIO();
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) return false;
	if (window && (ImGui::IsKeyPressed(ImGuiKey_F11, false) || (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Enter, false))))
	{
		window->SetFullscreen(!window->IsFullscreen());
	}
	if (build.development && ImGui::IsKeyPressed(ImGuiKey_F1, false)) showPlayerStats = !showPlayerStats;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImDrawList* drawList = ImGui::GetForegroundDrawList();

	// ------------------------------- Sin camara -------------------------------
	GameObject* camera = scene ? scene->GetCameraForDisplay(render ? render->GetGameDisplay() : 0) : nullptr;
	if (camera == nullptr)
	{
		const char* message = "No cameras rendering";
		const ImVec2 extent = ImGui::CalcTextSize(message);
		const ImVec2 center = viewport->GetCenter();
		drawList->AddText(ImVec2(center.x - extent.x * 0.5f, center.y - extent.y * 0.5f), IM_COL32(220, 220, 220, 255), message);
	}

	// ------------------------------- Development Build -------------------------------
	if (build.development)
	{
		const char* watermark = "Development Build";
		const ImVec2 extent = ImGui::CalcTextSize(watermark);
		const ImVec2 pos(viewport->Pos.x + viewport->Size.x - extent.x - 12.0f, viewport->Pos.y + viewport->Size.y - extent.y - 10.0f);
		drawList->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 200), watermark);
		drawList->AddText(pos, IM_COL32(255, 255, 255, 220), watermark);

		if (showPlayerStats)
		{
			ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 10.0f, viewport->Pos.y + 10.0f));
			ImGui::SetNextWindowBgAlpha(0.55f);
			ImGui::Begin("##PlayerStats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
			ImGui::Text("%s %s", build.productName.c_str(), build.version.c_str());
			ImGui::Separator();
			ImGui::Text("%.0f FPS (%.2f ms)", io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
			if (window) ImGui::Text("%d x %d%s", window->GetWidth(), window->GetHeight(), window->IsFullscreen() ? " fullscreen" : "");
			if (render)
			{
				const RenderStats& stats = render->GetStats();
				ImGui::Text("Drawn %d / %d objects%s", stats.gameDrawn, stats.gameCandidates, stats.gameCulling ? " (culling)" : "");
			}
			if (scene) ImGui::Text("Scene: %s  t=%.1fs", FileSystem::GetFileNameNoExtension(scene->GetCurrentScenePath()).c_str(), scene->GetGameTime());
			ImGui::TextDisabled("%s %s | build %s", ENGINE_NAME, build.engineVersion.c_str(), build.buildId.c_str());
			ImGui::TextDisabled("F1 stats  F11 fullscreen  Esc quit");
			ImGui::End();
		}
	}
	return true;
}
