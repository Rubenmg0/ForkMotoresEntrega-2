// =====================================================================================
// Vistas del Hub: barra lateral, lista de proyectos, panel de detalle (version del proyecto),
// modal de proyecto nuevo, ajustes y pagina "About".
// =====================================================================================
#include "HubApp.h"
#include "HubStyle.h"
#include "../Engine/Project/HubRegistry.h"
#include "../Engine/UI/AboutContent.h"
#include "../Engine/UI/BrandLogo.h"
#include "../Engine/UI/EditorPlatform.h"
#include "Version.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
	std::string ToLower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	void CopyToBuffer(char* buffer, size_t size, const std::string& text)
	{
		if (size == 0) return;
		const size_t length = std::min(size - 1, text.size());
		std::memcpy(buffer, text.data(), length);
		buffer[length] = '\0';
	}

	// Color estable a partir del nombre (miniatura de cada proyecto)
	ImVec4 ColorFromName(const std::string& name)
	{
		unsigned int hash = 2166136261u;
		for (unsigned char c : name) hash = (hash ^ c) * 16777619u;
		const float hue = static_cast<float>(hash % 360u) / 360.0f;
		float r, g, b;
		ImGui::ColorConvertHSVtoRGB(hue, 0.50f, 0.72f, r, g, b);
		return ImVec4(r, g, b, 1.0f);
	}

	// Iniciales: "Street Demo" -> "SD", "racer" -> "RA"
	std::string Initials(const std::string& name)
	{
		std::string result;
		bool newWord = true;
		for (unsigned char c : name)
		{
			if (std::isspace(c) || c == '_' || c == '-') { newWord = true; continue; }
			if (newWord && std::isalnum(c)) { result += static_cast<char>(std::toupper(c)); newWord = false; }
			if (result.size() == 2) break;
		}
		if (result.size() < 2)
		{
			result.clear();
			for (unsigned char c : name)
			{
				if (std::isalnum(c)) result += static_cast<char>(std::toupper(c));
				if (result.size() == 2) break;
			}
		}
		return result.empty() ? std::string("?") : result;
	}

	// Miniatura: cuadrado redondeado con degradado y las iniciales
	void DrawProjectTile(ImDrawList* drawList, const ImVec2& pos, float size, const std::string& name, bool available)
	{
		ImVec4 color = ColorFromName(name);
		if (!available) color = ImVec4(0.35f, 0.35f, 0.37f, 1.0f);
		const ImVec4 dark(color.x * 0.55f, color.y * 0.55f, color.z * 0.55f, 1.0f);
		const ImVec2 end(pos.x + size, pos.y + size);
		const float rounding = size * 0.18f;
		drawList->AddRectFilled(pos, end, ImGui::ColorConvertFloat4ToU32(dark), rounding);
		drawList->AddRectFilled(pos, ImVec2(end.x, pos.y + size * 0.55f), ImGui::ColorConvertFloat4ToU32(color), rounding, ImDrawFlags_RoundCornersTop);
		drawList->AddRectFilled(ImVec2(pos.x, pos.y + size * 0.45f), ImVec2(end.x, pos.y + size * 0.55f), ImGui::ColorConvertFloat4ToU32(color));

		const std::string initials = Initials(name);
		const float fontSize = size * 0.42f;
		const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, initials.c_str());
		drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x + (size - extent.x) * 0.5f, pos.y + (size - extent.y) * 0.5f),
			IM_COL32(255, 255, 255, available ? 235 : 140), initials.c_str());
	}

	void SectionTitle(const char* text, float scale = 1.75f)
	{
		ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
		ImGui::PushStyleColor(ImGuiCol_Text, HubStyle::TEXT_STRONG);
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}

	// Boton de navegacion de la barra lateral
	bool NavButton(const char* icon, const char* label, bool active)
	{
		const ImVec2 size(ImGui::GetContentRegionAvail().x, 38.0f);
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::PushID(label);
		const bool pressed = ImGui::InvisibleButton("##nav", size);
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 end(pos.x + size.x, pos.y + size.y);
		if (active)
		{
			drawList->AddRectFilled(pos, end, ImGui::ColorConvertFloat4ToU32(ImVec4(0.227f, 0.475f, 0.733f, 0.22f)), 6.0f);
			drawList->AddRectFilled(pos, ImVec2(pos.x + 3.0f, end.y), ImGui::ColorConvertFloat4ToU32(EditorPalette::ACCENT_HOVERED), 2.0f);
		}
		else if (hovered)
		{
			drawList->AddRectFilled(pos, end, IM_COL32(255, 255, 255, 14), 6.0f);
		}
		const float textY = pos.y + (size.y - ImGui::GetFontSize()) * 0.5f;
		const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(active ? HubStyle::TEXT_STRONG : HubStyle::TEXT_MUTED);
		drawList->AddText(ImVec2(pos.x + 16.0f, textY), active ? ImGui::ColorConvertFloat4ToU32(EditorPalette::ACCENT_HOVERED) : textColor, icon);
		drawList->AddText(ImVec2(pos.x + 44.0f, textY), textColor, label);
		return pressed;
	}

	void KeyValue(const char* key, const std::string& value)
	{
		ImGui::TextColored(HubStyle::TEXT_MUTED, "%s", key);
		ImGui::SameLine(120.0f);
		ImGui::TextUnformatted(value.c_str());
	}
}

// =====================================================================================
// RAIZ
// =====================================================================================
void HubApp::DrawHub()
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##Hub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::PopStyleVar();

	// Fundido de entrada tras el splash
	const float fade = std::clamp((time - static_cast<float>(stageStartTime)) / 0.35f, 0.0f, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f + fade);

	DrawSidebar();
	ImGui::SameLine(0.0f, 0.0f);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 22.0f));
	ImGui::BeginChild("##Content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
	switch (page)
	{
	case Page::Projects: DrawProjectsPage(); break;
	case Page::Settings: DrawSettingsPage(); break;
	case Page::About:    DrawAboutPage(); break;
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();

	DrawNewProjectModal();
	DrawEngineVersionModal();

	// Aviso temporal (toast) abajo a la derecha
	if (!toastText.empty() && time - toastTime < 3.5f)
	{
		const float alpha = std::clamp((3.5f - (time - toastTime)) / 0.4f, 0.0f, 1.0f);
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		const ImVec2 extent = ImGui::CalcTextSize(toastText.c_str());
		const ImVec2 end(viewport->Pos.x + viewport->Size.x - 20.0f, viewport->Pos.y + viewport->Size.y - 20.0f);
		const ImVec2 start(end.x - extent.x - 44.0f, end.y - extent.y - 20.0f);
		drawList->AddRectFilled(start, end, ImGui::ColorConvertFloat4ToU32(ImVec4(0.17f, 0.17f, 0.18f, 0.96f * alpha)), 6.0f);
		const ImVec4 accent = toastError ? HubStyle::DANGER : HubStyle::SUCCESS;
		drawList->AddRectFilled(start, ImVec2(start.x + 4.0f, end.y), ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, alpha)), 6.0f, ImDrawFlags_RoundCornersLeft);
		drawList->AddText(ImVec2(start.x + 14.0f, start.y + 10.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, alpha)),
			toastError ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CHECK);
		drawList->AddText(ImVec2(start.x + 34.0f, start.y + 10.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, alpha)), toastText.c_str());
	}

	ImGui::PopStyleVar();
	ImGui::End();
}

// =====================================================================================
// BARRA LATERAL
// =====================================================================================
void HubApp::DrawSidebar()
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, HubStyle::SIDEBAR_BG);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 18.0f));
	ImGui::BeginChild("##Sidebar", ImVec2(HubStyle::SIDEBAR_WIDTH, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// Logo + marca
	const ImVec2 start = ImGui::GetCursorScreenPos();
	BrandLogo::Draw(ImGui::GetWindowDrawList(), ImVec2(start.x + 26.0f, start.y + 26.0f), 52.0f, time);
	BrandLogo::DrawWordmark(ImGui::GetWindowDrawList(), ImVec2(start.x + 62.0f, start.y + 6.0f), ImGui::GetStyle().FontSizeBase * 1.85f);
	ImGui::Dummy(ImVec2(0.0f, 60.0f));
	ImGui::Spacing();

	if (NavButton(ICON_FA_CUBES, "Projects", page == Page::Projects)) page = Page::Projects;
	if (NavButton(ICON_FA_GEAR, "Settings", page == Page::Settings))
	{
		page = Page::Settings;
		CopyToBuffer(defaultLocationBuffer, sizeof(defaultLocationBuffer), HubRegistry::GetDefaultProjectsDir());
	}
	if (NavButton(ICON_FA_CIRCLE_INFO, "About", page == Page::About)) page = Page::About;

	// Pie: version del motor instalada
	const float footerHeight = 54.0f;
	const float available = ImGui::GetContentRegionAvail().y;
	if (available > footerHeight) ImGui::Dummy(ImVec2(0.0f, available - footerHeight));
	ImGui::Separator();
	ImGui::TextColored(HubStyle::TEXT_MUTED, ICON_FA_TAG "  %s %s", ENGINE_NAME, ENGINE_VERSION_STRING);
	ImGui::TextColored(ImVec4(HubStyle::TEXT_MUTED.x, HubStyle::TEXT_MUTED.y, HubStyle::TEXT_MUTED.z, 0.6f),
		ICON_FA_CODE_BRANCH "  %s  " ICON_FA_GEAR " %s", ENGINE_GIT_HASH, ENGINE_BUILD_CONFIG);

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
}

// =====================================================================================
// PROYECTOS
// =====================================================================================
void HubApp::DrawProjectsPage()
{
	// Cabecera: titulo + botones
	SectionTitle("Projects");
	const float buttonsWidth = 260.0f;
	ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonsWidth);
	const float buttonsY = ImGui::GetCursorPosY() + 4.0f;
	ImGui::SetCursorPosY(buttonsY);
	if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Add", ImVec2(100.0f, 0.0f)))
	{
		ShowFolderDialog(DialogPurpose::AddExisting, HubRegistry::GetDefaultProjectsDir());
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add an existing project folder to the list");
	ImGui::SameLine();
	ImGui::SetCursorPosY(buttonsY);
	if (HubStyle::PrimaryButton(ICON_FA_PLUS "  New project", ImVec2(152.0f, 0.0f))) OpenNewProjectModal();

	// Buscador
	ImGui::SetNextItemWidth(320.0f);
	ImGui::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS "  Search projects", searchBuffer, sizeof(searchBuffer));
	ImGui::SameLine();
	ImGui::TextColored(HubStyle::TEXT_MUTED, "%d project%s", static_cast<int>(projects.size()), projects.size() == 1 ? "" : "s");
	ImGui::Spacing();

	if (projects.empty())
	{
		DrawEmptyState();
		return;
	}

	// Filtro del buscador (nombre o ruta)
	const std::string filter = ToLower(searchBuffer);
	std::vector<int> visible;
	for (int i = 0; i < static_cast<int>(projects.size()); ++i)
	{
		if (filter.empty() || ToLower(projects[i].info.name).find(filter) != std::string::npos ||
			ToLower(projects[i].root).find(filter) != std::string::npos)
		{
			visible.push_back(i);
		}
	}

	const bool showDetails = selected >= 0 && selected < static_cast<int>(projects.size());
	const float tableWidth = showDetails ? ImGui::GetContentRegionAvail().x - HubStyle::DETAILS_WIDTH - 16.0f : 0.0f;

	ImGui::BeginChild("##ProjectList", ImVec2(tableWidth, 0));
	DrawProjectsTable(visible);
	ImGui::EndChild();

	if (showDetails)
	{
		ImGui::SameLine(0.0f, 16.0f);
		DrawDetailsPanel();
	}

	if (pendingListAction != ListAction::None) ProcessListAction();
}

void HubApp::DrawProjectsTable(const std::vector<int>& visible)
{
	if (visible.empty())
	{
		ImGui::Dummy(ImVec2(0.0f, 30.0f));
		ImGui::TextColored(HubStyle::TEXT_MUTED, "No projects match \"%s\"", searchBuffer);
		return;
	}

	const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX |
		ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
	if (!ImGui::BeginTable("##Projects", 4, flags)) return;

	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
	ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 80.0f);
	ImGui::TableSetupColumn("Engine", ImGuiTableColumnFlags_WidthFixed, 80.0f);
	ImGui::TableSetupColumn("Last opened", ImGuiTableColumnFlags_WidthFixed, 120.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, HubStyle::TEXT_MUTED);
	ImGui::TableHeadersRow();
	ImGui::PopStyleColor();

	int openRequest = -1;
	for (int index : visible)
	{
		HubProject& project = projects[index];
		ImGui::PushID(index);
		ImGui::TableNextRow(ImGuiTableRowFlags_None, HubStyle::ROW_HEIGHT);
		ImGui::TableNextColumn();

		// Fila seleccionable que ocupa todas las columnas
		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		if (ImGui::Selectable("##row", selected == index, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
			ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, HubStyle::ROW_HEIGHT - 4.0f)))
		{
			selected = index;
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openRequest = index;
		}
		if (ImGui::BeginPopupContextItem("##ctx"))
		{
			selected = index;
			DrawProjectContextMenu(index);
			ImGui::EndPopup();
		}
		if (ImGui::IsItemHovered() && !project.exists) ImGui::SetTooltip("Folder not found:\n%s", project.root.c_str());

		// Columna 1: miniatura + nombre + ruta (pintados sobre el Selectable)
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float tileSize = HubStyle::ROW_HEIGHT - 18.0f;
		DrawProjectTile(drawList, ImVec2(rowStart.x + 2.0f, rowStart.y + 6.0f), tileSize, project.info.name, project.exists);
		const float textX = rowStart.x + tileSize + 16.0f;
		std::string title = project.info.name;
		if (project.pinned) title = std::string(ICON_FA_THUMBTACK) + "  " + title;
		drawList->AddText(ImVec2(textX, rowStart.y + 8.0f),
			ImGui::ColorConvertFloat4ToU32(project.exists ? HubStyle::TEXT_STRONG : HubStyle::DANGER), title.c_str());
		const std::string pathLine = project.exists ? project.root : (std::string(ICON_FA_TRIANGLE_EXCLAMATION) + "  Missing: " + project.root);
		drawList->PushClipRect(ImVec2(textX, rowStart.y), ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x, rowStart.y + HubStyle::ROW_HEIGHT), true);
		drawList->AddText(ImVec2(textX, rowStart.y + 28.0f), ImGui::ColorConvertFloat4ToU32(HubStyle::TEXT_MUTED), pathLine.c_str());
		drawList->PopClipRect();

		// Columna 2: version del proyecto
		ImGui::TableNextColumn();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.0f);
		if (project.hasProjectFile) ImGui::Text("v%s", project.info.version.ToString().c_str());
		else ImGui::TextColored(HubStyle::TEXT_MUTED, "-");

		// Columna 3: version del motor (aviso si no coincide con la instalada)
		ImGui::TableNextColumn();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.0f);
		if (project.hasProjectFile && !project.info.engineVersion.empty())
		{
			const bool matches = project.info.engineVersion == ENGINE_VERSION_STRING;
			if (matches) ImGui::TextColored(HubStyle::TEXT_MUTED, "%s", project.info.engineVersion.c_str());
			else
			{
				ImGui::TextColored(HubStyle::WARNING, ICON_FA_TRIANGLE_EXCLAMATION " %s", project.info.engineVersion.c_str());
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Last opened with %s %s.\nThis installation is %s.", ENGINE_NAME,
						project.info.engineVersion.c_str(), ENGINE_VERSION_STRING);
				}
			}
		}
		else ImGui::TextColored(HubStyle::TEXT_MUTED, "-");

		// Columna 4: ultima apertura
		ImGui::TableNextColumn();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.0f);
		ImGui::TextColored(HubStyle::TEXT_MUTED, "%s", Project::FormatRelative(project.lastOpened).c_str());
		if (ImGui::IsItemHovered() && project.lastOpened > 0) ImGui::SetTooltip("%s", Project::FormatDate(project.lastOpened).c_str());

		ImGui::PopID();
	}
	ImGui::EndTable();

	if (openRequest >= 0) RequestOpenProject(openRequest);

	// Atajos: Enter abre el seleccionado, Supr lo quita de la lista
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() && selected >= 0)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Enter)) RequestOpenProject(selected);
	}
}

void HubApp::DrawProjectContextMenu(int index)
{
	HubProject& project = projects[index];
	if (ImGui::MenuItem(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "  Open", nullptr, false, project.exists)) RequestOpenProject(index);
	if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Show in Explorer", nullptr, false, project.exists)) EditorPlatform::RevealInExplorer(project.root);
	// Quitar y fijar cambian la lista: se aplican despues de dibujar la tabla (ProcessListAction)
	if (ImGui::MenuItem(ICON_FA_THUMBTACK "  Pin to top", nullptr, project.pinned))
	{
		pendingListAction = ListAction::TogglePin;
		pendingListIndex = index;
	}
	ImGui::Separator();
	if (ImGui::MenuItem(ICON_FA_XMARK "  Remove from list"))
	{
		pendingListAction = ListAction::Remove;
		pendingListIndex = index;
	}
}

void HubApp::ProcessListAction()
{
	const ListAction action = pendingListAction;
	const int index = pendingListIndex;
	pendingListAction = ListAction::None;
	pendingListIndex = -1;
	if (index < 0 || index >= static_cast<int>(projects.size())) return;

	if (action == ListAction::Remove)
	{
		RemoveProject(index);
	}
	else if (action == ListAction::TogglePin)
	{
		if (HubRegistry::Entry* entry = HubRegistry::Find(projects[index].root))
		{
			entry->pinned = !entry->pinned;
			HubRegistry::Save();
			RefreshProjects();
		}
	}
}

void HubApp::DrawEmptyState()
{
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const float blockHeight = 260.0f;
	ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (available.y - blockHeight) * 0.4f)));

	const float centerX = ImGui::GetCursorScreenPos().x + available.x * 0.5f;
	BrandLogo::Draw(ImGui::GetWindowDrawList(), ImVec2(centerX, ImGui::GetCursorScreenPos().y + 60.0f), 120.0f, time, 0.8f);
	ImGui::Dummy(ImVec2(0.0f, 130.0f));

	auto Centered = [&](const char* text, const ImVec4& color)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - ImGui::CalcTextSize(text).x) * 0.5f);
		ImGui::TextColored(color, "%s", text);
	};
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.4f);
	Centered("No projects yet", HubStyle::TEXT_STRONG);
	ImGui::PopFont();
	Centered("Create a new project or add an existing project folder to get started.", HubStyle::TEXT_MUTED);
	ImGui::Spacing();

	const float buttonsWidth = 152.0f + 120.0f + ImGui::GetStyle().ItemSpacing.x;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - buttonsWidth) * 0.5f);
	if (HubStyle::PrimaryButton(ICON_FA_PLUS "  New project", ImVec2(152.0f, 34.0f))) OpenNewProjectModal();
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Add", ImVec2(120.0f, 34.0f))) ShowFolderDialog(DialogPurpose::AddExisting, HubRegistry::GetDefaultProjectsDir());
}

// =====================================================================================
// PANEL DE DETALLE: datos del proyecto y gestion de su version
// =====================================================================================
void HubApp::DrawDetailsPanel()
{
	HubProject& project = projects[selected];

	ImGui::PushStyleColor(ImGuiCol_ChildBg, HubStyle::PANEL_BG);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 18.0f));
	ImGui::BeginChild("##Details", ImVec2(HubStyle::DETAILS_WIDTH, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

	// Cabecera: miniatura grande + nombre
	const ImVec2 start = ImGui::GetCursorScreenPos();
	DrawProjectTile(ImGui::GetWindowDrawList(), start, 64.0f, project.info.name, project.exists);
	ImGui::SetCursorScreenPos(ImVec2(start.x + 78.0f, start.y + 6.0f));
	ImGui::BeginGroup();
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + HubStyle::DETAILS_WIDTH - 78.0f - 40.0f);
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
	ImGui::TextColored(HubStyle::TEXT_STRONG, "%s", project.info.name.c_str());
	ImGui::PopFont();
	ImGui::PopTextWrapPos();
	if (project.hasProjectFile) ImGui::TextColored(EditorPalette::ACCENT_HOVERED, "v%s", project.info.version.ToString().c_str());
	else ImGui::TextColored(HubStyle::TEXT_MUTED, project.exists ? "No project.json yet" : "Folder not found");
	ImGui::EndGroup();
	ImGui::SetCursorScreenPos(ImVec2(start.x, std::max(start.y + 76.0f, ImGui::GetCursorScreenPos().y + 6.0f)));

	// Abrir
	ImGui::BeginDisabled(!project.exists);
	if (HubStyle::PrimaryButton(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "  Open project", ImVec2(-1.0f, 36.0f))) RequestOpenProject(selected);
	ImGui::EndDisabled();
	if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Explorer", ImVec2((ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f, 0.0f)))
	{
		EditorPlatform::RevealInExplorer(project.root);
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_XMARK "  Remove", ImVec2(-1.0f, 0.0f)))
	{
		RemoveProject(selected);
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		return;
	}

	ImGui::TextColored(HubStyle::TEXT_MUTED, "%s", project.root.c_str());

	if (project.hasProjectFile)
	{
		// ------------------------- Version del proyecto -------------------------
		ImGui::SeparatorText("Project version");
		ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
		ImGui::TextColored(HubStyle::TEXT_STRONG, "%s", project.info.version.ToString().c_str());
		ImGui::PopFont();

		const float third = (ImGui::GetContentRegionAvail().x - 16.0f) / 3.0f;
		bool changed = false;
		if (ImGui::Button("Major +1", ImVec2(third, 0.0f))) { project.info.version.BumpMajor(); changed = true; }
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Breaking changes: %d.0.0", project.info.version.major + 1);
		ImGui::SameLine();
		if (ImGui::Button("Minor +1", ImVec2(third, 0.0f))) { project.info.version.BumpMinor(); changed = true; }
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("New features: %d.%d.0", project.info.version.major, project.info.version.minor + 1);
		ImGui::SameLine();
		if (ImGui::Button("Patch +1", ImVec2(third, 0.0f))) { project.info.version.BumpPatch(); changed = true; }
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Fixes: %d.%d.%d", project.info.version.major, project.info.version.minor, project.info.version.patch + 1);
		}

		// Version exacta a mano
		// (el valor se aplica mientras se escribe y se guarda al salir del campo)
		int parts[3] = { project.info.version.major, project.info.version.minor, project.info.version.patch };
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputInt3("##SetVersion", parts))
		{
			project.info.version.major = std::max(0, parts[0]);
			project.info.version.minor = std::max(0, parts[1]);
			project.info.version.patch = std::max(0, parts[2]);
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		if (changed)
		{
			SaveProjectInfo(project);
			ShowToast("'" + project.info.name + "' is now v" + project.info.version.ToString());
		}

		// ------------------------- Descripcion -------------------------
		ImGui::SeparatorText("Description");
		if (detailsBufferIndex != selected)
		{
			CopyToBuffer(detailsDescriptionBuffer, sizeof(detailsDescriptionBuffer), project.info.description);
			detailsBufferIndex = selected;
		}
		ImGui::InputTextMultiline("##Description", detailsDescriptionBuffer, sizeof(detailsDescriptionBuffer), ImVec2(-1.0f, 70.0f),
			ImGuiInputTextFlags_WordWrap);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			project.info.description = detailsDescriptionBuffer;
			SaveProjectInfo(project);
		}

		// ------------------------- Informacion -------------------------
		ImGui::SeparatorText("Info");
		KeyValue("Engine", project.info.engineVersion.empty() ? std::string("-") : project.info.engineVersion);
		if (!project.info.createdWithEngine.empty() && project.info.createdWithEngine != project.info.engineVersion)
		{
			KeyValue("Created with", project.info.createdWithEngine);
		}
		KeyValue("Created", Project::FormatDate(project.info.createdAt));
		KeyValue("Modified", Project::FormatDate(project.info.lastModifiedAt));
		KeyValue("Last opened", Project::FormatDate(project.lastOpened));
		if (!project.info.lastScene.empty()) KeyValue("Last scene", project.info.lastScene);
	}
	else if (project.exists)
	{
		ImGui::Spacing();
		ImGui::TextWrapped("This folder has no ProjectSettings/project.json. It will be created the first time you open the project.");
	}

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

// =====================================================================================
// MODALES
// =====================================================================================
void HubApp::DrawNewProjectModal()
{
	if (openNewProjectPopup)
	{
		ImGui::OpenPopup("New project");
		openNewProjectPopup = false;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 20.0f));
	if (!ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
	{
		ImGui::PopStyleVar();
		return;
	}

	SectionTitle(ICON_FA_FOLDER_PLUS "  New project", 1.4f);
	ImGui::TextColored(HubStyle::TEXT_MUTED, "Creates an empty 3D project with Assets/, Assets/Scenes/ and ProjectSettings/.");
	ImGui::Spacing();

	ImGui::TextUnformatted("Project name");
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
	ImGui::InputText("##Name", newNameBuffer, sizeof(newNameBuffer));

	ImGui::TextUnformatted("Location");
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 46.0f);
	ImGui::InputText("##Location", newLocationBuffer, sizeof(newLocationBuffer));
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_FOLDER_OPEN "##Browse", ImVec2(-1.0f, 0.0f)))
	{
		ShowFolderDialog(DialogPurpose::NewProjectLocation, newLocationBuffer);
	}

	ImGui::TextUnformatted("Description (optional)");
	ImGui::InputTextMultiline("##NewDescription", newDescriptionBuffer, sizeof(newDescriptionBuffer), ImVec2(-1.0f, 54.0f),
		ImGuiInputTextFlags_WordWrap);

	// Validacion en vivo
	std::string validation;
	const std::string name = newNameBuffer;
	const std::string location = newLocationBuffer;
	bool valid = Project::IsValidProjectName(name, &validation);
	const std::string finalPath = location + "/" + name;
	if (valid && location.empty()) { valid = false; validation = "Choose a location"; }
	if (valid && Project::FolderExists(finalPath)) { valid = false; validation = "A folder with that name already exists in this location"; }

	ImGui::Spacing();
	ImGui::TextColored(HubStyle::TEXT_MUTED, ICON_FA_FOLDER "  %s", finalPath.c_str());
	ImGui::TextColored(HubStyle::TEXT_MUTED, ICON_FA_TAG "  Version 0.1.0  " ICON_FA_CUBES "  %s %s", ENGINE_NAME, ENGINE_VERSION_STRING);
	if (!valid) ImGui::TextColored(HubStyle::WARNING, ICON_FA_TRIANGLE_EXCLAMATION "  %s", validation.c_str());
	if (!newProjectError.empty()) ImGui::TextColored(HubStyle::DANGER, ICON_FA_TRIANGLE_EXCLAMATION "  %s", newProjectError.c_str());

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	const float buttonWidth = 130.0f;
	ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x);
	if (ImGui::Button("Cancel", ImVec2(buttonWidth, 32.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
	ImGui::SameLine();
	ImGui::BeginDisabled(!valid);
	const bool create = HubStyle::PrimaryButton("Create", ImVec2(buttonWidth, 32.0f)) || (valid && ImGui::IsKeyPressed(ImGuiKey_Enter));
	ImGui::EndDisabled();
	if (create)
	{
		// La carpeta padre se crea si no existe (p.ej. "Documentos/Maruja Projects" la primera vez)
		if (!Project::FolderExists(location))
		{
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::u8path(location), ec);
		}
		std::string root;
		if (Project::CreateNewProject(location, name, newDescriptionBuffer, root, &newProjectError))
		{
			HubRegistry::AddOrTouch(root, false);
			HubRegistry::Save();
			ImGui::CloseCurrentPopup();
			OpenProjectNow(root);
		}
	}

	ImGui::EndPopup();
	ImGui::PopStyleVar();
}

void HubApp::DrawEngineVersionModal()
{
	if (openEngineVersionPopup)
	{
		ImGui::OpenPopup("Engine version");
		openEngineVersionPopup = false;
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f));
	if (!ImGui::BeginPopupModal("Engine version", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) return;

	const bool validIndex = pendingOpenIndex >= 0 && pendingOpenIndex < static_cast<int>(projects.size());
	SectionTitle(ICON_FA_TRIANGLE_EXCLAMATION "  Newer engine version", 1.3f);
	if (validIndex)
	{
		const HubProject& project = projects[pendingOpenIndex];
		ImGui::TextWrapped("'%s' was last opened with %s %s, but this installation is %s.",
			project.info.name.c_str(), ENGINE_NAME, project.info.engineVersion.c_str(), ENGINE_VERSION_STRING);
		ImGui::TextWrapped("Opening it with an older version may lose data saved by newer features.");
	}
	ImGui::Spacing();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 30.0f)))
	{
		pendingOpenIndex = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (HubStyle::PrimaryButton("Open anyway", ImVec2(140.0f, 30.0f)))
	{
		if (validIndex) OpenProjectNow(projects[pendingOpenIndex].root);
		pendingOpenIndex = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void HubApp::DrawOpeningOverlay()
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	const ImVec2 end(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
	drawList->AddRectFilled(viewport->Pos, end, IM_COL32(10, 12, 18, 215));
	const ImVec2 center = viewport->GetCenter();
	BrandLogo::Draw(drawList, ImVec2(center.x, center.y - 40.0f), 110.0f, time);
	const std::string text = "Opening " + openingName + "...";
	const ImVec2 extent = ImGui::CalcTextSize(text.c_str());
	drawList->AddText(ImVec2(center.x - extent.x * 0.5f, center.y + 34.0f), IM_COL32(235, 240, 248, 255), text.c_str());
}

// =====================================================================================
// AJUSTES Y ABOUT
// =====================================================================================
void HubApp::DrawSettingsPage()
{
	SectionTitle("Settings");
	ImGui::Spacing();

	ImGui::SeparatorText("Projects");
	ImGui::TextUnformatted("Default location for new projects");
	ImGui::SetNextItemWidth(std::min(520.0f, ImGui::GetContentRegionAvail().x - 120.0f));
	if (defaultLocationBuffer[0] == '\0') CopyToBuffer(defaultLocationBuffer, sizeof(defaultLocationBuffer), HubRegistry::GetDefaultProjectsDir());
	ImGui::InputText("##DefaultLocation", defaultLocationBuffer, sizeof(defaultLocationBuffer));
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		HubRegistry::SetDefaultProjectsDir(defaultLocationBuffer);
		HubRegistry::Save();
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Browse")) ShowFolderDialog(DialogPurpose::DefaultLocation, defaultLocationBuffer);

	ImGui::Spacing();
	if (ImGui::Button(ICON_FA_CLOCK_ROTATE_LEFT "  Rescan projects")) { RefreshProjects(); ShowToast("Project list refreshed"); }
	ImGui::SameLine();
	int missing = 0;
	for (const HubProject& project : projects) missing += project.exists ? 0 : 1;
	ImGui::BeginDisabled(missing == 0);
	char label[64];
	snprintf(label, sizeof(label), ICON_FA_TRASH "  Remove %d missing", missing);
	if (ImGui::Button(label))
	{
		for (const HubProject& project : projects)
		{
			if (!project.exists) HubRegistry::Remove(project.root);
		}
		HubRegistry::Save();
		selected = -1;
		RefreshProjects();
		ShowToast("Missing projects removed from the list");
	}
	ImGui::EndDisabled();

	ImGui::SeparatorText("Installation");
	KeyValue("Engine", std::string(ENGINE_NAME) + " " + ENGINE_VERSION_STRING);
	KeyValue("Commit", ENGINE_GIT_HASH);
	KeyValue("Build", std::string(ENGINE_BUILD_CONFIG) + ", " + ENGINE_BUILD_DATE);
	KeyValue("Hub data", HubRegistry::GetRegistryFile());
	if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Show Hub data folder")) EditorPlatform::RevealInExplorer(HubRegistry::GetRegistryFile());
}

void HubApp::DrawAboutPage()
{
	ImGui::BeginChild("##AboutScroll", ImVec2(std::min(760.0f, ImGui::GetContentRegionAvail().x), 0));
	AboutContent::Draw(time);
	ImGui::EndChild();
}
