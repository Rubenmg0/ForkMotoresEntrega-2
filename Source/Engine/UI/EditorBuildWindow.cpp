// =====================================================================================
// Ventana "Build Settings" del editor (File > Build Settings..., Ctrl+Shift+B), al estilo de
// Unity / Godot / Unreal:
//
//   Build            escenas incluidas (orden = indice, la 0 arranca), plataforma, Development
//                    Build, carpeta de salida, Build / Build And Run / Clean e informe de la build
//   Player Settings  nombre del producto, empresa, version, resolucion y modo de ventana
//   Releases         build Release empaquetada en .zip con SHA-256, notas, changelog, historial
//                    y subida automatica de la version del proyecto
//
// Los ajustes se guardan en ProjectSettings/BuildSettings.json (van al control de versiones).
// =====================================================================================
#include "ModuleUI.h"
#include "EditorIcons.h"
#include "EditorPlatform.h"
#include "../Engine.h"
#include "../Window/Window.h"
#include "../Scene/Scene.h"
#include "../Project/ProjectInfo.h"
#include "../Build/BuildPipeline.h"
#include "../Build/ReleaseManager.h"
#include "../../Utils/Log.h"
#include "Version.h"

#include <imgui.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <mutex>

namespace
{
	// Estado de la ventana (solo hay un editor por proceso)
	struct BuildWindowState
	{
		bool loaded = false;
		BuildSettings settings;
		char productName[128] = "";
		char companyName[128] = "";
		char outputDir[512] = "";
		char releaseNotes[2048] = "";

		bool hasReport = false;
		BuildReport report;
		bool hasRelease = false;
		ReleaseReport release;
		std::vector<ReleaseEntry> history;

		// Build pedida: se hace un par de frames despues para que se vea el aviso "Building..."
		enum class Job { None, Build, BuildAndRun, Release } job = Job::None;
		int jobDelay = 0;

		// Dialogo de carpeta (asincrono)
		std::mutex dialogMutex;
		bool dialogResultReady = false;
		std::string dialogResult;
	};

	BuildWindowState& State()
	{
		static BuildWindowState state;
		return state;
	}

	void CopyToBuffer(char* buffer, size_t size, const std::string& text)
	{
		if (size == 0) return;
		const size_t length = std::min(size - 1, text.size());
		std::memcpy(buffer, text.data(), length);
		buffer[length] = '\0';
	}

	void LoadState()
	{
		BuildWindowState& state = State();
		state.settings = BuildSettings::LoadOrCreate();
		CopyToBuffer(state.productName, sizeof(state.productName), state.settings.productName);
		CopyToBuffer(state.companyName, sizeof(state.companyName), state.settings.companyName);
		CopyToBuffer(state.outputDir, sizeof(state.outputDir), state.settings.GetOutputDir());
		state.history = ReleaseManager::LoadHistory();
		state.loaded = true;
	}

	void SaveSettings()
	{
		BuildWindowState& state = State();
		if (!state.settings.Save()) LOG_ERROR("[Build] Could not save %s", BuildSettings::FILE);
	}

	void FolderDialogCallback(void*, const char* const* filelist, int)
	{
		if (filelist == nullptr || filelist[0] == nullptr) return;
		BuildWindowState& state = State();
		std::lock_guard<std::mutex> lock(state.dialogMutex);
		state.dialogResult = filelist[0];
		state.dialogResultReady = true;
	}

	void StatusLine(bool success, const char* text)
	{
		ImGui::TextColored(success ? EditorPalette::STATE_PLAY : EditorPalette::LEVEL_ERROR, "%s  %s",
			success ? ICON_FA_CIRCLE_CHECK : ICON_FA_CIRCLE_XMARK, text);
	}

	void MessageList(const std::vector<std::string>& lines, const ImVec4& color, const char* icon)
	{
		for (const std::string& line : lines)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextWrapped("%s  %s", icon, line.c_str());
			ImGui::PopStyleColor();
		}
	}
}

// =====================================================================================
// PETICIONES DE BUILD (se ejecutan al principio de ModuleUI::Update)
// =====================================================================================
void ModuleUI::RequestBuild(bool runAfterBuild)
{
	BuildWindowState& state = State();
	if (!state.loaded) LoadState();
	state.job = runAfterBuild ? BuildWindowState::Job::BuildAndRun : BuildWindowState::Job::Build;
	state.jobDelay = 2;
}

void ModuleUI::ProcessPendingBuild()
{
	BuildWindowState& state = State();

	{
		std::lock_guard<std::mutex> lock(state.dialogMutex);
		if (state.dialogResultReady)
		{
			state.dialogResultReady = false;
			state.settings.outputDir = Project::NormalizeRoot(state.dialogResult);
			CopyToBuffer(state.outputDir, sizeof(state.outputDir), state.settings.outputDir);
			SaveSettings();
		}
	}

	if (state.job == BuildWindowState::Job::None) return;

	// Aviso "Building..." mientras se espera
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	drawList->AddRectFilled(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), IM_COL32(0, 0, 0, 140));
	const char* label = (state.job == BuildWindowState::Job::Release) ? ICON_FA_BOX_ARCHIVE "  Creating release..." : ICON_FA_HAMMER "  Building player...";
	const ImVec2 extent = ImGui::CalcTextSize(label);
	const ImVec2 center = viewport->GetCenter();
	drawList->AddRectFilled(ImVec2(center.x - extent.x * 0.5f - 24.0f, center.y - 22.0f), ImVec2(center.x + extent.x * 0.5f + 24.0f, center.y + 22.0f),
		IM_COL32(40, 40, 40, 255), 6.0f);
	drawList->AddText(ImVec2(center.x - extent.x * 0.5f, center.y - extent.y * 0.5f), IM_COL32(235, 235, 235, 255), label);

	if (--state.jobDelay > 0) return;

	Scene* scene = Engine::GetInstance().scene.get();
	if (scene && scene->IsInPlayMode())
	{
		LOG_WARNING("[Build] Stop Play mode before building");
		state.job = BuildWindowState::Job::None;
		return;
	}

	const BuildWindowState::Job job = state.job;
	state.job = BuildWindowState::Job::None;
	showBuildSettings = true;

	if (job == BuildWindowState::Job::Release)
	{
		state.release = ReleaseManager::CreateRelease(state.settings, state.releaseNotes, state.settings.bumpAfterRelease);
		state.hasRelease = true;
		state.report = state.release.build;
		state.hasReport = !state.release.build.steps.empty();
		state.history = ReleaseManager::LoadHistory();
		if (state.release.success)
		{
			state.releaseNotes[0] = '\0';
			autoWindowTitle.clear(); // la version del proyecto ha cambiado
		}
		return;
	}

	state.report = BuildPipeline::Build(state.settings, state.settings.development);
	state.hasReport = true;
	if (state.report.success && job == BuildWindowState::Job::BuildAndRun)
	{
		EditorPlatform::LaunchProcess(state.report.executablePath, {}, state.report.productDir);
	}
}

// =====================================================================================
// VENTANA
// =====================================================================================
void ModuleUI::DrawBuildSettingsPanel()
{
	if (!showBuildSettings) return;
	BuildWindowState& state = State();
	if (!state.loaded) LoadState();
	BuildSettings& settings = state.settings;

	ImGui::SetNextWindowSize(ImVec2(640, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(ICON_FA_HAMMER "  Build Settings###BuildSettings", &showBuildSettings))
	{
		ImGui::End();
		return;
	}

	Scene* scene = Engine::GetInstance().scene.get();
	const bool playing = scene && scene->IsInPlayMode();
	const bool busy = state.job != BuildWindowState::Job::None;

	if (ImGui::BeginTabBar("##BuildTabs"))
	{
		// ================================== BUILD ==================================
		if (ImGui::BeginTabItem(ICON_FA_HAMMER "  Build"))
		{
			ImGui::SeparatorText("Scenes In Build");
			ImGui::BeginChild("##ScenesInBuild", ImVec2(0, 150), ImGuiChildFlags_Borders);
			int buildIndex = 0;
			int moveFrom = -1, moveTo = -1, removeIndex = -1;
			for (int i = 0; i < static_cast<int>(settings.scenes.size()); ++i)
			{
				SceneInBuild& entry = settings.scenes[i];
				ImGui::PushID(i);
				if (ImGui::Checkbox("##enabled", &entry.enabled)) SaveSettings();
				ImGui::SameLine();
				const bool exists = FileSystem::Exists(entry.path);
				if (!exists) ImGui::PushStyleColor(ImGuiCol_Text, EditorPalette::LEVEL_ERROR);
				ImGui::Selectable(entry.path.c_str(), false, ImGuiSelectableFlags_AllowOverlap, ImVec2(ImGui::GetContentRegionAvail().x - 110.0f, 0));
				if (!exists) { ImGui::PopStyleColor(); ImGui::SetItemTooltip("Scene file not found"); }

				// Arrastrar para reordenar
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("BUILD_SCENE", &i, sizeof(int));
					ImGui::TextUnformatted(entry.path.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BUILD_SCENE"))
					{
						moveFrom = *static_cast<const int*>(payload->Data);
						moveTo = i;
					}
					ImGui::EndDragDropTarget();
				}

				ImGui::SameLine(ImGui::GetContentRegionMax().x - 104.0f);
				if (entry.enabled) ImGui::TextDisabled("%2d", buildIndex++);
				else ImGui::TextDisabled("  ");
				ImGui::SameLine();
				if (ImGui::SmallButton(ICON_FA_ARROW_UP) && i > 0) { moveFrom = i; moveTo = i - 1; }
				ImGui::SameLine();
				if (ImGui::SmallButton(ICON_FA_ARROW_DOWN) && i + 1 < static_cast<int>(settings.scenes.size())) { moveFrom = i; moveTo = i + 1; }
				ImGui::SameLine();
				if (ImGui::SmallButton(ICON_FA_XMARK)) removeIndex = i;
				ImGui::PopID();
			}
			if (settings.scenes.empty()) ImGui::TextDisabled("No scenes. Save a scene and press \"Add Open Scene\".");
			ImGui::EndChild();

			if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo)
			{
				SceneInBuild moved = settings.scenes[moveFrom];
				settings.scenes.erase(settings.scenes.begin() + moveFrom);
				settings.scenes.insert(settings.scenes.begin() + moveTo, moved);
				SaveSettings();
			}
			if (removeIndex >= 0)
			{
				settings.scenes.erase(settings.scenes.begin() + removeIndex);
				SaveSettings();
			}

			const std::string openScene = scene ? scene->GetCurrentScenePath() : std::string();
			const bool openInList = std::any_of(settings.scenes.begin(), settings.scenes.end(), [&](const SceneInBuild& s) { return s.path == openScene; });
			ImGui::BeginDisabled(openScene.empty() || openInList);
			if (ImGui::Button(ICON_FA_PLUS "  Add Open Scene"))
			{
				settings.scenes.push_back({ openScene, true });
				SaveSettings();
			}
			ImGui::EndDisabled();
			if (openScene.empty()) ImGui::SetItemTooltip("Save the current scene first");
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Add All Scenes"))
			{
				settings.AddMissingScenes();
				SaveSettings();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("Scene 0 is loaded at startup. Drag to reorder.");

			// ------------------------------- Plataforma y opciones -------------------------------
			ImGui::SeparatorText("Platform");
			ImGui::TextUnformatted(ICON_FA_DESKTOP "  Windows  x86_64");
			ImGui::SameLine();
			ImGui::TextDisabled("(standalone player, OpenGL 4.6)");
			if (ImGui::Checkbox("Development Build", &settings.development)) SaveSettings();
			ImGui::SetItemTooltip("Shows the stats overlay (F1) and the \"Development Build\" watermark");
			if (std::string(ENGINE_BUILD_CONFIG) == "Debug")
			{
				ImGui::TextColored(EditorPalette::LEVEL_WARNING, ICON_FA_TRIANGLE_EXCLAMATION "  The editor is a Debug build: games need Visual Studio's debug runtime.");
			}

			ImGui::TextUnformatted("Output folder");
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150.0f);
			ImGui::InputText("##OutputDir", state.outputDir, sizeof(state.outputDir));
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				settings.outputDir = Project::NormalizeRoot(state.outputDir);
				SaveSettings();
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Browse"))
			{
				SDL_ShowOpenFolderDialog(FolderDialogCallback, nullptr, Engine::GetInstance().window->GetWindow(), state.outputDir, false);
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "##OpenOutput"))
			{
				std::error_code ec;
				std::filesystem::create_directories(std::filesystem::u8path(settings.GetOutputDir()), ec);
				EditorPlatform::RevealInExplorer(settings.GetOutputDir());
			}
			ImGui::SetItemTooltip("Open the output folder");
			ImGui::TextDisabled("%s", (settings.GetProductDir() + "/" + settings.productName + ".exe").c_str());

			// ------------------------------- Botones -------------------------------
			ImGui::Spacing();
			ImGui::BeginDisabled(playing || busy);
			if (ImGui::Button(ICON_FA_BROOM "  Clean"))
			{
				std::string error;
				if (BuildPipeline::CleanBuild(settings, &error)) LOG("[Build] Removed '%s'", settings.GetProductDir().c_str());
				else LOG_ERROR("[Build] %s", error.c_str());
			}
			ImGui::SameLine(ImGui::GetContentRegionMax().x - 250.0f);
			if (ImGui::Button(ICON_FA_HAMMER "  Build", ImVec2(110.0f, 0.0f))) RequestBuild(false);
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, EditorPalette::ACCENT);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorPalette::ACCENT_HOVERED);
			if (ImGui::Button(ICON_FA_PLAY "  Build And Run", ImVec2(130.0f, 0.0f))) RequestBuild(true);
			ImGui::PopStyleColor(2);
			ImGui::EndDisabled();
			if (playing) ImGui::TextColored(EditorPalette::LEVEL_WARNING, "Stop Play mode to build.");

			// ------------------------------- Informe -------------------------------
			if (state.hasReport)
			{
				const BuildReport& report = state.report;
				ImGui::SeparatorText("Last build");
				char summary[256];
				snprintf(summary, sizeof(summary), "%s %s  %s  -  %d files, %s, %.2f s", report.productName.c_str(), report.version.c_str(),
					report.development ? "[Development]" : "[Release]", report.fileCount, BuildPipeline::FormatBytes(report.totalBytes).c_str(), report.seconds);
				StatusLine(report.success, summary);
				ImGui::TextDisabled("%d scene(s), %d resource(s), build %s, %s", report.sceneCount, report.resourceCount, report.buildId.c_str(),
					Project::FormatDate(report.timestamp).c_str());
				if (report.success)
				{
					if (ImGui::SmallButton(ICON_FA_PLAY "  Run")) EditorPlatform::LaunchProcess(report.executablePath, {}, report.productDir);
					ImGui::SameLine();
					if (ImGui::SmallButton(ICON_FA_FOLDER_OPEN "  Show in Explorer")) EditorPlatform::RevealInExplorer(report.executablePath);
					ImGui::SameLine();
				}
				if (!report.logPath.empty() && ImGui::SmallButton(ICON_FA_FILE_LINES "  Build log")) EditorPlatform::RevealInExplorer(report.logPath);
				MessageList(report.errors, EditorPalette::LEVEL_ERROR, ICON_FA_CIRCLE_XMARK);
				MessageList(report.warnings, EditorPalette::LEVEL_WARNING, ICON_FA_TRIANGLE_EXCLAMATION);
				if (ImGui::TreeNode("Steps"))
				{
					for (const std::string& step : report.steps) ImGui::TextUnformatted(step.c_str());
					ImGui::TreePop();
				}
			}
			ImGui::EndTabItem();
		}

		// ================================== PLAYER SETTINGS ==================================
		if (ImGui::BeginTabItem(ICON_FA_SLIDERS "  Player Settings"))
		{
			ImGui::SeparatorText("Product");
			ImGui::InputText("Product Name", state.productName, sizeof(state.productName));
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				std::string error;
				if (Project::IsValidProjectName(state.productName, &error))
				{
					settings.productName = state.productName;
					SaveSettings();
				}
				else
				{
					LOG_WARNING("[Build] Invalid product name: %s", error.c_str());
					CopyToBuffer(state.productName, sizeof(state.productName), settings.productName);
				}
			}
			ImGui::SetItemTooltip("Name of the executable, the window and the release archive");
			ImGui::InputText("Company Name", state.companyName, sizeof(state.companyName));
			if (ImGui::IsItemDeactivatedAfterEdit()) { settings.companyName = state.companyName; SaveSettings(); }
			const std::string version = Project::HasActive() ? Project::GetActive().version.ToString() : std::string("-");
			ImGui::LabelText("Version", "%s", version.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Edit...")) showProjectSettings = true;

			ImGui::SeparatorText("Resolution and Presentation");
			int resolution[2] = { settings.window.width, settings.window.height };
			if (ImGui::InputInt2("Default Size", resolution))
			{
				settings.window.width = std::clamp(resolution[0], 320, 7680);
				settings.window.height = std::clamp(resolution[1], 240, 4320);
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) SaveSettings();
			if (ImGui::Checkbox("Fullscreen", &settings.window.fullscreen)) SaveSettings();
			if (ImGui::Checkbox("Resizable Window", &settings.window.resizable)) SaveSettings();
			if (ImGui::Checkbox("VSync", &settings.window.vsync)) SaveSettings();
			ImGui::TextDisabled("In game: F11 / Alt+Enter toggles fullscreen, Esc quits.");
			ImGui::EndTabItem();
		}

		// ================================== RELEASES ==================================
		if (ImGui::BeginTabItem(ICON_FA_BOX_ARCHIVE "  Releases"))
		{
			const std::string version = Project::HasActive() ? Project::GetActive().version.ToString() : std::string("0.1.0");
			const bool alreadyReleased = std::any_of(state.history.begin(), state.history.end(), [&](const ReleaseEntry& e) { return e.version == version; });

			ImGui::SeparatorText("New release");
			ImGui::Text("%s", settings.productName.c_str());
			ImGui::SameLine();
			ImGui::TextColored(EditorPalette::ACCENT_HOVERED, "v%s", version.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("-> %s", ReleaseManager::GetArchiveName(settings.productName, version).c_str());

			ImGui::TextUnformatted("Release notes (one change per line)");
			ImGui::InputTextMultiline("##ReleaseNotes", state.releaseNotes, sizeof(state.releaseNotes), ImVec2(-1.0f, 90.0f));

			const char* bumps[] = { "Keep version", "Patch (x.y.Z+1)", "Minor (x.Y+1.0)", "Major (X+1.0.0)" };
			int bump = static_cast<int>(settings.bumpAfterRelease);
			ImGui::SetNextItemWidth(200.0f);
			if (ImGui::Combo("Version after release", &bump, bumps, IM_ARRAYSIZE(bumps)))
			{
				settings.bumpAfterRelease = static_cast<VersionBump>(bump);
				SaveSettings();
			}

			ImGui::TextDisabled("Release = Release build + .zip + SHA-256 + CHANGELOG.md + ReleaseHistory.json");
			if (alreadyReleased)
			{
				ImGui::TextColored(EditorPalette::LEVEL_WARNING, ICON_FA_TRIANGLE_EXCLAMATION "  v%s is already released. Bump the version in Project Settings.", version.c_str());
			}
			ImGui::BeginDisabled(playing || busy || alreadyReleased);
			ImGui::PushStyleColor(ImGuiCol_Button, EditorPalette::ACCENT);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorPalette::ACCENT_HOVERED);
			if (ImGui::Button(ICON_FA_BOX_ARCHIVE "  Create Release", ImVec2(180.0f, 0.0f)))
			{
				state.job = BuildWindowState::Job::Release;
				state.jobDelay = 2;
			}
			ImGui::PopStyleColor(2);
			ImGui::EndDisabled();
			if (ImGui::Button(ICON_FA_FILE_LINES "  Open CHANGELOG.md") && FileSystem::Exists(ReleaseManager::CHANGELOG_FILE))
			{
				EditorPlatform::RevealInExplorer(FileSystem::ToDiskPath(ReleaseManager::CHANGELOG_FILE));
			}

			if (state.hasRelease)
			{
				const ReleaseReport& release = state.release;
				if (release.success)
				{
					StatusLine(true, ("Released " + release.entry.version + " (" + BuildPipeline::FormatBytes(release.entry.size) + ")").c_str());
					if (!release.nextVersion.empty()) ImGui::TextDisabled("Project version is now %s", release.nextVersion.c_str());
				}
				else
				{
					StatusLine(false, "Release failed");
				}
				MessageList(release.errors, EditorPalette::LEVEL_ERROR, ICON_FA_CIRCLE_XMARK);
				MessageList(release.build.warnings, EditorPalette::LEVEL_WARNING, ICON_FA_TRIANGLE_EXCLAMATION);
			}

			// ------------------------------- Historial -------------------------------
			ImGui::SeparatorText("History");
			if (state.history.empty()) ImGui::TextDisabled("No releases yet.");
			else if (ImGui::BeginTable("##Releases", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
				ImVec2(0, ImGui::GetContentRegionAvail().y)))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Version");
				ImGui::TableSetupColumn("Date");
				ImGui::TableSetupColumn("Size");
				ImGui::TableSetupColumn("SHA-256", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("");
				ImGui::TableHeadersRow();
				for (size_t i = 0; i < state.history.size(); ++i)
				{
					const ReleaseEntry& entry = state.history[i];
					ImGui::PushID(static_cast<int>(i));
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextColored(EditorPalette::ACCENT_HOVERED, "v%s", entry.version.c_str());
					if (!entry.notes.empty()) ImGui::SetItemTooltip("%s", entry.notes.c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(Project::FormatDate(entry.date).c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(BuildPipeline::FormatBytes(entry.size).c_str());
					ImGui::TableNextColumn();
					ImGui::TextDisabled("%.16s...", entry.sha256.c_str());
					ImGui::SetItemTooltip("%s", entry.sha256.c_str());
					ImGui::TableNextColumn();
					if (ImGui::SmallButton(ICON_FA_COPY)) ImGui::SetClipboardText(entry.sha256.c_str());
					ImGui::SetItemTooltip("Copy SHA-256");
					ImGui::SameLine();
					if (ImGui::SmallButton(ICON_FA_FOLDER_OPEN)) EditorPlatform::RevealInExplorer(entry.archive);
					ImGui::SetItemTooltip("Show archive in Explorer");
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}
