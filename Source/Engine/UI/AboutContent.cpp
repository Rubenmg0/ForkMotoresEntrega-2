#include "AboutContent.h"
#include "BrandLogo.h"
#include "EditorIcons.h"
#include "../System/SystemInfo.h"
#include "../../Utils/Log.h"
#include "Version.h"

#include <imgui.h>
#include <SDL3/SDL.h>

#include <string>

namespace
{
	constexpr const char* REPO_URL = "https://github.com/JanCorredor/MotorVideojuego";
	constexpr const char* ISSUES_URL = "https://github.com/JanCorredor/MotorVideojuego/issues";

	const SystemInfo& GetSystemInfo()
	{
		// Se consulta una sola vez (necesita el contexto GL ya creado)
		static const SystemInfo info = SystemInfo::Query();
		return info;
	}

	void OpenURL(const char* url)
	{
		if (!SDL_OpenURL(url)) LOG_ERROR("[UI] Could not open URL '%s': %s", url, SDL_GetError());
	}

	void LibraryRow(const char* name, const std::string& version, const char* purpose)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(name);
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%s", version.c_str());
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(purpose);
	}
}

namespace AboutContent
{
	const char* GetVersionLabel()
	{
		static const std::string label = std::string(ENGINE_NAME) + " " + ENGINE_VERSION_STRING + " (" + ENGINE_GIT_HASH + ")";
		return label.c_str();
	}

	void Draw(float time)
	{
		const float baseSize = ImGui::GetStyle().FontSizeBase;

		// ------------------------------- Cabecera -------------------------------
		const float logoSize = 96.0f;
		const ImVec2 headerStart = ImGui::GetCursorScreenPos();
		BrandLogo::Widget(logoSize, time);
		ImGui::SameLine(0.0f, 18.0f);
		ImGui::BeginGroup();
		ImGui::Dummy(ImVec2(0.0f, 10.0f));
		const ImVec2 wordmarkPos = ImGui::GetCursorScreenPos();
		const ImVec2 wordmark = BrandLogo::DrawWordmark(ImGui::GetWindowDrawList(), wordmarkPos, baseSize * 2.6f);
		ImGui::Dummy(wordmark);
		ImGui::TextColored(EditorPalette::ACCENT_HOVERED, "Version %s", ENGINE_VERSION_STRING);
		ImGui::SameLine();
		ImGui::TextDisabled("(%s, %s)", ENGINE_GIT_HASH, ENGINE_BUILD_CONFIG);
		ImGui::EndGroup();
		(void)headerStart;

		ImGui::Spacing();
		ImGui::TextWrapped("Educational 3D game engine written in C++17 with an Entity/Component architecture, "
			"a resource Library with custom formats, octree-accelerated frustum culling and a Unity-style editor.");
		ImGui::Spacing();

		if (ImGui::Button(ICON_FA_CODE "  Source code")) OpenURL(REPO_URL);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_BUG "  Report an issue")) OpenURL(ISSUES_URL);

		// ------------------------------- Build -------------------------------
		ImGui::SeparatorText("Build");
		if (ImGui::BeginTable("AboutBuild", 2, ImGuiTableFlags_SizingFixedFit))
		{
			auto Row = [](const char* key, const char* value)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", key);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(value);
			};
			Row("Version", ENGINE_VERSION_STRING);
			Row("Commit", ENGINE_GIT_HASH);
			Row("Configuration", ENGINE_BUILD_CONFIG);
			Row("Built", ENGINE_BUILD_DATE);
			ImGui::EndTable();
		}

		// ------------------------------- Equipo -------------------------------
		ImGui::SeparatorText("Team");
		ImGui::BulletText("Jan Corredor");
		ImGui::BulletText("Ruben Mateo Guerrero");
		ImGui::BulletText("Marc Jimenez Marquina");

		// ------------------------------- Librerias -------------------------------
		ImGui::SeparatorText("Third-party libraries");
		const SystemInfo& info = GetSystemInfo();
		if (ImGui::BeginTable("AboutLibraries", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX))
		{
			LibraryRow("SDL3", info.sdlVersion, "Window, OpenGL context, input and audio");
			LibraryRow("OpenGL", info.glVersion, "3D rendering (GLAD loader)");
			LibraryRow("Dear ImGui", IMGUI_VERSION, "Editor and Hub UI, docking");
			LibraryRow("ImGuizmo", "1.10", "Transform gizmos");
			LibraryRow("Assimp", info.assimpVersion, "3D model import");
			LibraryRow("DevIL", info.devilVersion, "Texture loading");
			LibraryRow("GLM", "1.0", "Math");
			LibraryRow("nlohmann/json", "3.12", "Scenes, metadata and projects");
			LibraryRow("Tracy", "0.13", "Profiler (Debug builds)");
			LibraryRow("Font Awesome", "6", "Editor icons");
			ImGui::EndTable();
		}

		// ------------------------------- Licencia -------------------------------
		if (ImGui::CollapsingHeader("MIT License"))
		{
			ImGui::TextWrapped(
				"Copyright (c) 2026 Maruja Engine Team\n\n"
				"Permission is hereby granted, free of charge, to any person obtaining a copy "
				"of this software and associated documentation files (the \"Software\"), to deal "
				"in the Software without restriction, including without limitation the rights "
				"to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
				"copies of the Software, and to permit persons to whom the Software is "
				"furnished to do so, subject to the following conditions:\n\n"
				"The above copyright notice and this permission notice shall be included in all "
				"copies or substantial portions of the Software.\n\n"
				"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
				"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
				"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
				"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
				"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
				"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
				"SOFTWARE.");
		}
	}
}
