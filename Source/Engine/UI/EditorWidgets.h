#pragma once

// =====================================================================================
// Widgets compartidos por las ventanas del editor (estilo Unity).
// =====================================================================================
#include "EditorIcons.h"

#include <imgui.h>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <string>

namespace EditorWidgets
{
	// Tooltip del ultimo widget que tambien aparece si esta desactivado
	inline void IconTooltip(const char* text)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", text);
		}
	}

	// Boton con tres colores propios (normal / hover / pulsado) en lugar de los del tema
	inline bool ColoredButton(const char* label, const ImVec2& size, const ImVec4& color, const ImVec4& hovered, const ImVec4& active)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
		const bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor(3);
		return pressed;
	}

	// Boton "toggle" de Unity: hundido y azulado cuando esta activo
	inline bool ToggleButton(const char* label, bool active, const ImVec2& size = ImVec2(0.0f, 0.0f))
	{
		if (!active) return ImGui::Button(label, size);
		return ColoredButton(label, size, EditorPalette::TOGGLE_ON, EditorPalette::ACCENT_HOVERED, EditorPalette::ACCENT_ACTIVE);
	}

	// Boton plano (sin fondo hasta que se pasa el raton), como los iconos de las barras de Unity
	inline bool FlatButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f), bool active = false)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, active ? EditorPalette::TOGGLE_ON : ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? EditorPalette::ACCENT_HOVERED : ImVec4(1, 1, 1, 0.10f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? EditorPalette::ACCENT_ACTIVE : ImVec4(1, 1, 1, 0.18f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		const bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);
		return pressed;
	}

	inline bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower)
	{
		if (needleLower.empty()) return true;
		if (needleLower.size() > haystack.size()) return false;
		auto it = std::search(
			haystack.begin(), haystack.end(),
			needleLower.begin(), needleLower.end(),
			[](unsigned char a, unsigned char b) { return std::tolower(a) == b; }
		);
		return it != haystack.end();
	}

	inline std::string ToLower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	// ---------------------------------------------------------------------------------
	// Cabecera de componente del Inspector (como Unity): barra a todo el ancho con la flecha
	// para plegar, la casilla de "activo" (opcional), icono + nombre y el menu de tres puntos.
	// Devuelve si el componente esta desplegado. 'enabledChanged' / 'menuClicked' informan de
	// lo que ha pulsado el usuario en este frame.
	// ---------------------------------------------------------------------------------
	struct HeaderResult
	{
		bool open = false;
		bool enabledChanged = false;
		bool enabledActivated = false; // se acaba de pulsar la casilla (para el Undo)
		bool menuClicked = false;
	};

	inline HeaderResult ComponentHeader(const char* id, const char* icon, const ImVec4& iconColor, const char* title, bool* enabled)
	{
		HeaderResult result;
		ImGui::PushID(id);

		const float startX = ImGui::GetCursorScreenPos().x;
		ImGui::PushStyleColor(ImGuiCol_Header, EditorPalette::RAISED);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorPalette::RAISED_HOVERED);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, EditorPalette::RAISED_ACTIVE);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
		const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding;
		result.open = ImGui::TreeNodeEx("##ComponentHeader", flags, "%s", "");
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);

		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const float height = max.y - min.y;
		float x = min.x + ImGui::GetTreeNodeToLabelSpacing();

		// Casilla de activo, dentro de la cabecera
		if (enabled != nullptr)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 1.0f));
			const float box = ImGui::GetFrameHeight();
			ImGui::SetCursorScreenPos(ImVec2(x, min.y + (height - box) * 0.5f));
			result.enabledChanged = ImGui::Checkbox("##Enabled", enabled);
			result.enabledActivated = ImGui::IsItemActivated();
			ImGui::PopStyleVar();
			x += box + 6.0f;
		}

		// Icono + nombre
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float textY = min.y + (height - ImGui::GetFontSize()) * 0.5f;
		drawList->AddText(ImVec2(x, textY), ImGui::GetColorU32(iconColor), icon);
		x += ImGui::CalcTextSize(icon).x + 6.0f;
		drawList->AddText(ImVec2(x, textY), ImGui::GetColorU32(ImGuiCol_Text), title);

		// Menu de tres puntos (Reset, Remove Component, Copy / Paste...)
		const float menuSize = height - 4.0f;
		ImGui::SetCursorScreenPos(ImVec2(max.x - menuSize - 2.0f, min.y + 2.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
		result.menuClicked = FlatButton(ICON_FA_ELLIPSIS_VERTICAL "##ComponentMenu", ImVec2(menuSize, menuSize));
		ImGui::PopStyleVar();

		// El resto del componente continua debajo de la cabecera
		ImGui::SetCursorScreenPos(ImVec2(startX, max.y));
		ImGui::Dummy(ImVec2(0.0f, 0.0f));

		ImGui::PopID();
		return result;
	}

	// ---------------------------------------------------------------------------------
	// Fila de propiedad del Inspector: etiqueta a la izquierda (40 % del ancho) y el widget a
	// la derecha ocupando el resto. Despues de llamarla, crear el widget con un ID "##...".
	// ---------------------------------------------------------------------------------
	inline float PropertyLabelWidth()
	{
		return std::max(90.0f, ImGui::GetContentRegionAvail().x * 0.38f);
	}

	inline void PropertyLabel(const char* label)
	{
		const float labelWidth = PropertyLabelWidth();
		const float startX = ImGui::GetCursorPosX();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		if (ImGui::IsItemHovered() && ImGui::CalcTextSize(label).x > labelWidth) ImGui::SetTooltip("%s", label);
		ImGui::SameLine(startX + labelWidth);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	// ---------------------------------------------------------------------------------
	// Campo vec3 de Unity: "X [ ] Y [ ] Z [ ]". Las letras se pueden ARRASTRAR para cambiar el
	// valor (como los "slider labels" de Unity) y el campo se edita escribiendo.
	// 'mixed[i]' = los objetos seleccionados tienen valores distintos en ese eje: se muestra "—".
	// Devuelve una mascara con los ejes cambiados (bit 0 = X, 1 = Y, 2 = Z).
	// 'activated' / 'deactivated' sirven para agrupar la edicion en una sola entrada de Undo.
	// ---------------------------------------------------------------------------------
	inline int Vec3Field(const char* label, glm::vec3& values, float dragSpeed, const bool mixed[3], bool& activated, bool& deactivated)
	{
		int changedMask = 0;
		ImGui::PushID(label);

		const float labelWidth = PropertyLabelWidth();
		const float startX = ImGui::GetCursorPosX();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine(startX + labelWidth);

		const ImGuiStyle& style = ImGui::GetStyle();
		const float letterWidth = ImGui::CalcTextSize("X").x + 8.0f;
		const float avail = ImGui::GetContentRegionAvail().x;
		const float fieldWidth = std::max(20.0f, (avail - 3.0f * letterWidth - 2.0f * style.ItemSpacing.x) / 3.0f);
		const char* letters[3] = { "X", "Y", "Z" };

		for (int i = 0; i < 3; ++i)
		{
			if (i > 0) ImGui::SameLine();
			ImGui::PushID(i);

			// Letra arrastrable
			const ImVec2 letterPos = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##drag", ImVec2(letterWidth, ImGui::GetFrameHeight()));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			if (ImGui::IsItemActivated()) activated = true;
			if (ImGui::IsItemDeactivated()) deactivated = true;
			if (ImGui::IsItemActive())
			{
				const float delta = ImGui::GetIO().MouseDelta.x;
				if (delta != 0.0f)
				{
					const float speed = dragSpeed * (ImGui::GetIO().KeyShift ? 4.0f : 1.0f) * (ImGui::GetIO().KeyAlt ? 0.25f : 1.0f);
					values[i] += delta * speed;
					changedMask |= (1 << i);
				}
			}
			const ImVec2 textSize = ImGui::CalcTextSize(letters[i]);
			ImGui::GetWindowDrawList()->AddText(
				ImVec2(letterPos.x + (letterWidth - textSize.x) * 0.5f, letterPos.y + (ImGui::GetFrameHeight() - textSize.y) * 0.5f),
				ImGui::GetColorU32(ImGuiCol_Text), letters[i]);

			// Campo de texto (se escribe el numero; Enter o salir del campo lo aplica)
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::SetNextItemWidth(fieldWidth);
			float value = values[i];
			const bool isMixed = mixed != nullptr && mixed[i];
			if (ImGui::InputFloat("##value", &value, 0.0f, 0.0f, isMixed ? "\xE2\x80\x94" : "%g"))
			{
				values[i] = value;
				changedMask |= (1 << i);
			}
			if (ImGui::IsItemActivated()) activated = true;
			if (ImGui::IsItemDeactivated()) deactivated = true;

			ImGui::PopID();
		}

		ImGui::PopID();
		return changedMask;
	}
}
