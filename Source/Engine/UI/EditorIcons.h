#pragma once

// =====================================================================================
// Iconos y paleta del editor.
//
// Los iconos son glifos de la fuente "Font Awesome 6 Free" (fa-solid-900.ttf). Esa fuente se
// fusiona con la fuente de texto del editor en ModuleUI::LoadEditorFonts() (MergeMode), asi
// que un icono no es mas que un caracter Unicode dentro de un texto normal de ImGui:
//
//      ImGui::Button(ICON_FA_PLAY "##Play");          // boton solo con icono
//      ImGui::Text(ICON_FA_FOLDER "  %s", nombre);    // icono + texto
//
// Las macros ICON_FA_* (cabecera de IconFontCppHeaders) son literales UTF-8 escritos con
// secuencias \x, por lo que no dependen de la codificacion de los ficheros fuente.
// =====================================================================================
#include "../../External/IconFontCppHeaders/IconsFontAwesome6.h"

#include <imgui.h>

// Colores compartidos por el tema (ModuleUI::SetupEditorStyle) y por los widgets que necesitan
// salirse de el: botones "toggle" activos, cabeceras de componentes, iconos de cada tipo de asset...
namespace EditorPalette
{
	// Color de acento: seleccion, pestana activa, checks, sliders y botones pulsados
	const ImVec4 ACCENT         = ImVec4(0.16f, 0.50f, 0.93f, 1.00f);
	const ImVec4 ACCENT_HOVERED = ImVec4(0.27f, 0.60f, 0.98f, 1.00f);
	const ImVec4 ACCENT_ACTIVE  = ImVec4(0.11f, 0.41f, 0.82f, 1.00f);

	// Superficies "elevadas" (botones, cabeceras plegables) en sus tres estados
	const ImVec4 RAISED         = ImVec4(0.180f, 0.180f, 0.195f, 1.00f);
	const ImVec4 RAISED_HOVERED = ImVec4(0.245f, 0.245f, 0.265f, 1.00f);
	const ImVec4 RAISED_ACTIVE  = ImVec4(0.300f, 0.300f, 0.325f, 1.00f);

	// Iconos segun el tipo de elemento (ventana Assets y Hierarchy)
	const ImVec4 ICON_FOLDER    = ImVec4(0.96f, 0.78f, 0.36f, 1.00f);
	const ImVec4 ICON_TEXTURE   = ImVec4(0.42f, 0.78f, 1.00f, 1.00f);
	const ImVec4 ICON_MODEL     = ImVec4(0.50f, 0.90f, 0.56f, 1.00f);
	const ImVec4 ICON_MESH      = ImVec4(0.66f, 0.86f, 0.70f, 1.00f);
	const ImVec4 ICON_SCENE     = ImVec4(0.98f, 0.55f, 0.82f, 1.00f);
	const ImVec4 ICON_CAMERA    = ImVec4(0.45f, 0.75f, 1.00f, 1.00f);
	const ImVec4 ICON_NEUTRAL   = ImVec4(0.62f, 0.63f, 0.66f, 1.00f);

	// Estados de la simulacion (barra de Play) y niveles de log (Console).
	// Los nombres llevan prefijo para no chocar con macros (ERROR de Windows, LOG_ERROR del motor...)
	const ImVec4 STATE_PLAY          = ImVec4(0.38f, 0.86f, 0.48f, 1.00f);
	const ImVec4 STATE_STOP          = ImVec4(0.78f, 0.25f, 0.22f, 1.00f);
	const ImVec4 STATE_STOP_HOVERED  = ImVec4(0.90f, 0.33f, 0.29f, 1.00f);
	const ImVec4 STATE_PAUSE         = ImVec4(0.84f, 0.58f, 0.14f, 1.00f);
	const ImVec4 STATE_PAUSE_HOVERED = ImVec4(0.94f, 0.68f, 0.22f, 1.00f);
	const ImVec4 LEVEL_INFO          = ImVec4(0.42f, 0.78f, 1.00f, 1.00f);
	const ImVec4 LEVEL_WARNING       = ImVec4(1.00f, 0.80f, 0.28f, 1.00f);
	const ImVec4 LEVEL_ERROR         = ImVec4(1.00f, 0.42f, 0.40f, 1.00f);
}

namespace EditorIcons
{
	// Dibuja "icono + texto" en dos colores (icono tintado, texto normal) a partir de 'position'.
	//
	// ImGui pinta la etiqueta de un widget con un unico color, asi que para tener el icono de un
	// color y el nombre de otro el widget (Selectable / TreeNode) se crea SIN etiqueta visible y
	// la etiqueta se pinta aqui, directamente en el draw list de la ventana. Al no crear ningun
	// item nuevo, las consultas posteriores (IsItemClicked, drag & drop, tooltips, menu contextual)
	// siguen refiriendose al widget original.
	//
	// 'position' es la esquina superior izquierda del texto en coordenadas de pantalla:
	//   - Selectable: ImGui::GetCursorScreenPos() leido justo ANTES de crear el widget.
	//   - TreeNode:   lo mismo, sumando ImGui::GetTreeNodeToLabelSpacing() en X (hueco de la flecha).
	inline void DrawIconLabel(const ImVec2& position, const char* icon, const ImVec4& iconColor, const char* text, const ImVec4& textColor)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float fontSize = ImGui::GetFontSize();

		// Los glifos de Font Awesome no miden todos lo mismo: cada icono se centra en una celda de
		// ancho fijo para que los nombres queden alineados en columna
		const float cellWidth = static_cast<float>(static_cast<int>(fontSize * 1.25f));
		const float gap = static_cast<float>(static_cast<int>(fontSize * 0.30f));
		const float iconWidth = ImGui::CalcTextSize(icon).x;
		const float iconX = position.x + static_cast<float>(static_cast<int>((cellWidth - iconWidth) * 0.5f));

		// GetColorU32 aplica el alpha global del estilo (por ejemplo dentro de BeginDisabled)
		drawList->AddText(ImVec2(iconX, position.y), ImGui::GetColorU32(iconColor), icon);
		drawList->AddText(ImVec2(position.x + cellWidth + gap, position.y), ImGui::GetColorU32(textColor), text);
	}
}
