// =====================================================================================
// Ventana "Project Settings" del editor (File > Project Settings...) y titulo de la ventana.
// Edita el ProjectSettings/project.json del proyecto abierto: nombre, descripcion y version.
// =====================================================================================
#include "ModuleUI.h"
#include "EditorIcons.h"
#include "../Engine.h"
#include "../Window/Window.h"
#include "../Scene/Scene.h"
#include "../Project/ProjectInfo.h"
#include "../../Utils/Log.h"
#include "Version.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace
{
	void CopyToBuffer(char* buffer, size_t size, const std::string& text)
	{
		if (size == 0) return;
		const size_t length = std::min(size - 1, text.size());
		std::memcpy(buffer, text.data(), length);
		buffer[length] = '\0';
	}

	void InfoRow(const char* key, const std::string& value)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%s", key);
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(value.c_str());
	}
}

void ModuleUI::DrawProjectSettingsPanel()
{
	if (!showProjectSettings || !Project::HasActive()) return;

	ProjectInfo& project = Project::GetActive();
	if (!projectBuffersLoaded)
	{
		CopyToBuffer(projectNameBuffer, sizeof(projectNameBuffer), project.name);
		CopyToBuffer(projectDescriptionBuffer, sizeof(projectDescriptionBuffer), project.description);
		projectBuffersLoaded = true;
	}

	ImGui::SetNextWindowSize(ImVec2(460, 470), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(ICON_FA_GEAR "  Project Settings###ProjectSettings", &showProjectSettings))
	{
		ImGui::End();
		return;
	}

	bool save = false;

	ImGui::SeparatorText("Project");
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputText("##ProjectName", projectNameBuffer, sizeof(projectNameBuffer));
	if (ImGui::IsItemDeactivatedAfterEdit() && projectNameBuffer[0] != '\0')
	{
		project.name = projectNameBuffer;
		save = true;
	}
	ImGui::TextDisabled("Description");
	ImGui::InputTextMultiline("##ProjectDescription", projectDescriptionBuffer, sizeof(projectDescriptionBuffer), ImVec2(-1.0f, 70.0f));
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		project.description = projectDescriptionBuffer;
		save = true;
	}

	// ------------------------- Version del proyecto -------------------------
	ImGui::SeparatorText("Version");
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8f);
	ImGui::TextColored(EditorPalette::ACCENT_HOVERED, "%s", project.version.ToString().c_str());
	ImGui::PopFont();

	const float third = (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
	if (ImGui::Button("Major +1", ImVec2(third, 0.0f))) { project.version.BumpMajor(); save = true; }
	ImGui::SetItemTooltip("Breaking changes (X.0.0)");
	ImGui::SameLine();
	if (ImGui::Button("Minor +1", ImVec2(third, 0.0f))) { project.version.BumpMinor(); save = true; }
	ImGui::SetItemTooltip("New features (x.Y.0)");
	ImGui::SameLine();
	if (ImGui::Button("Patch +1", ImVec2(third, 0.0f))) { project.version.BumpPatch(); save = true; }
	ImGui::SetItemTooltip("Fixes (x.y.Z)");

	int parts[3] = { project.version.major, project.version.minor, project.version.patch };
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::InputInt3("##ProjectVersion", parts))
	{
		project.version.major = std::max(0, parts[0]);
		project.version.minor = std::max(0, parts[1]);
		project.version.patch = std::max(0, parts[2]);
	}
	if (ImGui::IsItemDeactivatedAfterEdit()) save = true;

	// ------------------------- Informacion -------------------------
	ImGui::SeparatorText("Info");
	if (ImGui::BeginTable("##ProjectInfo", 2, ImGuiTableFlags_SizingFixedFit))
	{
		InfoRow("Folder", Project::GetActiveRoot());
		InfoRow("Engine", std::string(ENGINE_NAME) + " " + project.engineVersion);
		InfoRow("Created with", project.createdWithEngine.empty() ? std::string("-") : project.createdWithEngine);
		InfoRow("Created", Project::FormatDate(project.createdAt));
		InfoRow("Modified", Project::FormatDate(project.lastModifiedAt));
		InfoRow("Startup scene", project.lastScene.empty() ? std::string("(none)") : project.lastScene);
		ImGui::EndTable();
	}

	if (save)
	{
		project.lastModifiedAt = Project::Now();
		if (Project::SaveActive()) LOG("[Project] Project settings saved (v%s)", project.version.ToString().c_str());
		autoWindowTitle.clear(); // fuerza a actualizar el titulo
	}

	ImGui::End();
}

void ModuleUI::UpdateWindowTitle()
{
	std::string title = std::string(ENGINE_NAME) + " " + ENGINE_VERSION_STRING;
	if (Project::HasActive())
	{
		const ProjectInfo& project = Project::GetActive();
		title += " - " + project.name + " v" + project.version.ToString();
	}
	if (Scene* scene = Engine::GetInstance().scene.get())
	{
		const std::string& scenePath = scene->GetCurrentScenePath();
		title += " - " + (scenePath.empty() ? std::string("Untitled") : FileSystem::GetFileNameNoExtension(scenePath));
	}

	if (title == autoWindowTitle) return;
	autoWindowTitle = title;
	if (Window* window = Engine::GetInstance().window.get())
	{
		window->SetTitle(title);
		CopyToBuffer(windowTitleBuffer, sizeof(windowTitleBuffer), title);
	}
}
