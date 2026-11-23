#pragma once

// =====================================================================================
// Iconos y paleta del editor.
//
// Los iconos son glifos de la fuente "Font Awesome 6 Free" (fa-solid-900.ttf). Esa fuente se
// fusiona con la fuente de texto del editor en EditorTheme::LoadFonts() (MergeMode), asi
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
	// Color de acento (azul de Unity): botones activos, foco, drag & drop
	const ImVec4 ACCENT         = ImVec4(0.227f, 0.475f, 0.733f, 1.00f); // #3A79BB
	const ImVec4 ACCENT_HOVERED = ImVec4(0.298f, 0.553f, 0.824f, 1.00f);
	const ImVec4 ACCENT_ACTIVE  = ImVec4(0.173f, 0.365f, 0.529f, 1.00f); // #2C5D87 (seleccion)

	// Superficies "elevadas" (cabeceras de componentes del Inspector, barras de herramientas)
	const ImVec4 RAISED         = ImVec4(0.243f, 0.243f, 0.243f, 1.00f); // #3E3E3E
	const ImVec4 RAISED_HOVERED = ImVec4(0.278f, 0.278f, 0.278f, 1.00f);
	const ImVec4 RAISED_ACTIVE  = ImVec4(0.310f, 0.310f, 0.310f, 1.00f);

	// Barras de herramientas (toolbar principal y barras de las ventanas)
	const ImVec4 TOOLBAR_BG     = ImVec4(0.098f, 0.098f, 0.098f, 1.00f); // #191919
	const ImVec4 PANEL_TOOLBAR  = ImVec4(0.235f, 0.235f, 0.235f, 1.00f); // #3C3C3C
	const ImVec4 TOGGLE_ON      = ImVec4(0.275f, 0.376f, 0.486f, 1.00f); // boton "pulsado" de Unity

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

// Titulos de las ventanas del editor (icono + nombre). El "###" fija el ID de la ventana: el
// layout (.ini) y el DockBuilder la reconocen aunque cambie el texto visible.
namespace EditorWindows
{
	constexpr const char* SCENE         = ICON_FA_TABLE_CELLS "  Scene###Scene";
	constexpr const char* GAME          = ICON_FA_GAMEPAD "  Game###Game";
	constexpr const char* HIERARCHY     = ICON_FA_LIST "  Hierarchy###Hierarchy";
	constexpr const char* INSPECTOR     = ICON_FA_CIRCLE_INFO "  Inspector###Inspector";
	constexpr const char* PROJECT       = ICON_FA_FOLDER "  Project###Project";
	constexpr const char* CONSOLE       = ICON_FA_TERMINAL "  Console###Console";
	constexpr const char* RESOURCES     = ICON_FA_DATABASE "  Resources###Resources";
	constexpr const char* CONFIGURATION = ICON_FA_GEAR "  Configuration###Configuration";
}
