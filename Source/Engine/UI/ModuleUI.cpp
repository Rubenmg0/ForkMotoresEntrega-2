#include "ModuleUI.h"
#include "../Engine.h"
#include "../Window/Window.h"
#include "../Input/Input.h"
#include "../Render/Render.h"
#include "../Render/Texture/TextureManager.h"
#include "../Render/Primitives/GeometryGenerator.h"
#include "../Scene/Scene.h"
#include "../Scene/GameObjects/GameObject.h"
#include "../Scene/GameObjects/Components/TransformComponent.h"
#include "../Scene/GameObjects/Components/MeshComponent.h"
#include "../Scene/GameObjects/Components/MaterialComponent.h"
#include "../Scene/GameObjects/Components/CameraComponent.h"
#include "../Audio/Audio.h"
#include "../ResourceManagement/ResourceManager.h"
#include "../ResourceManagement/ResourceMesh.h"
#include "../ResourceManagement/ResourceTexture.h"
#include "../ResourceManagement/Importers/ImporterManager.h"
#include "EditorDragDrop.h"
#include "EditorIcons.h"

#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h> // gizmos 3D (siempre despues de imgui.h)

#include <SDL3/SDL.h>

#include "../../Utils/Log.h"
#include <algorithm>
#include <cctype>
#include <string>

#include <functional>
#include <cfloat>
#include <cstring>

#ifdef _DEBUG
	#include <tracy/Tracy.hpp>
#endif

namespace
{
	constexpr const char* REPO_URL_DOCS     = "https://github.com/JanCorredor/MotorVideojuego";
	constexpr const char* REPO_URL_ISSUES   = "https://github.com/JanCorredor/MotorVideojuego/issues";
	constexpr const char* REPO_URL_RELEASES = "https://github.com/JanCorredor/MotorVideojuego/releases";

	// ----------------------------- Fuentes del editor -----------------------------
	// Ficheros dentro de FileSystem::EDITOR_FONTS_DIR
	constexpr const char* EDITOR_TEXT_FONT_FILE = "Roboto-Medium.ttf";
	constexpr const char* EDITOR_ICON_FONT_FILE = FONT_ICON_FILE_NAME_FAS; // "fa-solid-900.ttf"
	constexpr float EDITOR_FONT_SIZE = 16.0f; // px de la fuente de texto
	// Los glifos de Font Awesome ocupan todo el alto de la fuente (no dejan hueco como las letras), asi
	// que al mismo tamano se ven mas grandes que el texto. A 13 px quedan centrados con las mayusculas.
	constexpr float EDITOR_ICON_SIZE = 13.0f;

	// ----------------------------- Gizmo de transformacion -----------------------------
	// Ajuste a rejilla manteniendo Ctrl mientras se arrastra
	constexpr float GIZMO_SNAP_TRANSLATE = 0.5f; // unidades
	constexpr float GIZMO_SNAP_ROTATE = 15.0f;   // grados
	constexpr float GIZMO_SNAP_SCALE = 0.1f;

	// Cabecera plegable de un componente del Inspector. ImGuiCol_Header es tambien el color de las
	// filas seleccionadas (azul de acento), asi que aqui se sustituye por un gris neutro: el azul
	// queda reservado para "esto esta seleccionado".
	bool ComponentHeader(const char* label, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen)
	{
		ImGui::PushStyleColor(ImGuiCol_Header, EditorPalette::RAISED);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorPalette::RAISED_HOVERED);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, EditorPalette::RAISED_ACTIVE);
		const bool open = ImGui::CollapsingHeader(label, flags);
		ImGui::PopStyleColor(3);
		return open;
	}

	// Boton con tres colores propios (normal / hover / pulsado) en lugar de los del tema
	bool ColoredButton(const char* label, const ImVec2& size, const ImVec4& color, const ImVec4& hovered, const ImVec4& active)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
		const bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor(3);
		return pressed;
	}

	// Tooltip del ultimo widget que tambien aparece si esta desactivado (SetItemTooltip no lo hace).
	// Un boton que solo tiene icono necesita poder explicarse siempre, este activo o no.
	void IconTooltip(const char* text)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", text);
		}
	}

	// Helper para alinear etiquetas y campos vec3 uniformemente en tablas
	bool DrawVec3Control(const char* label, glm::vec3& values, float speed = 0.1f)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);

		ImGui::TableSetColumnIndex(1);
		ImGui::PushItemWidth(-1.0f);
		std::string id = std::string("##") + label;
		bool changed = ImGui::DragFloat3(id.c_str(), glm::value_ptr(values), speed);
		ImGui::PopItemWidth();
		return changed;
	}

	bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower)
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

	void OpenWebURL(const char* url)
	{
		if (!SDL_OpenURL(url))
		{
			LOG_ERROR("[UI] No se pudo abrir la URL '%s': %s", url, SDL_GetError());
		}
		else
		{
			LOG("[UI] Abriendo navegador en: %s", url);
		}
	}
}

ModuleUI::ModuleUI() : Module()
{
	name = "UI";
}

ModuleUI::~ModuleUI()
{
}

bool ModuleUI::Awake()
{
	return true;
}

bool ModuleUI::Start()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows

	// Fichero de layout nuevo: asi la primera vez se crea la disposicion con las ventanas
	// nuevas (Assets, Resources) ya acopladas, en vez de heredar el layout antiguo
	io.IniFilename = "imgui_layout_a2.ini";

	// Las ventanas solo se mueven arrastrando su barra de titulo. Sin esto, arrastrar dentro de una
	// vista Scene flotante (un gizmo, Alt + clic para orbitar...) moveria tambien la ventana.
	io.ConfigWindowsMoveFromTitleBarOnly = true;

	// Aspecto del editor: fuente de texto + iconos y tema oscuro propio
	LoadEditorFonts();
	SetupEditorStyle();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL3_InitForOpenGL(Engine::GetInstance().window->GetWindow(), Engine::GetInstance().window->GetGLContext());
	ImGui_ImplOpenGL3_Init("#version 130");

	LOG("[ImGui] Inicializando Dear ImGui versión: %s", IMGUI_VERSION);
	LOG("[ImGui] Backends inicializados: SDL3 + OpenGL3 (#version 130), Docking habilitado");

	const std::string& initTitle = Engine::GetInstance().window->GetTitle();
	strncpy_s(windowTitleBuffer, sizeof(windowTitleBuffer), initTitle.c_str(), _TRUNCATE);

	return true;
}

// =====================================================================================
// FUENTES: texto (.ttf) + iconos (Font Awesome 6) fusionados en una unica ImFont
// =====================================================================================
void ModuleUI::LoadEditorFonts()
{
	ImGuiIO& io = ImGui::GetIO();

	// ---------------------------------------------------------------------------------
	// 1) Fuente de texto principal: sustituye a ProggyClean (la fuente pixelada por defecto)
	// ---------------------------------------------------------------------------------
	const std::string textFontPath = FileSystem::Join(FileSystem::EDITOR_FONTS_DIR, EDITOR_TEXT_FONT_FILE);
	ImFont* textFont = nullptr;

	// Se comprueba antes que el fichero existe: AddFontFromFileTTF lanza un assert si no puede abrirlo
	if (FileSystem::Exists(textFontPath))
	{
		ImFontConfig textConfig;
		// Oversampling: el glifo se rasteriza a mas resolucion de la necesaria y despues se reduce.
		//  - Horizontal x3: ImGui coloca cada letra en posiciones sub-pixel en X; con mas muestras
		//    el espaciado entre letras es regular y los trazos verticales salen definidos.
		//  - Vertical x1: en Y el texto siempre cae en pixeles enteros, asi que sobremuestrear solo
		//    lo emborronaria. Con 1 las lineas horizontales (barra de la "e", la "t"...) quedan nitidas.
		textConfig.OversampleH = 3;
		textConfig.OversampleV = 1;
		textConfig.PixelSnapH = false; // el avance sub-pixel es justo lo que aprovecha OversampleH
		textFont = io.Fonts->AddFontFromFileTTF(FileSystem::ToDiskPath(textFontPath).c_str(), EDITOR_FONT_SIZE, &textConfig);
	}

	if (textFont != nullptr)
	{
		LOG("[ImGui] Fuente de texto cargada: '%s' (%.0f px)", textFontPath.c_str(), EDITOR_FONT_SIZE);
	}
	else
	{
		// Sin el .ttf el editor sigue funcionando con la fuente integrada en ImGui
		LOG_WARNING("[ImGui] No se pudo cargar la fuente '%s'. Se usa la fuente por defecto de ImGui.", textFontPath.c_str());
		textFont = io.Fonts->AddFontDefault();
	}
	io.FontDefault = textFont;

	// ---------------------------------------------------------------------------------
	// 2) Iconos: Font Awesome 6 fusionada DENTRO de la fuente anterior (MergeMode)
	//    A partir de aqui los ICON_FA_* se pueden escribir en cualquier texto de ImGui.
	// ---------------------------------------------------------------------------------
	const std::string iconFontPath = FileSystem::Join(FileSystem::EDITOR_FONTS_DIR, EDITOR_ICON_FONT_FILE);
	if (!FileSystem::Exists(iconFontPath))
	{
		LOG_ERROR("[ImGui] Falta la fuente de iconos '%s': los iconos del editor no se veran.", iconFontPath.c_str());
		return;
	}

	// Rango de codigos Unicode que se toma de la fuente de iconos (zona de uso privado). ImGui guarda
	// el puntero, no copia el array: por eso es 'static' (tiene que vivir tanto como el atlas).
	static const ImWchar iconRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };

	ImFontConfig iconConfig;
	iconConfig.MergeMode = true;                      // anade sus glifos a la fuente anterior en vez de crear otra ImFont
	iconConfig.PixelSnapH = true;                     // iconos alineados a pixeles enteros: bordes limpios
	iconConfig.GlyphMinAdvanceX = EDITOR_ICON_SIZE;   // ancho minimo comun para que los iconos se alineen en columna
	iconConfig.OversampleH = 2;
	iconConfig.OversampleV = 1;
	// Sin desplazamiento vertical: al fusionar, ImGui alinea las lineas base de las dos fuentes y con
	// EDITOR_ICON_SIZE el centro del icono coincide con el centro de las mayusculas del texto.
	iconConfig.GlyphOffset = ImVec2(0.0f, 0.0f);

	if (io.Fonts->AddFontFromFileTTF(FileSystem::ToDiskPath(iconFontPath).c_str(), EDITOR_ICON_SIZE, &iconConfig, iconRanges) != nullptr)
	{
		LOG("[ImGui] Iconos Font Awesome 6 fusionados con la fuente de texto ('%s', %.0f px)", iconFontPath.c_str(), EDITOR_ICON_SIZE);
	}
	else
	{
		LOG_ERROR("[ImGui] No se pudo cargar la fuente de iconos '%s'", iconFontPath.c_str());
	}
}

// =====================================================================================
// TEMA: oscuro, plano y con un unico color de acento (estilo Unreal Engine 5 / Unity Dark)
// =====================================================================================
void ModuleUI::SetupEditorStyle()
{
	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();

	// Punto de partida: el tema oscuro de ImGui. Cualquier color que no se redefina aqui abajo
	// (por ejemplo uno anadido en una version futura de ImGui) conserva un valor razonable.
	ImGui::StyleColorsDark(&style);

	// ---------------------------------------------------------------------------------
	// Geometria: espaciados compactos y esquinas ligeramente redondeadas
	// ---------------------------------------------------------------------------------
	style.WindowPadding = ImVec2(10.0f, 10.0f);
	style.FramePadding = ImVec2(8.0f, 4.0f);       // alto de boton / campo = fuente (16) + 2 * 4 = 24 px
	style.CellPadding = ImVec2(6.0f, 4.0f);
	style.ItemSpacing = ImVec2(8.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.IndentSpacing = 18.0f;
	style.ScrollbarSize = 13.0f;
	style.GrabMinSize = 10.0f;

	style.WindowRounding = 6.0f;
	style.ChildRounding = 4.0f;
	style.FrameRounding = 4.0f;
	style.PopupRounding = 6.0f;
	style.ScrollbarRounding = 8.0f;
	style.GrabRounding = 3.0f;
	style.TabRounding = 5.0f;

	// Bordes: una linea fina separa ventanas y popups; botones y campos son planos (sin borde)
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.TabBorderSize = 0.0f;
	style.TabBarBorderSize = 1.0f;
	style.TabBarOverlineSize = 2.0f;               // linea de acento sobre la pestana activa
	style.DockingSeparatorSize = 2.0f;
	style.SeparatorTextBorderSize = 1.0f;

	style.WindowMenuButtonPosition = ImGuiDir_None; // sin el triangulo de plegar en las barras de pestanas
	style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesFull; // lineas guia padre -> hijos (Hierarchy, Assets)
	style.TreeLinesSize = 1.0f;

	// ---------------------------------------------------------------------------------
	// Paleta: grises neutros con un punto frio. Cuanto mas "al fondo" esta una superficie, mas
	// oscura es; lo interactivo es mas claro y solo lo seleccionado / activo usa el azul de acento.
	// ---------------------------------------------------------------------------------
	const ImVec4 bgDeepest = ImVec4(0.055f, 0.055f, 0.060f, 1.00f); // campos de texto, zona vacia del dock
	const ImVec4 bgDark    = ImVec4(0.085f, 0.085f, 0.092f, 1.00f); // barras de titulo y de menu, pestanas inactivas
	const ImVec4 bgPanel   = ImVec4(0.125f, 0.125f, 0.135f, 1.00f); // fondo de los paneles
	const ImVec4 text      = ImVec4(0.900f, 0.905f, 0.915f, 1.00f); // contraste ~13:1 sobre bgPanel
	const ImVec4 textDim   = ImVec4(0.520f, 0.530f, 0.550f, 1.00f);
	const ImVec4 border    = ImVec4(0.000f, 0.000f, 0.000f, 0.55f);

	const ImVec4& accent = EditorPalette::ACCENT;
	const ImVec4& accentHovered = EditorPalette::ACCENT_HOVERED;
	const ImVec4& accentActive = EditorPalette::ACCENT_ACTIVE;
	// El mismo color con otra opacidad (para estados que se superponen al fondo)
	const auto withAlpha = [](const ImVec4& color, float alpha) { return ImVec4(color.x, color.y, color.z, alpha); };

	ImVec4* colors = style.Colors;

	// Texto
	colors[ImGuiCol_Text] = text;
	colors[ImGuiCol_TextDisabled] = textDim;
	colors[ImGuiCol_TextLink] = accentHovered;
	colors[ImGuiCol_TextSelectedBg] = withAlpha(accent, 0.45f);
	colors[ImGuiCol_InputTextCursor] = text;

	// Fondos de ventanas y paneles
	colors[ImGuiCol_WindowBg] = bgPanel;
	colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.16f);       // los paneles hijos (lista de Assets, log) quedan hundidos
	colors[ImGuiCol_PopupBg] = ImVec4(0.095f, 0.095f, 0.103f, 0.98f);
	colors[ImGuiCol_Border] = border;
	colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	colors[ImGuiCol_MenuBarBg] = bgDark;
	colors[ImGuiCol_TitleBg] = bgDark;
	colors[ImGuiCol_TitleBgActive] = bgDark;
	colors[ImGuiCol_TitleBgCollapsed] = bgDark;
	colors[ImGuiCol_DockingEmptyBg] = bgDeepest;
	colors[ImGuiCol_DockingPreview] = withAlpha(accent, 0.55f);

	// Campos (InputText, Drag, Slider, Checkbox, Combo): hundidos respecto al panel
	colors[ImGuiCol_FrameBg] = bgDeepest;
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.105f, 0.108f, 0.120f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.130f, 0.150f, 0.190f, 1.00f); // toque azul al editar
	colors[ImGuiCol_CheckMark] = accentHovered;
	colors[ImGuiCol_SliderGrab] = accent;
	colors[ImGuiCol_SliderGrabActive] = accentHovered;

	// Botones: elevados respecto al panel; al pulsar pasan al color de acento
	colors[ImGuiCol_Button] = EditorPalette::RAISED;
	colors[ImGuiCol_ButtonHovered] = EditorPalette::RAISED_HOVERED;
	colors[ImGuiCol_ButtonActive] = accent;

	// Header = filas seleccionadas (Selectable, TreeNode) y elementos de menu bajo el raton.
	// Las cabeceras de los componentes del Inspector usan ComponentHeader() para no salir azules.
	colors[ImGuiCol_Header] = withAlpha(accent, 0.45f);
	colors[ImGuiCol_HeaderHovered] = withAlpha(accent, 0.65f);
	colors[ImGuiCol_HeaderActive] = withAlpha(accent, 0.85f);

	// Pestanas: la activa tiene el color del panel (parece "unida" a el) y una linea de acento encima
	colors[ImGuiCol_Tab] = bgDark;
	colors[ImGuiCol_TabHovered] = EditorPalette::RAISED_HOVERED;
	colors[ImGuiCol_TabSelected] = bgPanel;
	colors[ImGuiCol_TabSelectedOverline] = accent;
	colors[ImGuiCol_TabDimmed] = bgDark;                                   // barra de pestanas sin foco
	colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.110f, 0.110f, 0.119f, 1.00f);
	colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);

	// Separadores, tiradores de redimensionado y barras de scroll
	colors[ImGuiCol_Separator] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
	colors[ImGuiCol_SeparatorHovered] = withAlpha(accent, 0.78f);
	colors[ImGuiCol_SeparatorActive] = accent;
	colors[ImGuiCol_ResizeGrip] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
	colors[ImGuiCol_ResizeGripHovered] = withAlpha(accent, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = withAlpha(accent, 0.95f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.26f, 0.28f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.34f, 0.37f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.43f, 0.43f, 0.47f, 1.00f);
	colors[ImGuiCol_TreeLines] = ImVec4(1.0f, 1.0f, 1.0f, 0.13f);

	// Tablas (ventana Resources) y graficas (Configuration)
	colors[ImGuiCol_TableHeaderBg] = EditorPalette::RAISED;
	colors[ImGuiCol_TableBorderStrong] = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);
	colors[ImGuiCol_TableBorderLight] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
	colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
	colors[ImGuiCol_PlotLines] = accentHovered;
	colors[ImGuiCol_PlotLinesHovered] = EditorPalette::LEVEL_WARNING;
	colors[ImGuiCol_PlotHistogram] = accent;
	colors[ImGuiCol_PlotHistogramHovered] = accentHovered;

	// Drag & drop, navegacion con teclado y fondos atenuados de las ventanas modales
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 0.78f, 0.25f, 0.95f); // ambar: destaca sobre el azul de seleccion
	colors[ImGuiCol_NavCursor] = accentHovered;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

	// Con multi-viewport una ventana sacada fuera de la principal pasa a ser una ventana real del
	// sistema operativo: no puede tener esquinas redondeadas ni transparencia (se verian en negro).
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.PopupRounding = 0.0f;
		colors[ImGuiCol_WindowBg].w = 1.0f;
		colors[ImGuiCol_PopupBg].w = 1.0f;
	}

	// ---------------------------------------------------------------------------------
	// Gizmo 3D (ImGuizmo): colores de eje mas luminosos que los de serie, que sobre un fondo
	// oscuro apenas se distinguen (sobre todo el azul del eje Z). Convenio habitual: X rojo, Y verde, Z azul.
	// ---------------------------------------------------------------------------------
	ImGuizmo::Style& gizmoStyle = ImGuizmo::GetStyle();
	const ImVec4 axisX = ImVec4(0.92f, 0.30f, 0.32f, 1.00f);
	const ImVec4 axisY = ImVec4(0.50f, 0.82f, 0.26f, 1.00f);
	const ImVec4 axisZ = ImVec4(0.26f, 0.56f, 0.97f, 1.00f);
	gizmoStyle.Colors[ImGuizmo::DIRECTION_X] = axisX;
	gizmoStyle.Colors[ImGuizmo::DIRECTION_Y] = axisY;
	gizmoStyle.Colors[ImGuizmo::DIRECTION_Z] = axisZ;
	gizmoStyle.Colors[ImGuizmo::PLANE_X] = withAlpha(axisX, 0.40f);
	gizmoStyle.Colors[ImGuizmo::PLANE_Y] = withAlpha(axisY, 0.40f);
	gizmoStyle.Colors[ImGuizmo::PLANE_Z] = withAlpha(axisZ, 0.40f);
	gizmoStyle.Colors[ImGuizmo::SELECTION] = ImVec4(1.00f, 0.80f, 0.20f, 1.00f); // eje bajo el raton / en uso

	// Los ejes apuntan siempre en su sentido positivo (como en Unity / Unreal). Por defecto ImGuizmo
	// los voltea hacia la camara, y entonces la flecha deja de indicar hacia donde crece la coordenada.
	ImGuizmo::AllowAxisFlip(false);
}

bool ModuleUI::PreUpdate()
{
#ifdef _DEBUG
	ZoneScoped;
#endif
	SDL_Window* sdlWindow = Engine::GetInstance().window->GetWindow();
	bool relativeMouse = (sdlWindow != nullptr) && SDL_GetWindowRelativeMouseMode(sdlWindow);

	ImGuiIO& io = ImGui::GetIO();

	if (relativeMouse)
	{
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;

	}
	else
	{
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
	}

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	// ImGuizmo reinicia su estado interno cada frame; tiene que ir justo despues de ImGui::NewFrame()
	ImGuizmo::BeginFrame();

	return true;
}

bool ModuleUI::Update(float dt)
{
#ifdef _DEBUG
	ZoneScoped;
#endif
	(void)dt;

	// Operaciones sobre assets pedidas el frame anterior (borrar, reimportar...): se hacen
	// antes de dibujar nada para que ImGui nunca use una textura ya liberada
	ProcessPendingAssetOperations();

	bool ret = DrawMainMenuBar();

	// Barra de Play / Pause / Stop (debajo del menu, antes del DockSpace para que le quite sitio)
	DrawToolbar();

	// Enable a DockSpace over the entire main viewport so windows can be docked properly
	ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, mainViewport);

	// Disposición inicial fija mediante DockBuilder (se ejecuta la primera vez o al resetear)
	static bool layoutInitialized = false;
	if (!layoutInitialized || resetLayout)
	{
		ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);
		if (resetLayout || node == nullptr || node->ChildNodes[0] == nullptr)
		{
			ImGui::DockBuilderRemoveNode(dockspace_id);
			ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockspace_id, mainViewport->WorkSize);

			ImGuiID dock_main_id = dockspace_id;
			ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.20f, nullptr, &dock_main_id);
			ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.30f, nullptr, &dock_main_id);
			//ImGuiID dock_right_down = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.45f, nullptr, &dock_right);
			ImGuiID dock_down = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.30f, nullptr, &dock_main_id);
			ImGuiID dock_game = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.50f, nullptr, &dock_main_id);

			ImGui::DockBuilderDockWindow("Hierarchy", dock_left);
			ImGui::DockBuilderDockWindow("Scene", dock_main_id);
			ImGui::DockBuilderDockWindow("Game", dock_game);
			ImGui::DockBuilderDockWindow("Assets", dock_down);
			ImGui::DockBuilderDockWindow("Console", dock_down);
			ImGui::DockBuilderDockWindow("Resources", dock_down);
			ImGui::DockBuilderDockWindow("Inspector", dock_right);
			ImGui::DockBuilderDockWindow("Configuration", dock_right);

			ImGui::DockBuilderFinish(dockspace_id);
		}
		layoutInitialized = true;
		resetLayout = false;
	}

	sceneOrHierarchyFocused = false;

	DrawScenePanel();
	DrawGamePanel();
	DrawHierarchyPanel();
	DrawInspectorPanel();
	DrawConfigurationPanel();
	DrawConsolePanel();
	DrawAssetsPanel();
	DrawResourcesPanel();
	DrawDepthBufferPanel();
	DrawAboutPanel();

	// Ventanas modales (fuera de cualquier ventana para que OpenPopup y BeginPopupModal coincidan)
	DrawSceneFilePopups();
	DrawAssetsPopups();

	HandleShortcuts();

	// Atajo de teclado: Ctrl+D para duplicar el GameObject seleccionado si Scene o Hierarchy tienen el foco
	if (sceneOrHierarchyFocused && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
	{
		Scene* scene = Engine::GetInstance().scene.get();
		if (scene && scene->GetSelected())
		{
			scene->DuplicateGameObject(scene->GetSelected());
		}
	}

	// Atajo de teclado: Supr (Delete) para eliminar el GameObject seleccionado si Scene o Hierarchy tienen el foco
	if (sceneOrHierarchyFocused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete))
	{
		Scene* scene = Engine::GetInstance().scene.get();
		if (scene && scene->GetSelected())
		{
			scene->DestroyGameObject(scene->GetSelected());
		}
	}

	return ret;
}

bool ModuleUI::DrawMainMenuBar()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	bool continueRunning = true;

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			Scene* scene = Engine::GetInstance().scene.get();
			const bool playing = scene && scene->IsInPlayMode();

			if (ImGui::MenuItem("New Scene") && scene)
			{
				scene->RequestNewScene();
			}

			// Lista de escenas guardadas dentro de Assets
			if (ImGui::BeginMenu("Open Scene"))
			{
				std::vector<std::string> files;
				FileSystem::ListFilesRecursive(FileSystem::ASSETS_DIR, files);
				bool anyScene = false;
				for (const std::string& file : files)
				{
					if (!ResourceManager::IsSceneFile(file)) continue;
					anyScene = true;
					if (ImGui::MenuItem(file.c_str()) && scene)
					{
						scene->RequestLoadScene(file);
					}
				}
				if (!anyScene) ImGui::TextDisabled("No hay escenas guardadas en Assets");
				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Load Street Environment") && scene)
			{
				scene->RequestDefaultScene();
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !playing) && scene)
			{
				if (scene->GetCurrentScenePath().empty()) openSaveScenePopup = true;
				else scene->SaveScene(scene->GetCurrentScenePath());
			}
			if (ImGui::MenuItem("Save Scene As...", nullptr, false, !playing))
			{
				openSaveScenePopup = true;
			}
			if (playing) ImGui::TextDisabled("(Para guardar, pulsa Stop)");

			ImGui::Separator();
			if (ImGui::MenuItem("Exit", "Alt+F4"))
			{
				LOG("[UI] Solicitud de salida desde el menú File -> Exit");
				continueRunning = false;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Scene", nullptr, &showScene);
			ImGui::MenuItem("Game", nullptr, &showGame);
			ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
			ImGui::MenuItem("Inspector", nullptr, &showInspector);
			ImGui::MenuItem("Console", nullptr, &showConsole);
			ImGui::MenuItem("Configuration", nullptr, &showConfiguration);
			ImGui::MenuItem("Assets", nullptr, &showAssets);
			ImGui::MenuItem("Resources", nullptr, &showResources);
			ImGui::Separator();

			Render* render = Engine::GetInstance().render.get();
			if (render)
			{
				RenderSettings rs = render->GetSettings();
				// Use a local bool for ImGui to manipulate directly
				bool showDepth = rs.showDepthBuffer;
				if (ImGui::MenuItem("Z-Buffer (Depth)", nullptr, &showDepth))
				{
					rs.showDepthBuffer = showDepth;
					render->SetSettings(rs); // Force immediate update back to Engine
				}
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout"))
			{
				resetLayout = true;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("GameObject"))
		{
			Scene* scene = Engine::GetInstance().scene.get();
			GameObject* selectedObj = scene ? scene->GetSelected() : nullptr;
			if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, selectedObj != nullptr))
			{
				if (scene && selectedObj)
				{
					scene->DuplicateGameObject(selectedObj);
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Create Empty"))
			{
				if (scene)
				{
					SelectGameObject(scene->CreateEmptyGameObject());
				}
			}
			if (ImGui::MenuItem("Camera"))
			{
				if (scene)
				{
					SelectGameObject(scene->CreateCamera());
				}
			}
			ImGui::SeparatorText("3D Primitives");
			const PrimitiveType types[] = {
				PrimitiveType::Cube,
				PrimitiveType::Sphere,
				PrimitiveType::Plane,
				PrimitiveType::Cylinder,
				PrimitiveType::Pyramid
			};
			for (PrimitiveType type : types)
			{
				if (ImGui::MenuItem(GeometryGenerator::GetName(type)))
				{
					if (scene)
					{
						SelectGameObject(scene->CreatePrimitive(type));
					}
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("GitHub Documentation"))
			{
				OpenWebURL(REPO_URL_DOCS);
			}
			if (ImGui::MenuItem("Report a Bug"))
			{
				OpenWebURL(REPO_URL_ISSUES);
			}
			if (ImGui::MenuItem("Download Latest"))
			{
				OpenWebURL(REPO_URL_RELEASES);
			}
			ImGui::Separator();
			ImGui::MenuItem("About", nullptr, &showAbout);
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	return continueRunning;
}

void ModuleUI::SelectGameObject(GameObject* object)
{
	if (Scene* scene = Engine::GetInstance().scene.get())
	{
		scene->SetSelected(object);
	}
	inspectorShowsAsset = false;
}

void ModuleUI::DrawToolbar()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	Scene* scene = Engine::GetInstance().scene.get();
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float height = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
	const PlayState state = scene ? scene->GetPlayState() : PlayState::Stopped;

	// En modo Play la barra cambia de color, como en Unity, para que se note que estamos simulando.
	// En edicion usa el mismo tono que la barra de menu: las dos forman una unica cabecera.
	const bool playMode = (state != PlayState::Stopped);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, playMode ? ImVec4(0.09f, 0.19f, 0.34f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
	if (ImGui::BeginViewportSideBar("##MainToolbar", viewport, ImGuiDir_Up, height, flags) && scene)
	{
		// Nombre de la escena a la izquierda
		const std::string& scenePath = scene->GetCurrentScenePath();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled(ICON_FA_CLAPPERBOARD "  Escena: %s", scenePath.empty() ? "(sin guardar)" : FileSystem::GetFileName(scenePath).c_str());

		// Botones centrados (o justo despues del nombre de la escena si no caben).
		// Solo llevan icono, asi que son mas estrechos que los antiguos botones de texto.
		const ImVec2 buttonSize(ImGui::GetFrameHeight() * 1.75f, 0.0f);
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float groupWidth = buttonSize.x * 3.0f + spacing * 2.0f;
		const float textEndX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + spacing * 2.0f;
		ImGui::SameLine(std::max(textEndX, (ImGui::GetWindowWidth() - groupWidth) * 0.5f));

		// Play / Stop: iconos de Font Awesome. El "##..." da al boton un ID estable aunque no tenga texto.
		if (state == PlayState::Stopped)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, EditorPalette::STATE_PLAY);
			if (ImGui::Button(ICON_FA_PLAY "##Play", buttonSize)) scene->Play();
			ImGui::PopStyleColor();
			ImGui::SetItemTooltip("Play: empieza la simulacion (Ctrl+P).\nAl parar se recupera el estado actual de la escena.");
		}
		else
		{
			if (ColoredButton(ICON_FA_STOP "##Stop", buttonSize, EditorPalette::STATE_STOP, EditorPalette::STATE_STOP_HOVERED, EditorPalette::STATE_STOP)) scene->Stop();
			ImGui::SetItemTooltip("Stop: para la simulacion y restaura la escena como estaba al pulsar Play (Ctrl+P)");
		}

		// Pause (resaltado mientras esta en pausa: volver a pulsarlo reanuda la simulacion)
		ImGui::SameLine();
		ImGui::BeginDisabled(state == PlayState::Stopped);
		const bool paused = (state == PlayState::Paused);
		const bool pausePressed = paused
			? ColoredButton(ICON_FA_PAUSE "##Pause", buttonSize, EditorPalette::STATE_PAUSE, EditorPalette::STATE_PAUSE_HOVERED, EditorPalette::STATE_PAUSE)
			: ImGui::Button(ICON_FA_PAUSE "##Pause", buttonSize);
		if (pausePressed) scene->Pause();
		ImGui::EndDisabled();
		IconTooltip(paused ? "Reanudar la simulacion (Ctrl+Shift+P)" : "Pause: congela la simulacion (Ctrl+Shift+P)");

		// Step (solo en pausa)
		ImGui::SameLine();
		ImGui::BeginDisabled(state != PlayState::Paused);
		if (ImGui::Button(ICON_FA_FORWARD_STEP "##Step", buttonSize)) scene->Step();
		ImGui::EndDisabled();
		IconTooltip("Step: avanza un unico frame de la simulacion (solo en pausa)");

		// Estado y tiempo de juego
		ImGui::SameLine();
		const char* stateText = (state == PlayState::Playing) ? "PLAY" : (state == PlayState::Paused) ? "PAUSA" : "EDICION";
		const ImVec4 stateColor = (state == PlayState::Playing) ? EditorPalette::STATE_PLAY :
			(state == PlayState::Paused) ? EditorPalette::LEVEL_WARNING : EditorPalette::ICON_NEUTRAL;
		ImGui::TextColored(stateColor, "  %s", stateText);
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_FA_CLOCK "  %.2f s", scene->GetGameTime());
		ImGui::SetItemTooltip("Tiempo de juego (segundos de simulacion)");

		// Escala de tiempo
		ImGui::SameLine();
		float timeScale = scene->GetTimeScale();
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::DragFloat("Escala de tiempo", &timeScale, 0.01f, 0.0f, 10.0f, "x%.2f"))
		{
			scene->SetTimeScale(timeScale);
		}
	}
	ImGui::End();

	ImGui::PopStyleColor();
}

void ModuleUI::DrawSceneFilePopups()
{
	Scene* scene = Engine::GetInstance().scene.get();

	if (openSaveScenePopup)
	{
		ImGui::OpenPopup("Guardar escena");
		openSaveScenePopup = false;
	}
	if (ImGui::BeginPopupModal("Guardar escena", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("La escena se guardara en %s/", FileSystem::SCENES_DIR);
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		const bool enterPressed = ImGui::InputText("Nombre", saveSceneName, sizeof(saveSceneName), ImGuiInputTextFlags_EnterReturnsTrue);

		const std::string sceneName = saveSceneName;
		const bool validName = !sceneName.empty() && sceneName.find_first_of("/\\:*?\"<>|") == std::string::npos;

		ImGui::BeginDisabled(!validName || scene == nullptr);
		if (ImGui::Button("Guardar", ImVec2(120.0f, 0.0f)) || (enterPressed && validName && scene != nullptr))
		{
			scene->SaveScene(FileSystem::Join(FileSystem::SCENES_DIR, sceneName + FileSystem::SCENE_EXTENSION));
			InvalidateAssetsCache();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancelar", ImVec2(120.0f, 0.0f)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void ModuleUI::HandleShortcuts()
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput) return;

	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;

	// Ctrl+S: guardar escena
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !scene->IsInPlayMode())
	{
		if (scene->GetCurrentScenePath().empty()) openSaveScenePopup = true;
		else scene->SaveScene(scene->GetCurrentScenePath());
	}

	// Ctrl+P: Play / Stop      Ctrl+Shift+P: Pause
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false))
	{
		if (io.KeyShift) scene->Pause();
		else if (scene->IsInPlayMode()) scene->Stop();
		else scene->Play();
	}

	// W / E / R: herramienta del gizmo (Mover / Rotar / Escalar), como en Unity.
	// Solo con la vista Scene bajo el raton o con Scene / Hierarchy enfocadas, y nunca:
	//  - mientras se pilota la camara con el clic derecho (W y E son teclas de movimiento),
	//  - a mitad de un arrastre del gizmo (cambiaria la operacion sobre la marcha),
	//  - con Ctrl o Alt pulsados (son otros atajos).
	GameObject* editorCamera = scene->GetEditorCameraObject();
	const CameraComponent* camera = editorCamera ? editorCamera->GetComponent<CameraComponent>() : nullptr;
	const bool flyingCamera = (camera != nullptr && camera->IsControllingCamera()) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
	if ((sceneViewHovered || sceneOrHierarchyFocused) && !flyingCamera && !gizmoInUse && !io.KeyCtrl && !io.KeyAlt)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) gizmoOperation = GizmoOperation::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) gizmoOperation = GizmoOperation::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) gizmoOperation = GizmoOperation::Scale;
	}
}

void ModuleUI::DrawSceneOverlayBar()
{
	Render* render = Engine::GetInstance().render.get();
	Scene* scene = Engine::GetInstance().scene.get();
	if (!render || !scene) return;

	// Herramientas del gizmo de transformacion (Mover / Rotar / Escalar, Local / Global)
	DrawGizmoToolbar();
	ImGui::SameLine();

	// Menu "Gizmos" con las opciones de depuracion de la vista Scene ("###" fija el ID del boton)
	if (ImGui::Button(ICON_FA_EYE "  Gizmos###SceneGizmosButton")) ImGui::OpenPopup("SceneGizmosPopup");
	if (ImGui::BeginPopup("SceneGizmosPopup"))
	{
		RenderSettings rs = render->GetSettings();
		bool changed = false;
		ImGui::SeparatorText("Frustum culling");
		changed |= ImGui::Checkbox("Culling con la camara del editor", &rs.editorFrustumCulling);
		changed |= ImGui::Checkbox("Vista previa del culling de la Main Camera", &rs.cullingPreview);
		ImGui::SetItemTooltip("La vista Scene solo dibuja lo que ve la Main Camera:\nmuevela y veras aparecer/desaparecer los objetos");
		bool useOctree = scene->IsUsingOctree();
		if (ImGui::Checkbox("Usar octree (culling y picking)", &useOctree)) scene->SetUseOctree(useOctree);

		ImGui::SeparatorText("Gizmos");
		changed |= ImGui::Checkbox("AABB de todas las mallas (verde/rojo)", &rs.showAABBs);
		changed |= ImGui::Checkbox("AABB del seleccionado", &rs.showSelectedAABB);
		changed |= ImGui::Checkbox("Frustum de la Main Camera", &rs.showCameraFrustum);
		changed |= ImGui::Checkbox("Nodos del octree", &rs.showOctree);
		changed |= ImGui::Checkbox("Rayo del raton (picking)", &rs.showPickRay);
		changed |= ImGui::Checkbox("Grid", &rs.showGrid);
		changed |= ImGui::Checkbox("Ejes", &rs.showAxis);
		if (changed) render->SetSettings(rs);
		ImGui::EndPopup();
	}

	// Estadisticas de culling del ultimo frame
	const RenderStats& stats = render->GetStats();
	ImGui::SameLine();
	if (stats.sceneCulling)
	{
		ImGui::TextDisabled("Dibujados %d / %d  |  Nodos octree visitados %d  |  AABB probadas %d",
			stats.sceneDrawn, stats.sceneCandidates, stats.sceneQuery.nodesVisited, stats.sceneQuery.objectsTested);
	}
	else
	{
		ImGui::TextDisabled("Dibujados %d / %d (sin culling)", stats.sceneDrawn, stats.sceneCandidates);
	}

	if (scene->IsInPlayMode())
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_GAMEPAD "  MODO PLAY");
	}
}

void ModuleUI::DrawGizmoToolbar()
{
	// Boton "toggle": el de la herramienta activa se pinta con el color de acento
	const auto toolButton = [](const char* label, bool active, const char* tooltip) -> bool
	{
		const bool pressed = active
			? ColoredButton(label, ImVec2(0.0f, 0.0f), EditorPalette::ACCENT, EditorPalette::ACCENT_HOVERED, EditorPalette::ACCENT_ACTIVE)
			: ImGui::Button(label);
		ImGui::SetItemTooltip("%s", tooltip);
		return pressed;
	};

	// Los tres botones van casi pegados para que se lean como un grupo
	const float groupSpacing = 2.0f;
	if (toolButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT "##GizmoTranslate", gizmoOperation == GizmoOperation::Translate,
		"Mover (W)\nCtrl mientras arrastras: ajuste a rejilla")) gizmoOperation = GizmoOperation::Translate;
	ImGui::SameLine(0.0f, groupSpacing);
	if (toolButton(ICON_FA_ARROWS_ROTATE "##GizmoRotate", gizmoOperation == GizmoOperation::Rotate,
		"Rotar (E)\nCtrl mientras arrastras: pasos de 15 grados")) gizmoOperation = GizmoOperation::Rotate;
	ImGui::SameLine(0.0f, groupSpacing);
	if (toolButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER "##GizmoScale", gizmoOperation == GizmoOperation::Scale,
		"Escalar (R)\nCtrl mientras arrastras: pasos de 0.1")) gizmoOperation = GizmoOperation::Scale;

	// Espacio de referencia de los ejes. El boton tiene un ancho fijo (el de la etiqueta mas larga)
	// para que el resto de la barra no se mueva al alternar entre "Local" y "Global".
	ImGui::SameLine();
	const char* localLabel = ICON_FA_CUBE "  Local###GizmoSpace";
	const char* globalLabel = ICON_FA_GLOBE "  Global###GizmoSpace";
	const float labelWidth = std::max(ImGui::CalcTextSize(localLabel, nullptr, true).x, ImGui::CalcTextSize(globalLabel, nullptr, true).x);
	const ImVec2 spaceButtonSize(labelWidth + ImGui::GetStyle().FramePadding.x * 2.0f, 0.0f);
	const bool localSpace = (gizmoSpace == GizmoSpace::Local);
	if (ImGui::Button(localSpace ? localLabel : globalLabel, spaceButtonSize))
	{
		gizmoSpace = localSpace ? GizmoSpace::World : GizmoSpace::Local;
	}
	ImGui::SetItemTooltip("Espacio del gizmo (clic para alternar):\n  Local: ejes del propio objeto\n  Global: ejes del mundo\nLa escala se aplica siempre en los ejes locales.");
}

void ModuleUI::DrawTransformGizmo(const ImVec2& viewportPos, const ImVec2& viewportSize, CameraComponent* camera)
{
	Scene* scene = Engine::GetInstance().scene.get();
	GameObject* selected = scene ? scene->GetSelected() : nullptr;
	TransformComponent* transform = selected ? selected->GetComponent<TransformComponent>() : nullptr;
	if (camera == nullptr || transform == nullptr || viewportSize.x < 1.0f || viewportSize.y < 1.0f) return;

	// ---------------------------------------------------------------------------------
	// 1) Donde se dibuja: en el draw list de la ventana Scene (queda por encima de la imagen del
	//    framebuffer y recortado por la ventana) y dentro del rectangulo EXACTO de esa imagen.
	//    Son coordenadas de pantalla, las mismas en las que ImGui da la posicion del raton, asi
	//    que funciona igual con la ventana acoplada, flotante o fuera de la ventana principal.
	// ---------------------------------------------------------------------------------
	ImGuizmo::SetOrthographic(false); // la camara del editor siempre es de perspectiva
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

	// ---------------------------------------------------------------------------------
	// 2) Matrices. View y Projection son las mismas con las que Render::RenderScene dibuja la
	//    vista Scene (el aspect ratio sale del tamano en pixeles enteros del framebuffer): si no
	//    coincidieran, el gizmo no caeria encima del objeto. GLM guarda las matrices por columnas,
	//    igual que OpenGL e ImGuizmo, asi que glm::value_ptr las pasa sin convertir nada.
	// ---------------------------------------------------------------------------------
	const float aspectRatio = static_cast<float>(static_cast<int>(viewportSize.x)) / static_cast<float>(static_cast<int>(viewportSize.y));
	const glm::mat4 view = camera->GetViewMatrix();
	const glm::mat4 projection = camera->GetProjectionMatrix(aspectRatio);
	glm::mat4 world = selected->GetWorldMatrix(); // matriz Model en espacio MUNDO: ImGuizmo la modifica

	// Objeto detras de la camara (o pegado al plano cercano): no hay gizmo que dibujar. ImGuizmo hace
	// esta misma comprobacion dentro de Manipulate(), pero sale con un 'return' ANTES de cerrar el clip
	// rect que acaba de abrir en el draw list (PushClipRect sin su PopClipRect) y deja ademas el estado
	// de IsOver() del frame anterior. Se descarta aqui el caso para no llegar nunca a ese camino.
	const glm::vec4 originClip = projection * view * glm::vec4(glm::vec3(world[3]), 1.0f);
	if (originClip.z < 0.001f && !ImGuizmo::IsUsing()) return;

	// ---------------------------------------------------------------------------------
	// 3) Operacion, espacio y ajuste a rejilla (Ctrl)
	// ---------------------------------------------------------------------------------
	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
	float snapStep = GIZMO_SNAP_TRANSLATE;
	if (gizmoOperation == GizmoOperation::Rotate)
	{
		operation = ImGuizmo::ROTATE;
		snapStep = GIZMO_SNAP_ROTATE;
	}
	else if (gizmoOperation == GizmoOperation::Scale)
	{
		operation = ImGuizmo::SCALE;
		snapStep = GIZMO_SNAP_SCALE;
	}

	// Una escala en ejes del mundo deformaria (shear) un objeto rotado: la escala es siempre local
	const bool localSpace = (gizmoSpace == GizmoSpace::Local) || (gizmoOperation == GizmoOperation::Scale);
	const ImGuizmo::MODE mode = localSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

	const float snapValues[3] = { snapStep, snapStep, snapStep };
	const bool snapping = ImGui::GetIO().KeyCtrl;

	const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, mode,
		glm::value_ptr(world), nullptr, snapping ? snapValues : nullptr);

	// ---------------------------------------------------------------------------------
	// 4) Prioridad de input. Estos dos flags son los que consultan DrawScenePanel (para no lanzar
	//    el raycast de seleccion) y la camara del editor (para no moverse) en este clic.
	// ---------------------------------------------------------------------------------
	gizmoInUse = ImGuizmo::IsUsing();
	gizmoHovered = ImGuizmo::IsOver();

	if (!changed || !gizmoInUse) return;

	// ---------------------------------------------------------------------------------
	// 5) Aplicar el resultado. ImGuizmo devuelve la nueva matriz en espacio MUNDO, pero el
	//    TransformComponent guarda posicion / rotacion / escala LOCALES (relativas al padre):
	//        world = parentWorld * local   ->   local = inverse(parentWorld) * world
	// ---------------------------------------------------------------------------------
	const GameObject* parent = selected->GetParent();
	const glm::mat4 parentWorld = parent ? parent->GetWorldMatrix() : glm::mat4(1.0f);
	const glm::mat4 local = glm::inverse(parentWorld) * world;

	glm::vec3 position(0.0f), rotation(0.0f), scale(1.0f);
	if (!transform->DecomposeLocalMatrix(local, position, rotation, scale)) return;

	// Solo se escribe el canal que se esta manipulando. Descomponer una matriz y volver a componerla
	// acumula error numerico: si al mover se reescribieran tambien rotacion y escala, sus valores
	// irian "bailando" en el Inspector (y los angulos de Euler pueden saltar a otra terna equivalente).
	switch (gizmoOperation)
	{
	case GizmoOperation::Translate: transform->SetPosition(position); break;
	case GizmoOperation::Rotate:    transform->SetRotation(rotation); break;
	case GizmoOperation::Scale:     transform->SetScale(scale);       break;
	}
}

void ModuleUI::DrawScenePanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	Render* render = Engine::GetInstance().render.get();
	Scene* scene = Engine::GetInstance().scene.get();
	GameObject* editorCamera = scene ? scene->GetEditorCameraObject() : nullptr;
	CameraComponent* camComp = editorCamera ? editorCamera->GetComponent<CameraComponent>() : nullptr;

	// El estado del gizmo se recalcula cada frame: si este frame no llega a dibujarse (ventana
	// oculta, nada seleccionado...) no puede quedarse "enganchado" bloqueando la camara o la seleccion
	gizmoHovered = false;
	gizmoInUse = false;
	sceneViewHovered = false;

	if (!showScene)
	{
		if (render) render->SetViewportVisible(false);
		if (camComp)
		{
			camComp->SetViewportHovered(false);
			camComp->SetGizmoState(false, false);
		}
		return;
	}

	bool visible = ImGui::Begin("Scene", &showScene);
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		sceneOrHierarchyFocused = true;
	}

	if (!visible)
	{
		if (render) render->SetViewportVisible(false);
		if (camComp)
		{
			camComp->SetViewportHovered(false);
			camComp->SetGizmoState(false, false);
		}
		ImGui::End();
		return;
	}

	if (render) render->SetViewportVisible(true);

	DrawSceneOverlayBar();

	ImVec2 viewportSize = ImGui::GetContentRegionAvail();
	if (render && viewportSize.x > 0 && viewportSize.y > 0)
	{
		render->SetViewportSize((int)viewportSize.x, (int)viewportSize.y);
	}

	unsigned int sceneTexture = render ? render->GetSceneTexture() : 0;
	bool imageHovered = false;
	if (sceneTexture != 0 && viewportSize.x > 0 && viewportSize.y > 0)
	{
		ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
		ImGui::Image((ImTextureID)(intptr_t)sceneTexture, viewportSize, ImVec2(0, 1), ImVec2(1, 0));
		imageHovered = ImGui::IsItemHovered();
		// El clic se guarda ahora (IsItemClicked se refiere al ultimo widget: la imagen) pero se
		// procesa mas abajo, despues del gizmo, que es quien decide si el clic es suyo.
		const bool imageClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

		Input* input = Engine::GetInstance().input.get();
		bool altHeld = input && (input->GetKey(SDL_SCANCODE_LALT) == KEY_DOWN || input->GetKey(SDL_SCANCODE_LALT) == KEY_REPEAT);

		// Soltar assets sobre la vista Scene:
		//   modelo  -> se instancia donde apunta el raton (sobre el objeto que haya debajo)
		//   malla   -> se crea un GameObject con esa malla
		//   textura -> se aplica al objeto que hay debajo del raton
		if (ImGui::BeginDragDropTarget() && scene && camComp)
		{
			const ImVec2 mousePos = ImGui::GetMousePos();
			const Ray ray = camComp->ScreenPointToRay(mousePos.x - cursorScreenPos.x, mousePos.y - cursorScreenPos.y, viewportSize.x, viewportSize.y);
			float hitDistance = 0.0f;

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_MODEL))
			{
				const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(payload->Data, payload->DataSize);
				GameObject* hit = scene->PickObject(ray, &hitDistance);
				const glm::vec3 position = hit ? ray.GetPoint(hitDistance) : ray.GetPoint(10.0f);
				if (GameObject* instance = scene->InstantiateModel(asset.uid))
				{
					if (TransformComponent* transform = instance->GetComponent<TransformComponent>()) transform->SetPosition(position);
					SelectGameObject(instance);
				}
			}
			else if (const ImGuiPayload* meshPayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_MESH))
			{
				const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(meshPayload->Data, meshPayload->DataSize);
				GameObject* hit = scene->PickObject(ray, &hitDistance);
				const glm::vec3 position = hit ? ray.GetPoint(hitDistance) : ray.GetPoint(10.0f);

				GameObject* created = scene->CreateEmptyGameObject();
				const Resource* meshResource = Engine::GetInstance().resources ? Engine::GetInstance().resources->GetResource(asset.uid) : nullptr;
				if (meshResource) created->SetName(scene->MakeUniqueName(meshResource->GetName()));
				created->AddComponent(std::make_unique<MeshComponent>(created, asset.uid));
				created->AddComponent(std::make_unique<MaterialComponent>(created));
				if (TransformComponent* transform = created->GetComponent<TransformComponent>()) transform->SetPosition(position);
				SelectGameObject(created);
			}
			else if (const ImGuiPayload* texturePayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_TEXTURE))
			{
				const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(texturePayload->Data, texturePayload->DataSize);
				if (GameObject* hit = scene->PickObject(ray, &hitDistance))
				{
					ImporterManager::AssignTextureToHierarchy(asset.uid, hit);
					SelectGameObject(hit);
				}
				else
				{
					LOG_WARNING("[UI] Suelta la textura encima de un objeto con malla");
				}
			}
			else if (const ImGuiPayload* scenePayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_SCENE))
			{
				const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(scenePayload->Data, scenePayload->DataSize);
				scene->RequestLoadScene(asset.path);
			}
			ImGui::EndDragDropTarget();
		}

		// Gizmo de transformacion del objeto seleccionado, acotado al rectangulo de la imagen.
		// Se dibuja ANTES de procesar el clic para saber en este mismo frame si el raton esta sobre el.
		DrawTransformGizmo(cursorScreenPos, viewportSize, camComp);

		// Selección por Raycast al hacer clic izquierdo sobre el viewport (sin Alt para no interferir con la órbita).
		// Si el clic cae sobre el gizmo (o empieza a arrastrarlo) es del gizmo: no se lanza el rayo, porque
		// seleccionaria el objeto que hay detras o deseleccionaria el actual.
		if (imageClicked && !altHeld && camComp && !gizmoHovered && !gizmoInUse)
		{
			ImVec2 mousePos = ImGui::GetMousePos();
			float localMouseX = mousePos.x - cursorScreenPos.x;
			float localMouseY = mousePos.y - cursorScreenPos.y;
			camComp->PerformRaycast(localMouseX, localMouseY, viewportSize.x, viewportSize.y);
			inspectorShowsAsset = false;
		}
	}

	sceneViewHovered = imageHovered || ImGui::IsWindowHovered();

	if (camComp)
	{
		// La camara lee este estado en su proximo Update (Scene se actualiza antes que la UI)
		camComp->SetGizmoState(gizmoHovered, gizmoInUse);

		bool mouseDisabledForCamera = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) != 0;
		if (camComp->IsControllingCamera())
		{
			ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			camComp->SetViewportHovered(true);
		}
		else if (mouseDisabledForCamera)
		{
			camComp->SetViewportHovered(true);
		}
		else
		{
			camComp->SetViewportHovered(imageHovered || ImGui::IsWindowHovered());
		}
	}

	ImGui::End();
}

void ModuleUI::DrawGamePanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	Render* render = Engine::GetInstance().render.get();

	if (!showGame)
	{
		if (render) render->SetGameViewportVisible(false);
		return;
	}

	bool visible = ImGui::Begin("Game", &showGame);
	if (!visible)
	{
		if (render) render->SetGameViewportVisible(false);
		ImGui::End();
		return;
	}

	if (render) render->SetGameViewportVisible(true);

	Scene* scene = Engine::GetInstance().scene.get();
	GameObject* gameCam = scene ? scene->GetGameCameraObject() : nullptr;

	// Estadisticas del frustum culling de la Main Camera
	if (render)
	{
		const RenderStats& stats = render->GetStats();
		ImGui::TextDisabled("Main Camera: dibujados %d / %d %s", stats.gameDrawn, stats.gameCandidates,
			stats.gameCulling ? "(frustum culling ON)" : "(frustum culling OFF)");
	}

	ImVec2 viewportSize = ImGui::GetContentRegionAvail();
	if (render && viewportSize.x > 0 && viewportSize.y > 0)
	{
		render->SetGameViewportSize((int)viewportSize.x, (int)viewportSize.y);
	}

	const CameraComponent* gameCamComponent = gameCam ? gameCam->GetComponent<CameraComponent>() : nullptr;
	if (!gameCam || !gameCam->IsActiveInHierarchy() || !gameCamComponent || !gameCamComponent->IsActive())
	{
		ImGui::TextDisabled("Display 1: No cameras rendering.");
		ImGui::End();
		return;
	}

	unsigned int gameTexture = render ? render->GetGameTexture() : 0;
	if (gameTexture != 0 && viewportSize.x > 0 && viewportSize.y > 0)
	{
		ImGui::Image((ImTextureID)(intptr_t)gameTexture, viewportSize, ImVec2(0, 1), ImVec2(1, 0));
	}

	ImGui::End();
}

void ModuleUI::DrawHierarchyPanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!showHierarchy) return;

	if (ImGui::Begin("Hierarchy", &showHierarchy))
	{
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			sceneOrHierarchyFocused = true;
		}

		Scene* scene = Engine::GetInstance().scene.get();
		GameObject* selected = scene ? scene->GetSelected() : nullptr;

		// Solo el frame en el que cambia la seleccion se abren sus padres (luego se pueden plegar)
		revealSelectionInHierarchy = (selected != lastHierarchySelection);
		lastHierarchySelection = selected;

		if (scene)
		{
			GameObject* root = scene->GetRoot();
			if (root)
			{
				// Copia: dibujar un nodo puede reordenar la lista de hijos (drag & drop)
				const std::vector<GameObject*> rootChildren = root->GetChildren();
				for (GameObject* child : rootChildren)
				{
					if (child && child->GetParent() == root)
					{
						DrawHierarchyNode(child, selected);
					}
				}
			}
		}

		// Zona vacía inferior: soltar aquí reemparenta al nodo raíz (o instancia un modelo en la raíz)
		ImVec2 avail = ImGui::GetContentRegionAvail();
		if (avail.y > 0.0f)
		{
			ImGui::Dummy(avail);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				SelectGameObject(nullptr);
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::GAMEOBJECT))
				{
					uint64_t uid = 0;
					std::memcpy(&uid, payload->Data, sizeof(uid));
					GameObject* droppedNode = scene ? scene->FindByUID(uid) : nullptr;
					if (droppedNode && scene)
					{
						droppedNode->SetParent(scene->GetRoot());
					}
				}
				if (const ImGuiPayload* modelPayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_MODEL))
				{
					const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(modelPayload->Data, modelPayload->DataSize);
					if (scene) SelectGameObject(scene->InstantiateModel(asset.uid));
				}
				ImGui::EndDragDropTarget();
			}

			// Clic derecho en la zona vacia: crear objetos en la raiz
			if (ImGui::BeginPopupContextItem("HierarchyEmptyContext"))
			{
				if (ImGui::MenuItem("Create Empty") && scene) SelectGameObject(scene->CreateEmptyGameObject());
				if (ImGui::MenuItem("Create Camera") && scene) SelectGameObject(scene->CreateCamera());
				if (ImGui::BeginMenu("Create Primitive"))
				{
					const PrimitiveType types[] = { PrimitiveType::Cube, PrimitiveType::Sphere, PrimitiveType::Plane, PrimitiveType::Cylinder, PrimitiveType::Pyramid };
					for (PrimitiveType type : types)
					{
						if (ImGui::MenuItem(GeometryGenerator::GetName(type)) && scene) SelectGameObject(scene->CreatePrimitive(type));
					}
					ImGui::EndMenu();
				}
				ImGui::EndPopup();
			}
		}
	}
	ImGui::End();
}

void ModuleUI::DrawHierarchyNode(GameObject* node, GameObject* selected)
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!node) return;

	Scene* scene = Engine::GetInstance().scene.get();
	const bool hasChildren = !node->GetChildren().empty();

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (selected == node)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	if (!hasChildren)
	{
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	// Si se acaba de seleccionar un objeto que esta dentro de este nodo, se abre para que se vea
	if (revealSelectionInHierarchy && selected != nullptr && selected->IsDescendantOf(node))
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}

	// Icono segun lo que ES el GameObject (por sus componentes), para reconocerlo de un vistazo:
	//   camara -> video | con malla -> cubo | sin malla pero con hijos (modelo / grupo) -> cubos | vacio -> punto
	const char* icon = ICON_FA_CIRCLE_DOT;
	ImVec4 iconColor = EditorPalette::ICON_NEUTRAL;
	if (node->GetComponent<CameraComponent>() != nullptr)
	{
		icon = ICON_FA_VIDEO;
		iconColor = EditorPalette::ICON_CAMERA;
	}
	else if (node->GetComponent<MeshComponent>() != nullptr)
	{
		icon = ICON_FA_CUBE;
		iconColor = EditorPalette::ICON_MESH;
	}
	else if (hasChildren)
	{
		icon = ICON_FA_CUBES;
		iconColor = EditorPalette::ICON_MODEL;
	}

	// Objetos desactivados en gris (icono incluido); el nombre de la Main Camera en azul
	const bool isGameCamera = scene && scene->GetGameCameraObject() == node;
	ImVec4 nameColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	if (!node->IsActive())
	{
		nameColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
		iconColor = nameColor;
	}
	else if (isGameCamera)
	{
		nameColor = ImVec4(0.55f, 0.8f, 1.0f, 1.0f);
	}

	// El nodo se crea sin etiqueta y "icono + nombre" se pinta encima en dos colores (ver DrawIconLabel).
	// La posicion se lee antes de crear el nodo: el texto de un TreeNode empieza tras el hueco de la flecha.
	const ImVec2 nodePos = ImGui::GetCursorScreenPos();
	bool nodeOpen = ImGui::TreeNodeEx((void*)node, flags, "%s", "");
	EditorIcons::DrawIconLabel(ImVec2(nodePos.x + ImGui::GetTreeNodeToLabelSpacing(), nodePos.y), icon, iconColor, node->GetName().c_str(), nameColor);

	// 1. Selección con clic izquierdo
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		SelectGameObject(node);
	}

	// 2. Drag & Drop: Origen de arrastre (se envia el UID, nunca un puntero)
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		const uint64_t uid = static_cast<uint64_t>(node->GetUUID());
		ImGui::SetDragDropPayload(EditorDragDrop::GAMEOBJECT, &uid, sizeof(uid));
		ImGui::Text("%s  Mover '%s'", icon, node->GetName().c_str());
		ImGui::EndDragDropSource();
	}

	// 3. Drag & Drop: Destino de soltado
	//    - GameObject -> pasa a ser hijo de 'node' (reparent)
	//    - Modelo     -> se instancia como hijo de 'node'
	//    - Textura    -> se aplica a 'node' y a sus hijos
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::GAMEOBJECT))
		{
			uint64_t uid = 0;
			std::memcpy(&uid, payload->Data, sizeof(uid));
			GameObject* droppedNode = scene ? scene->FindByUID(uid) : nullptr;
			if (droppedNode && droppedNode != node)
			{
				droppedNode->SetParent(node);
			}
		}
		if (const ImGuiPayload* modelPayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_MODEL))
		{
			const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(modelPayload->Data, modelPayload->DataSize);
			if (scene) SelectGameObject(scene->InstantiateModel(asset.uid, node));
		}
		if (const ImGuiPayload* texturePayload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_TEXTURE))
		{
			const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(texturePayload->Data, texturePayload->DataSize);
			ImporterManager::AssignTextureToHierarchy(asset.uid, node);
		}
		ImGui::EndDragDropTarget();
	}

	// 4. Menú contextual con clic derecho
	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Create Empty Child"))
		{
			if (scene) SelectGameObject(scene->CreateEmptyGameObject(node));
		}
		if (ImGui::MenuItem("Create Camera Child"))
		{
			if (scene) SelectGameObject(scene->CreateCamera(node));
		}
		if (ImGui::BeginMenu("Create Primitive Child"))
		{
			const PrimitiveType types[] = {
				PrimitiveType::Cube,
				PrimitiveType::Sphere,
				PrimitiveType::Plane,
				PrimitiveType::Cylinder,
				PrimitiveType::Pyramid
			};
			for (PrimitiveType type : types)
			{
				if (ImGui::MenuItem(GeometryGenerator::GetName(type)))
				{
					if (scene) SelectGameObject(scene->CreatePrimitive(type, node));
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
		{
			if (scene)
			{
				scene->DuplicateGameObject(node);
			}
		}

		if (ImGui::MenuItem("Unparent (mover a la raiz)", nullptr, false, scene && node->GetParent() != scene->GetRoot()))
		{
			if (scene) node->SetParent(scene->GetRoot());
		}

		if (ImGui::MenuItem("Focus (F)"))
		{
			GameObject* editorCamera = scene ? scene->GetEditorCameraObject() : nullptr;
			if (CameraComponent* camera = editorCamera ? editorCamera->GetComponent<CameraComponent>() : nullptr)
			{
				camera->FocusOnObject(node);
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Delete", "Supr"))
		{
			if (scene)
			{
				scene->DestroyGameObject(node);
			}
		}

		ImGui::EndPopup();
	}

	if (nodeOpen && hasChildren)
	{
		const std::vector<GameObject*> children = node->GetChildren();
		for (GameObject* child : children)
		{
			if (child && child->GetParent() == node)
			{
				DrawHierarchyNode(child, selected);
			}
		}
		ImGui::TreePop();
	}
}

void ModuleUI::DrawInspectorPanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!showInspector) return;

	if (ImGui::Begin("Inspector", &showInspector))
	{
		Scene* scene = Engine::GetInstance().scene.get();
		GameObject* selected = scene ? scene->GetSelected() : nullptr;

		// Si cambia el GameObject seleccionado (por ejemplo con un clic en la vista Scene),
		// el Inspector vuelve a mostrar GameObjects
		if (selected != lastInspectedObject)
		{
			lastInspectedObject = selected;
			if (selected != nullptr) inspectorShowsAsset = false;
		}

		if (inspectorShowsAsset && !selectedAssetPath.empty())
		{
			DrawAssetInspector();
		}
		else if (selected)
		{
			DrawGameObjectInspector(selected);
		}
		else
		{
			ImGui::TextDisabled("Selecciona un GameObject en Hierarchy o en Scene para inspeccionar sus componentes.");
			ImGui::TextDisabled("Selecciona un fichero en Assets para ver sus opciones de importacion.");
		}
	}
	ImGui::End();
}

void ModuleUI::DrawGameObjectInspector(GameObject* selected)
{
	bool isActive = selected->IsActive();
	if (ImGui::Checkbox("##ActiveGO", &isActive))
	{
		if (isActive) selected->Enable();
		else selected->Disable();
	}
	ImGui::SameLine();

	char nameBuf[128] = "";
	strncpy_s(nameBuf, sizeof(nameBuf), selected->GetName().c_str(), _TRUNCATE);
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 68.0f);
	if (ImGui::InputText("##GOName", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		if (nameBuf[0] != '\0')
		{
			selected->SetName(nameBuf);
		}
	}
	ImGui::TextDisabled("UID: %llu", (unsigned long long)static_cast<uint64_t>(selected->GetUUID()));

	ImGui::Separator();

	// =====================================================================
	// 1. TRANSFORM COMPONENT
	// =====================================================================
	TransformComponent* transform = selected->GetComponent<TransformComponent>();
	// En las cabeceras "###" separa el texto visible (con icono) del ID del widget, que sigue siendo el nombre
	if (transform && ComponentHeader(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT "  Transform###Transform"))
	{
		if (ImGui::BeginTable("TransformTable", 2, ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

			glm::vec3 pos = transform->GetPosition();
			if (DrawVec3Control("Position", pos, 0.1f))
			{
				transform->SetPosition(pos);
			}

			glm::vec3 rot = transform->GetRotation();
			if (DrawVec3Control("Rotation", rot, 1.0f))
			{
				transform->SetRotation(rot);
			}

			glm::vec3 scl = transform->GetScale();
			if (DrawVec3Control("Scale", scl, 0.05f))
			{
				transform->SetScale(scl);
			}

			ImGui::EndTable();
		}

		if (ImGui::SmallButton("Reset Transform"))
		{
			transform->SetPosition(glm::vec3(0.0f));
			transform->SetRotation(glm::vec3(0.0f));
			transform->SetScale(glm::vec3(1.0f));
		}
	}

	// =====================================================================
	// 2. MESH, MATERIAL Y CAMERA
	// =====================================================================
	DrawMeshInspector(selected);
	DrawMaterialInspector(selected);
	DrawCameraInspector(selected);

	// Padre de un modelo (sin malla propia): se puede soltar una textura para todas sus partes
	if (selected->GetComponent<MeshComponent>() == nullptr && !selected->GetChildren().empty())
	{
		const AABB bounds = selected->GetWorldAABB(true);
		if (bounds.IsValid() && ComponentHeader(ICON_FA_CUBES "  Modelo (hijos)###Modelo (hijos)"))
		{
			ImGui::BulletText("Hijos directos: %zu", selected->GetChildren().size());
			const glm::vec3 size = bounds.GetSize();
			ImGui::BulletText("Tamano (AABB del mundo): %.2f x %.2f x %.2f", size.x, size.y, size.z);
			ImGui::Button("Suelta aqui una textura para aplicarla a todas las partes", ImVec2(-1.0f, 0.0f));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_TEXTURE))
				{
					const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(payload->Data, payload->DataSize);
					ImporterManager::AssignTextureToHierarchy(asset.uid, selected);
				}
				ImGui::EndDragDropTarget();
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	DrawAddComponentButton(selected);
}

void ModuleUI::DrawMeshInspector(GameObject* selected)
{
	MeshComponent* mesh = selected->GetComponent<MeshComponent>();
	if (mesh == nullptr) return;
	if (!ComponentHeader(ICON_FA_CUBE "  Mesh###Mesh")) return;

	ResourceManager* resources = Engine::GetInstance().resources.get();
	ImGui::PushID("MeshComponent");

	bool enabled = mesh->IsActive();
	if (ImGui::Checkbox("Activo", &enabled))
	{
		if (enabled) mesh->Enable();
		else mesh->Disable();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Quitar componente"))
	{
		selected->RemoveComponent(COMPONENT_MESH);
		ImGui::PopID();
		return;
	}

	// ----- Selector de malla: combo con todas las mallas importadas + drag & drop -----
	const ResourceMesh* meshResource = mesh->GetMeshResource();
	std::string preview = meshResource ? meshResource->GetName() : (mesh->IsMeshMissing() ? std::string("(Missing)") : std::string("(Ninguna)"));
	if (ImGui::BeginCombo("Malla", preview.c_str(), ImGuiComboFlags_HeightLarge))
	{
		if (ImGui::Selectable("(Ninguna)", mesh->GetMeshUID() == INVALID_UID))
		{
			mesh->SetMesh(INVALID_UID);
		}
		if (resources)
		{
			for (const Resource* resource : resources->GetResourcesOfType(ResourceType::Mesh))
			{
				ImGui::PushID(resource);
				std::string label = resource->GetName();
				label += resource->IsBuiltIn() ? "  [Built-in]" : "  [" + FileSystem::GetFileName(resource->GetAssetPath()) + "]";
				if (ImGui::Selectable(label.c_str(), resource->GetUID() == mesh->GetMeshUID()))
				{
					mesh->SetMesh(resource->GetUID());
				}
				ImGui::PopID();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_MESH))
		{
			const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(payload->Data, payload->DataSize);
			mesh->SetMesh(asset.uid);
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SetItemTooltip("Elige una malla o arrastra aqui una malla desde la ventana Assets\n(despliega un modelo con la flecha para ver sus mallas)");

	// ----- Informacion de la malla -----
	if (meshResource)
	{
		ImGui::BulletText("UID: %llu  |  Referencias: %u", (unsigned long long)meshResource->GetUID(), meshResource->GetReferenceCount());
		ImGui::BulletText("Vertices: %zu  |  Triangulos: %zu", meshResource->GetVertexCount(), meshResource->GetTriangleCount());
		const AABB local = mesh->GetLocalAABB();
		const AABB world = selected->GetWorldAABB(false);
		if (local.IsValid())
		{
			ImGui::BulletText("AABB local min (%.2f, %.2f, %.2f) max (%.2f, %.2f, %.2f)",
				local.min.x, local.min.y, local.min.z, local.max.x, local.max.y, local.max.z);
		}
		if (world.IsValid())
		{
			ImGui::BulletText("AABB mundo min (%.2f, %.2f, %.2f) max (%.2f, %.2f, %.2f)",
				world.min.x, world.min.y, world.min.z, world.max.x, world.max.y, world.max.z);
		}
	}
	else if (mesh->IsMeshMissing())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "La malla asignada ya no existe (asset borrado)");
	}

	// ----- Visualizacion de normales (depuracion) -----
	ImGui::Spacing();
	ImGui::SeparatorText("Visualización de Normales");

	Render* render = Engine::GetInstance().render.get();
	if (render)
	{
		RenderSettings rs = render->GetSettings();
		bool vertexNormals = (rs.normals == NormalsMode::Vertex || rs.normals == NormalsMode::Both);
		bool faceNormals = (rs.normals == NormalsMode::Face || rs.normals == NormalsMode::Both);

		bool changed = false;
		changed |= ImGui::Checkbox("Mostrar Normales por Vértice (Amarillo)", &vertexNormals);
		changed |= ImGui::Checkbox("Mostrar Normales por Cara (Cian)", &faceNormals);
		changed |= ImGui::Checkbox("Aplicar a todos los objetos de la escena", &rs.normalsAllObjects);

		if (changed)
		{
			if (vertexNormals && faceNormals) rs.normals = NormalsMode::Both;
			else if (vertexNormals) rs.normals = NormalsMode::Vertex;
			else if (faceNormals) rs.normals = NormalsMode::Face;
			else rs.normals = NormalsMode::None;

			render->SetSettings(rs);
		}
	}

	ImGui::PopID();
}

void ModuleUI::DrawMaterialInspector(GameObject* selected)
{
	MaterialComponent* material = selected->GetComponent<MaterialComponent>();
	if (material == nullptr) return;
	if (!ComponentHeader(ICON_FA_PALETTE "  Texture & Material###Texture & Material")) return;

	ResourceManager* resources = Engine::GetInstance().resources.get();
	ImGui::PushID("MaterialComponent");

	bool enabled = material->IsActive();
	if (ImGui::Checkbox("Activo", &enabled))
	{
		if (enabled) material->Enable();
		else material->Disable();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Quitar componente"))
	{
		selected->RemoveComponent(COMPONENT_MATERIAL);
		ImGui::PopID();
		return;
	}

	glm::vec4 color = material->GetColor();
	if (ImGui::ColorEdit4("Color Base (Alpha)", glm::value_ptr(color), ImGuiColorEditFlags_PickerHueWheel))
	{
		material->SetColor(color);
	}

	// ----- Selector de textura: combo con todas las texturas importadas + drag & drop -----
	const ResourceTexture* texture = material->GetTexture();
	std::string preview = texture ? texture->GetName() : (material->IsTextureMissing() ? std::string("(Missing)") : std::string("(Ninguna)"));
	if (ImGui::BeginCombo("Textura", preview.c_str(), ImGuiComboFlags_HeightLarge))
	{
		if (ImGui::Selectable("(Ninguna)", material->GetTextureUID() == INVALID_UID))
		{
			material->SetTexture(INVALID_UID);
		}
		if (resources)
		{
			for (const Resource* resource : resources->GetResourcesOfType(ResourceType::Texture))
			{
				ImGui::PushID(resource);
				const std::string label = resource->GetName() + "  [" + FileSystem::GetParentPath(resource->GetAssetPath()) + "]";
				if (ImGui::Selectable(label.c_str(), resource->GetUID() == material->GetTextureUID()))
				{
					material->SetTexture(resource->GetUID());
					material->SetUseTexture(true);
				}
				ImGui::PopID();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_TEXTURE))
		{
			const EditorDragDrop::AssetPayload asset = EditorDragDrop::ReadAssetPayload(payload->Data, payload->DataSize);
			material->SetTexture(asset.uid);
			material->SetUseTexture(true);
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SetItemTooltip("Elige una textura o arrastra aqui una textura desde la ventana Assets");

	bool usingTexture = material->GetUseTexture();
	if (ImGui::Checkbox("Usar Textura Difusa", &usingTexture))
	{
		material->SetUseTexture(usingTexture);
	}

	bool usingChecker = material->GetUseCheckerboard();
	if (ImGui::Checkbox("Textura Checkerboard (Blanco/Negro)", &usingChecker))
	{
		material->SetUseCheckerboard(usingChecker);
	}

	ImGui::Spacing();
	ImGui::SeparatorText("Datos de Textura");
	if (texture)
	{
		ImGui::BulletText("Asset: %s", texture->GetAssetPath().c_str());
		ImGui::BulletText("UID: %llu  |  Referencias: %u", (unsigned long long)texture->GetUID(), texture->GetReferenceCount());
		ImGui::BulletText("Tamaño: %d x %d px", texture->GetWidth(), texture->GetHeight());
		ImGui::BulletText("OpenGL Texture ID: %u", texture->GetGLID());
	}
	else if (material->IsTextureMissing())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "La textura asignada ya no existe (se ve la textura 'missing')");
	}
	else
	{
		ImGui::TextDisabled("Sin textura difusa asignada (arrastra una desde Assets).");
	}

	unsigned int activeId = material->GetActiveTextureID();
	if (activeId != 0)
	{
		ImGui::Spacing();
		ImGui::TextDisabled("Vista previa activa:");
		ImGui::Image((ImTextureID)(intptr_t)activeId, ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
	}

	ImGui::PopID();
}

void ModuleUI::DrawCameraInspector(GameObject* selected)
{
	CameraComponent* cam = selected->GetComponent<CameraComponent>();
	if (cam == nullptr) return;
	if (!ComponentHeader(ICON_FA_VIDEO "  Camera###Camera")) return;

	Scene* scene = Engine::GetInstance().scene.get();
	ImGui::PushID("CameraComponent");

	bool enabled = cam->IsActive();
	if (ImGui::Checkbox("Activo", &enabled))
	{
		if (enabled) cam->Enable();
		else cam->Disable();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Quitar componente"))
	{
		if (scene && scene->GetGameCameraObject() == selected) scene->SetGameCamera(nullptr);
		selected->RemoveComponent(COMPONENT_CAMERA);
		ImGui::PopID();
		return;
	}

	// Main Camera = la que dibuja la ventana Game y hace el frustum culling
	const bool isMain = scene && scene->GetGameCameraObject() == selected;
	if (isMain)
	{
		ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "Main Camera (vista Game)");
	}
	else if (scene && ImGui::Button("Usar como Main Camera"))
	{
		scene->SetGameCamera(selected);
	}

	ImGui::SliderFloat("FOV (Grados)", &cam->fov, 10.0f, 120.0f, "%.1f");
	ImGui::DragFloat("Near Plane", &cam->nearPlane, 0.01f, 0.01f, 10.0f, "%.2f");
	ImGui::DragFloat("Far Plane", &cam->farPlane, 1.0f, 1.0f, 5000.0f, "%.1f");
	if (cam->farPlane <= cam->nearPlane) cam->farPlane = cam->nearPlane + 0.1f;

	ImGui::Checkbox("Frustum Culling", &cam->frustumCulling);
	ImGui::SetItemTooltip("Si esta activo, la vista Game solo dibuja los objetos cuya AABB esta dentro del frustum");

	ImGui::PopID();
}

void ModuleUI::DrawAddComponentButton(GameObject* selected)
{
	const float width = ImGui::GetContentRegionAvail().x;
	if (ImGui::Button("Add Component", ImVec2(width, 0.0f)))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}

	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		const bool hasMesh = selected->GetComponent<MeshComponent>() != nullptr;
		const bool hasMaterial = selected->GetComponent<MaterialComponent>() != nullptr;
		const bool hasCamera = selected->GetComponent<CameraComponent>() != nullptr;

		if (ImGui::MenuItem("Mesh", nullptr, false, !hasMesh))
		{
			selected->AddComponent(std::make_unique<MeshComponent>(selected));
		}
		if (ImGui::MenuItem("Material", nullptr, false, !hasMaterial))
		{
			selected->AddComponent(std::make_unique<MaterialComponent>(selected));
		}
		if (ImGui::MenuItem("Camera", nullptr, false, !hasCamera))
		{
			selected->AddComponent(std::make_unique<CameraComponent>(selected));
			Scene* scene = Engine::GetInstance().scene.get();
			if (scene && scene->GetGameCameraObject() == nullptr) scene->SetGameCamera(selected);
		}
		ImGui::EndPopup();
	}
}

bool ModuleUI::PostUpdate()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	RenderUI();
	return true;
}

bool ModuleUI::CleanUp()
{
	LOG("[ImGui] Apagando Dear ImGui y liberando recursos...");
	consoleViewCache.clear();
	consoleViewCache.shrink_to_fit();

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	return true;
}

void ModuleUI::RenderUI()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	ImGui::Render();

	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
		SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
	}
}

void ModuleUI::DrawConsolePanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!showConsole) return;

	if (!ImGui::Begin("Console", &showConsole))
	{
		ImGui::End();
		return;
	}

	LogSystem& logSys = LogSystem::GetInstance();

	// 1. Barra de herramientas superior
	if (ImGui::Button(ICON_FA_TRASH_CAN "  Clear###ConsoleClear"))
	{
		logSys.Clear();
		selectedLogIndex = -1;
		consoleFilterDirty = true;
	}
	ImGui::SameLine();

	if (ImGui::Checkbox("Collapse", &collapseLogs))
	{
		consoleFilterDirty = true;
	}
	ImGui::SameLine();
	ImGui::Checkbox("Auto-scroll", &autoScroll);
	ImGui::SameLine();

	// Filtro de búsqueda por texto
	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::InputTextWithHint("##ConsoleSearch", ICON_FA_MAGNIFYING_GLASS "  Buscar...", consoleFilter, sizeof(consoleFilter)))
	{
		consoleFilterDirty = true;
	}
	if (consoleFilter[0] != '\0')
	{
		ImGui::SameLine();
		if (ImGui::SmallButton(ICON_FA_XMARK "##ConsoleSearchClear"))
		{
			consoleFilter[0] = '\0';
			consoleFilterDirty = true;
		}
	}

	ImGui::SameLine();
	ImGui::Spacing();
	ImGui::SameLine();

	// Filtros por Categoría con Contadores. El contador forma parte del texto del boton, asi que el ID se
	// fija con "###": si no, cambiaria cada vez que llega un log y el clic podria perderse.
	const ImVec4 filterOffColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
	ImVec4 infoColor = showInfo ? EditorPalette::LEVEL_INFO : filterOffColor;
	ImGui::PushStyleColor(ImGuiCol_Text, infoColor);
	std::string infoLabel = ICON_FA_CIRCLE_INFO "  Info (" + std::to_string(logSys.GetInfoCount()) + ")###ConsoleInfo";
	if (ImGui::SmallButton(infoLabel.c_str()))
	{
		showInfo = !showInfo;
		consoleFilterDirty = true;
	}
	ImGui::PopStyleColor();
	ImGui::SameLine();

	ImVec4 warnColor = showWarnings ? EditorPalette::LEVEL_WARNING : filterOffColor;
	ImGui::PushStyleColor(ImGuiCol_Text, warnColor);
	std::string warnLabel = ICON_FA_TRIANGLE_EXCLAMATION "  Warn (" + std::to_string(logSys.GetWarningCount()) + ")###ConsoleWarn";
	if (ImGui::SmallButton(warnLabel.c_str()))
	{
		showWarnings = !showWarnings;
		consoleFilterDirty = true;
	}
	ImGui::PopStyleColor();
	ImGui::SameLine();

	ImVec4 errColor = showErrors ? EditorPalette::LEVEL_ERROR : filterOffColor;
	ImGui::PushStyleColor(ImGuiCol_Text, errColor);
	std::string errLabel = ICON_FA_CIRCLE_XMARK "  Error (" + std::to_string(logSys.GetErrorCount()) + ")###ConsoleError";
	if (ImGui::SmallButton(errLabel.c_str()))
	{
		showErrors = !showErrors;
		consoleFilterDirty = true;
	}
	ImGui::PopStyleColor();

	ImGui::Separator();

	// 2. Reconstruir la caché de índices visibles solo si cambió la revisión del LogSystem o los filtros
	const uint64_t currentRev = logSys.GetRevision();
	if (currentRev != lastConsoleRevision || consoleFilterDirty)
	{
		if (currentRev != lastConsoleRevision && autoScroll)
		{
			scrollToBottom = true;
		}
		lastConsoleRevision = currentRev;
		consoleFilterDirty = false;

		consoleViewCache.clear();
		std::string searchLower = consoleFilter;
		std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });

		std::string prevText;
		LogType prevType = LogType::Info;

		logSys.ForEach([&](int index, const LogEntry& entry)
		{
			if (entry.type == LogType::Info && !showInfo) return;
			if (entry.type == LogType::Warning && !showWarnings) return;
			if (entry.type == LogType::Error && !showErrors) return;
			if (!ContainsCaseInsensitive(entry.text, searchLower)) return;

			if (collapseLogs && !consoleViewCache.empty() && entry.type == prevType && entry.text == prevText)
			{
				consoleViewCache.back().collapsedCount++;
				return;
			}

			consoleViewCache.push_back({ index, 1 });
			prevText = entry.text;
			prevType = entry.type;
		});
	}

	// 3. Región de lista y panel de detalles inferior
	LogEntry selectedEntry;
	const bool hasSelectedEntry = (selectedLogIndex >= 0) && logSys.GetEntry(selectedLogIndex, selectedEntry);
	if (!hasSelectedEntry)
	{
		selectedLogIndex = -1;
	}

	float detailsHeight = hasSelectedEntry ? 95.0f : 0.0f;
	float listHeight = ImGui::GetContentRegionAvail().y - detailsHeight;
	if (detailsHeight > 0.0f) listHeight -= ImGui::GetStyle().ItemSpacing.y;

	ImGui::BeginChild("LogScrollRegion", ImVec2(0, listHeight), false, ImGuiWindowFlags_HorizontalScrollbar);

	LogEntry entry;
	for (size_t viewIdx = 0; viewIdx < consoleViewCache.size(); ++viewIdx)
	{
		const ConsoleDisplayItem& item = consoleViewCache[viewIdx];
		if (!logSys.GetEntry(item.logIndex, entry)) continue;

		// Icono del nivel del mensaje (los iconos tienen un ancho minimo comun: los textos quedan en columna)
		ImVec4 textColor;
		const char* typeTag = ICON_FA_CIRCLE_INFO;
		switch (entry.type)
		{
		case LogType::Info:
			textColor = EditorPalette::LEVEL_INFO;
			typeTag = ICON_FA_CIRCLE_INFO;
			break;
		case LogType::Warning:
			textColor = EditorPalette::LEVEL_WARNING;
			typeTag = ICON_FA_TRIANGLE_EXCLAMATION;
			break;
		case LogType::Error:
			textColor = EditorPalette::LEVEL_ERROR;
			typeTag = ICON_FA_CIRCLE_XMARK;
			break;
		}

		ImGui::PushID(item.logIndex);

		bool isSelected = (selectedLogIndex == item.logIndex);

		ImGui::TextDisabled("[%s]", entry.timestamp.c_str());
		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		ImGui::TextUnformatted(typeTag);
		ImGui::PopStyleColor();
		ImGui::SameLine();

		if (collapseLogs && item.collapsedCount > 1)
		{
			ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[x%d]", item.collapsedCount);
			ImGui::SameLine();
		}

		if (ImGui::Selectable(entry.text.c_str(), isSelected))
		{
			selectedLogIndex = isSelected ? -1 : item.logIndex;
		}

		ImGui::PopID();
	}

	if (scrollToBottom || (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
	{
		ImGui::SetScrollHereY(1.0f);
		scrollToBottom = false;
	}

	ImGui::EndChild();

	// 4. Panel inferior de detalle del mensaje seleccionado
	if (hasSelectedEntry)
	{
		ImGui::Separator();
		ImGui::BeginChild("LogDetailsRegion", ImVec2(0, detailsHeight), true);

		ImGui::TextDisabled("Detalle:");
		ImGui::SameLine();
		if (ImGui::SmallButton("Copiar Mensaje"))
		{
			std::string fullText = "[" + selectedEntry.timestamp + "] " + selectedEntry.text;
			if (!selectedEntry.file.empty()) fullText += " (" + selectedEntry.file + ":" + std::to_string(selectedEntry.line) + ")";
			ImGui::SetClipboardText(fullText.c_str());
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Cerrar Detalle"))
		{
			selectedLogIndex = -1;
		}

		if (!selectedEntry.file.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("| Origen: %s:%d", selectedEntry.file.c_str(), selectedEntry.line);
		}

		ImGui::TextWrapped("%s", selectedEntry.text.c_str());

		ImGui::EndChild();
	}

	ImGui::End();
}

void ModuleUI::DrawConfigurationPanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!showConfiguration) return;

	if (ImGui::Begin("Configuration", &showConfiguration))
	{
		if (ImGui::BeginTabBar("ConfigurationTabBar", ImGuiTabBarFlags_None))
		{
			// =========================================================================
			// MONITORIZACIÓN DEL RENDIMIENTO (APPLICATION)
			// =========================================================================
			if (ImGui::BeginTabItem("Application"))
			{
				const auto& perf = Engine::GetInstance().GetPerformanceData();

				ImGui::Text("Motor: Motor Videojuego 3D");
				ImGui::BulletText("Tiempo Activo (Uptime): %.1f s", perf.uptimeSeconds);
				ImGui::BulletText("Frames Totales: %zu", perf.totalFrames);
				ImGui::BulletText("Consumo RAM del Proceso: %zu MB (Pico: %zu MB)", perf.processMemoryMB, perf.processPeakMemoryMB);

				ImGui::Spacing();
				ImGui::SeparatorText("Control de Framerate");

				int targetFps = Engine::GetInstance().GetTargetFrameRate();
				if (ImGui::SliderInt("Límite FPS (0=Ilimitado)", &targetFps, 0, 240))
				{
					Engine::GetInstance().SetTargetFrameRate(targetFps);
				}

				bool vsync = Engine::GetInstance().render->GetVSync();
				if (ImGui::Checkbox("V-Sync (Sincronización Vertical)", &vsync))
				{
					Engine::GetInstance().render->SetVSync(vsync);
				}

				ImGui::Spacing();
				ImGui::SeparatorText("Gráficos de Rendimiento en Tiempo Real");

				char fpsOverlay[128];
				sprintf_s(fpsOverlay, sizeof(fpsOverlay), "FPS: %.1f (Min: %.1f, Max: %.1f, Med: %.1f)",
					perf.currentFps, perf.minFps, perf.maxFps, perf.avgFps);
				ImGui::PlotLines("##FPSGraph", perf.fpsHistory, (int)PerformanceData::HISTORY_CAPACITY,
					(int)perf.historyIndex, fpsOverlay, 0.0f, 140.0f, ImVec2(0, 80));

				char msOverlay[128];
				sprintf_s(msOverlay, sizeof(msOverlay), "Frame Time: %.2f ms (Min: %.2f, Max: %.2f, Med: %.2f)",
					perf.currentMs, perf.minMs, perf.maxMs, perf.avgMs);
				ImGui::PlotLines("##MSGraph", perf.msHistory, (int)PerformanceData::HISTORY_CAPACITY,
					(int)perf.historyIndex, msOverlay, 0.0f, 35.0f, ImVec2(0, 80));

				ImGui::Spacing();
				ImGui::SeparatorText("Resumen Estadístico");

				if (ImGui::BeginTable("PerfMetricsSummary", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("Métrica");
					ImGui::TableSetupColumn("Mínimo");
					ImGui::TableSetupColumn("Actual");
					ImGui::TableSetupColumn("Máximo");
					ImGui::TableHeadersRow();

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::Text("FPS");
					ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f", perf.minFps);
					ImGui::TableSetColumnIndex(2); ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%.1f", perf.currentFps);
					ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", perf.maxFps);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::Text("Tiempo Frame (ms)");
					ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f ms", perf.minMs);
					ImGui::TableSetColumnIndex(2); ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "%.2f ms", perf.currentMs);
					ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f ms", perf.maxMs);

					ImGui::EndTable();
				}

				ImGui::EndTabItem();
			}

			// =========================================================================
			// CONFIGURACIÓN DEL MÓDULO WINDOW
			// =========================================================================
			if (ImGui::BeginTabItem("Window"))
			{
				auto win = Engine::GetInstance().window;
				const int curW = win->GetWidth();
				const int curH = win->GetHeight();

				ImGui::SeparatorText("Propiedades en Tiempo Real");

				bool applyTitle = false;
				if (ImGui::InputText("Título", windowTitleBuffer, sizeof(windowTitleBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					applyTitle = true;
				}
				ImGui::SameLine();
				if (ImGui::Button("Aplicar Título"))
				{
					applyTitle = true;
				}
				if (applyTitle && windowTitleBuffer[0] != '\0')
				{
					win->SetTitle(windowTitleBuffer);
				}

				// Modificación 100% reactiva del tamaño de ventana (sin botones intermedios)
				int w = curW;
				int h = curH;
				bool sizeChanged = false;
				sizeChanged |= ImGui::DragInt("Ancho (px)", &w, 2.0f, Window::MIN_WIDTH, 3840);
				sizeChanged |= ImGui::DragInt("Alto (px)", &h, 2.0f, Window::MIN_HEIGHT, 2160);
				if (sizeChanged)
				{
					win->SetSize(w, h);
				}

				ImGui::Spacing();
				bool isFull = win->IsFullscreen();
				if (ImGui::Checkbox("Pantalla Completa (Fullscreen)", &isFull))
				{
					win->SetFullscreen(isFull);
				}

				bool isRes = win->IsResizable();
				if (ImGui::Checkbox("Ventana Redimensionable (Resizable)", &isRes))
				{
					win->SetResizable(isRes);
				}

				bool isBrd = win->IsBorderless();
				if (ImGui::Checkbox("Sin Bordes (Borderless)", &isBrd))
				{
					win->SetBorderless(isBrd);
				}

				ImGui::Spacing();
				ImGui::SeparatorText("Parámetros de Contexto [Requiere Reinicio]");
				ImGui::TextDisabled("• API Gráfica: OpenGL 4.6 Core Profile");
				ImGui::TextDisabled("• Doble Búfer (Double Buffer): Habilitado");
				ImGui::TextDisabled("• Profundidad Z-Buffer: 24 bits | Stencil: 8 bits");

				ImGui::EndTabItem();
			}

			// =========================================================================
			// CONFIGURACIÓN DEL MÓDULO RENDERER (Única fuente de verdad)
			// =========================================================================
			if (ImGui::BeginTabItem("Renderer"))
			{
				auto rnd = Engine::GetInstance().render;
				RenderSettings rs = rnd->GetSettings();
				bool settingsChanged = false;

				ImGui::SeparatorText("Pipeline OpenGL en Tiempo Real");
				settingsChanged |= ImGui::Checkbox("Depth Test (GL_DEPTH_TEST)", &rs.depthTest);
				settingsChanged |= ImGui::Checkbox("Face Culling (GL_CULL_FACE)", &rs.cullFace);
				settingsChanged |= ImGui::Checkbox("Iluminación Direccional (Shader Lighting)", &rs.lighting);
				settingsChanged |= ImGui::Checkbox("Modo Wireframe (GL_LINE)", &rs.wireframe);
				settingsChanged |= ImGui::Checkbox("Resaltar Selección", &rs.highlightSelection);

				// ColorEdit para seleccionar el color del outline en tiempo real
				if (rs.highlightSelection)
				{
					ImGui::Indent();
					settingsChanged |= ImGui::ColorEdit3("Color del Outline", glm::value_ptr(rs.outlineColor), ImGuiColorEditFlags_PickerHueWheel);
					ImGui::Unindent();
				}

				bool showDepth = rs.showDepthBuffer;
				if (ImGui::Checkbox("Show Z-Buffer Window", &showDepth))
				{
					rs.showDepthBuffer = showDepth;
					rnd->SetSettings(rs);
				}

				// Subapartado para Grid y Central Axis
				ImGui::Spacing();
				ImGui::SeparatorText("Grid & Origin Axis");
				settingsChanged |= ImGui::Checkbox("Mostrar Rejilla (Plano XZ)", &rs.showGrid);
				settingsChanged |= ImGui::Checkbox("Mostrar Ejes Centrales (X: Rojo, Y: Verde, Z: Azul)", &rs.showAxis);

				ImGui::Spacing();
				ImGui::SeparatorText("Depuración de Normales");
				static const char* normalModes[] = {
					"Desactivado",
					"Por Vértice (Amarillo)",
					"Por Cara / Triángulo (Cian)",
					"Ambos (Vértice + Cara)"
				};
				int currentNormalsMode = (int)rs.normals;
				if (ImGui::Combo("Modo Normales", &currentNormalsMode, normalModes, IM_ARRAYSIZE(normalModes)))
				{
					rs.normals = (NormalsMode)currentNormalsMode;
					settingsChanged = true;
				}
				settingsChanged |= ImGui::Checkbox("Mostrar normales en toda la escena", &rs.normalsAllObjects);

				if (settingsChanged)
				{
					rnd->SetSettings(rs);
				}

				ImGui::Spacing();
				SDL_Color bg = rnd->GetBackgroundColor();
				float bgFloat[3] = { bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f };
				if (ImGui::ColorEdit3("Color de Fondo (Clear Color)", bgFloat, ImGuiColorEditFlags_PickerHueWheel))
				{
					SDL_Color newBg = {
						(Uint8)(bgFloat[0] * 255.0f),
						(Uint8)(bgFloat[1] * 255.0f),
						(Uint8)(bgFloat[2] * 255.0f),
						255
					};
					rnd->SetBackgroundColor(newBg);
				}

				// ---------------- Frustum culling, octree y gizmos ----------------
				ImGui::Spacing();
				ImGui::SeparatorText("Frustum Culling y Octree");
				{
					RenderSettings cullingSettings = rnd->GetSettings();
					bool cullingChanged = false;
					cullingChanged |= ImGui::Checkbox("Culling en la vista Scene (camara del editor)", &cullingSettings.editorFrustumCulling);
					cullingChanged |= ImGui::Checkbox("Vista previa del culling de la Main Camera", &cullingSettings.cullingPreview);
					cullingChanged |= ImGui::Checkbox("Dibujar AABB de todas las mallas (verde = visible, rojo = descartada)", &cullingSettings.showAABBs);
					cullingChanged |= ImGui::Checkbox("Dibujar AABB del seleccionado", &cullingSettings.showSelectedAABB);
					cullingChanged |= ImGui::Checkbox("Dibujar frustum de la Main Camera", &cullingSettings.showCameraFrustum);
					cullingChanged |= ImGui::Checkbox("Dibujar nodos del octree", &cullingSettings.showOctree);
					cullingChanged |= ImGui::Checkbox("Dibujar rayo del mouse picking", &cullingSettings.showPickRay);
					if (cullingChanged) rnd->SetSettings(cullingSettings);

					Scene* cullingScene = Engine::GetInstance().scene.get();
					if (cullingScene)
					{
						bool useOctree = cullingScene->IsUsingOctree();
						if (ImGui::Checkbox("Usar octree (si se desactiva se prueban todos los objetos)", &useOctree))
						{
							cullingScene->SetUseOctree(useOctree);
						}

						const Octree& octree = cullingScene->GetOctree();
						ImGui::BulletText("Octree: %d nodos, profundidad %d, %zu objetos (reconstruido %d veces)",
							octree.GetNodeCount(), octree.GetDepth(), octree.GetObjectCount(), cullingScene->GetOctreeRebuildCount());

						const RenderStats& renderStats = rnd->GetStats();
						ImGui::BulletText("Vista Scene: %d / %d dibujados (nodos visitados %d, AABB probadas %d)",
							renderStats.sceneDrawn, renderStats.sceneCandidates, renderStats.sceneQuery.nodesVisited, renderStats.sceneQuery.objectsTested);
						ImGui::BulletText("Vista Game:  %d / %d dibujados (nodos visitados %d, AABB probadas %d)",
							renderStats.gameDrawn, renderStats.gameCandidates, renderStats.gameQuery.nodesVisited, renderStats.gameQuery.objectsTested);

						const PickStats& pick = cullingScene->GetLastPickStats();
						ImGui::BulletText("Ultimo picking: %d candidatos, %d mallas, %d triangulos probados (%.3f ms)",
							pick.candidates, pick.meshesTested, pick.trianglesTested, pick.milliseconds);
					}
				}

				ImGui::Spacing();
				ImGui::SeparatorText("Estado del Framebuffer (FBO)");
				ImGui::BulletText("Resolución Viewport: %d x %d px", rnd->GetViewportWidth(), rnd->GetViewportHeight());
				ImGui::BulletText("ID Framebuffer (FBO): %u", rnd->GetFramebufferID());
				ImGui::BulletText("ID Textura Color: %u", rnd->GetSceneTexture());
				ImGui::BulletText("ID Renderbuffer (RBO): %u", rnd->GetRboID());
				ImGui::BulletText("Estado OpenGL: %s", rnd->IsFramebufferComplete() ? "GL_FRAMEBUFFER_COMPLETE" : "INCOMPLETO");

				ImGui::EndTabItem();
			}

			// =========================================================================
			// CONFIGURACIÓN DE CÁMARA DE EDITOR (CAMERA)
			// =========================================================================
			if (ImGui::BeginTabItem("Camera"))
			{
				Scene* scene = Engine::GetInstance().scene.get();
				GameObject* camObj = scene ? scene->GetEditorCameraObject() : nullptr;
				CameraComponent* cam = camObj ? camObj->GetComponent<CameraComponent>() : nullptr;
				TransformComponent* camTrans = camObj ? camObj->GetComponent<TransformComponent>() : nullptr;

				if (cam && camTrans)
				{
					ImGui::SeparatorText("Parámetros de Proyección");
					ImGui::SliderFloat("Campo de Visión (FOV)", &cam->fov, 10.0f, 120.0f, "%.1f deg");
					ImGui::DragFloat("Plano Cercano (Near)", &cam->nearPlane, 0.01f, 0.01f, 10.0f, "%.2f");
					ImGui::DragFloat("Plano Lejano (Far)", &cam->farPlane, 1.0f, 10.0f, 5000.0f, "%.1f");
					ImGui::Checkbox("Frustum Culling (vista Scene)", &cam->frustumCulling);

					ImGui::Spacing();
					ImGui::SeparatorText("Navegación y Control");
					glm::vec3 pos = camTrans->GetPosition();
					if (ImGui::DragFloat3("Posición Cámara", glm::value_ptr(pos), 0.1f))
					{
						camTrans->SetPosition(pos);
					}

					float speed = cam->GetMovementSpeed();
					if (ImGui::SliderFloat("Velocidad Movimiento", &speed, 0.5f, 30.0f, "%.1f u/s"))
					{
						cam->SetMovementSpeed(speed);
					}

					float sens = cam->GetMouseSensitivity();
					if (ImGui::SliderFloat("Sensibilidad Ratón", &sens, 0.01f, 1.0f, "%.2f"))
					{
						cam->SetMouseSensitivity(sens);
					}

					float orbitDist = cam->GetOrbitDistance();
					if (ImGui::SliderFloat("Distancia de Órbita", &orbitDist, 1.0f, 30.0f, "%.1f"))
					{
						cam->SetOrbitDistance(orbitDist);
					}

					if (ImGui::Button("Restablecer Cámara"))
					{
						cam->ResetCamera();
					}
					if (scene && scene->GetSelected())
					{
						ImGui::SameLine();
						if (ImGui::Button("Enfocar Seleccionado (F)"))
						{
							cam->FocusOnObject(scene->GetSelected());
						}
					}
				}
				else
				{
					ImGui::TextDisabled("No hay cámara activa en la escena.");
				}

				ImGui::EndTabItem();
			}

			// =========================================================================
			// CONFIGURACIÓN DEL MÓDULO INPUT
			// =========================================================================
			if (ImGui::BeginTabItem("Input"))
			{
				auto inp = Engine::GetInstance().input;

				ImGui::SeparatorText("Ratón (Mouse)");
				ImGui::BulletText("Posición Pantalla: (%d, %d)", inp->GetMouseX(), inp->GetMouseY());
				ImGui::BulletText("Movimiento Delta: (%d, %d)", inp->GetMouseMotionX(), inp->GetMouseMotionY());
				ImGui::BulletText("Rueda / Scroll Delta: %.2f", inp->GetMouseScrollDelta());

				ImGui::Spacing();
				ImGui::Text("Botones del Ratón:");
				ImGui::SameLine();
				auto drawButtonBadge = [](const char* name, bool pressed) {
					if (pressed) {
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
						ImGui::SmallButton(name);
						ImGui::PopStyleColor();
					} else {
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
						ImGui::SmallButton(name);
						ImGui::PopStyleColor();
					}
					ImGui::SameLine();
				};

				drawButtonBadge("Izquierdo", inp->GetMouseButton(1) == KEY_REPEAT || inp->GetMouseButton(1) == KEY_DOWN);
				drawButtonBadge("Central", inp->GetMouseButton(2) == KEY_REPEAT || inp->GetMouseButton(2) == KEY_DOWN);
				drawButtonBadge("Derecho", inp->GetMouseButton(3) == KEY_REPEAT || inp->GetMouseButton(3) == KEY_DOWN);
				ImGui::NewLine();

				ImGui::Spacing();
				ImGui::SeparatorText("Mando / Gamepad");
				bool gpConnected = inp->IsGamepadConnected();
				ImGui::BulletText("Estado: %s", gpConnected ? "Conectado" : "No detectado");
				if (gpConnected)
				{
					ImGui::BulletText("Stick Izquierdo: (%.2f, %.2f)",
						inp->GetGamepadAxis(GAMEPAD_AXIS_LSTICK_X), inp->GetGamepadAxis(GAMEPAD_AXIS_LSTICK_Y));
					ImGui::BulletText("Stick Derecho: (%.2f, %.2f)",
						inp->GetGamepadAxis(GAMEPAD_AXIS_RSTICK_X), inp->GetGamepadAxis(GAMEPAD_AXIS_RSTICK_Y));
					ImGui::BulletText("Gatillos (LT / RT): (%.2f / %.2f)",
						inp->GetGamepadAxis(GAMEPAD_AXIS_LT), inp->GetGamepadAxis(GAMEPAD_AXIS_RT));
				}

				ImGui::EndTabItem();
			}

			// =========================================================================
			// CONFIGURACIÓN DEL MÓDULO TEXTURES (DEVIL / OPENGL)
			// =========================================================================
			if (ImGui::BeginTabItem("Textures"))
			{
				const auto& hw = Engine::GetInstance().GetHardwareMetrics();
				const ResourceManager* resourceManager = Engine::GetInstance().resources.get();

				ImGui::SeparatorText("Subsistema DevIL");
				ImGui::BulletText("Librería de Carga: Developer's Image Library (DevIL v%s)", hw.devilVersion.c_str());
				ImGui::BulletText("Formatos Compatibles: PNG, JPG, JPEG, DDS, TGA, BMP");
				ImGui::BulletText("Formato propio en Library: .tex (cabecera + pixeles RGB/RGBA listos para OpenGL)");

				if (resourceManager)
				{
					const std::vector<const Resource*> textures = resourceManager->GetResourcesOfType(ResourceType::Texture);
					ImGui::Spacing();
					ImGui::SeparatorText("Texturas (recursos)");
					ImGui::BulletText("Texturas importadas: %zu", textures.size());
					ImGui::BulletText("Texturas en memoria (VRAM): %zu (+2 procedurales: checkerboard y missing)", resourceManager->GetLoadedCount(ResourceType::Texture));
				}

				ImGui::Spacing();
				ImGui::TextWrapped("Los filtros, el wrapping, el volteo y el tamano maximo se configuran por textura: selecciona la textura en la ventana Assets y cambia sus opciones de importacion en el Inspector.");

				ImGui::EndTabItem();
			}

			// =========================================================================
			// CONFIGURACIÓN DEL MÓDULO AUDIO
			// =========================================================================
			if (ImGui::BeginTabItem("Audio"))
			{
				auto aud = Engine::GetInstance().audio;

				ImGui::SeparatorText("Volúmenes Maestros en Tiempo Real");

				float musicVol = aud->GetMusicVolume();
				if (ImGui::SliderFloat("Volumen Música", &musicVol, 0.0f, 1.0f, "%.2f (%.0f%%)"))
				{
					aud->SetMusicVolume(musicVol);
				}

				float sfxVol = aud->GetSFXVolume();
				if (ImGui::SliderFloat("Volumen Efectos (SFX)", &sfxVol, 0.0f, 1.0f, "%.2f (%.0f%%)"))
				{
					aud->SetSFXVolume(sfxVol);
				}

				ImGui::Spacing();
				ImGui::SeparatorText("Dispositivo de Salida de Audio");
				ImGui::BulletText("Driver de Audio: SDL3 Audio Subsystem");
				ImGui::BulletText("Estado: Inicializado correctamente");

				ImGui::EndTabItem();
			}

			// =========================================================================
			// 8. INFORMACIÓN DEL SISTEMA, HARDWARE Y VERSIONES (Cacheadas)
			// =========================================================================
			if (ImGui::BeginTabItem("Hardware"))
			{
				const auto& hw = Engine::GetInstance().GetHardwareMetrics();
				const auto& perf = Engine::GetInstance().GetPerformanceData();

				// CPU
				ImGui::SeparatorText("Procesador (CPU)");
				ImGui::BulletText("Núcleos Lógicos: %d", hw.cpuCores);
				ImGui::BulletText("Memoria RAM del Sistema: %d MB (%.1f GB)", hw.systemRamMB, hw.systemRamMB / 1024.0f);
				ImGui::BulletText("Tamaño Línea Caché L1/L2: %d bytes", hw.cpuCacheLineSize);

				ImGui::Text("Conjuntos de Instrucciones:");
				ImGui::SameLine();
				auto drawInstructionBadge = [](const char* name, bool supported) {
					if (supported) {
						ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[%s]", name);
						ImGui::SameLine();
					}
				};
				drawInstructionBadge("AVX", hw.hasAVX);
				drawInstructionBadge("AVX2", hw.hasAVX2);
				drawInstructionBadge("AVX512", hw.hasAVX512);
				drawInstructionBadge("SSE", hw.hasSSE);
				drawInstructionBadge("SSE2", hw.hasSSE2);
				drawInstructionBadge("SSE3", hw.hasSSE3);
				drawInstructionBadge("SSE4.1", hw.hasSSE41);
				drawInstructionBadge("SSE4.2", hw.hasSSE42);
				ImGui::NewLine();

				// GPU
				ImGui::Spacing();
				ImGui::SeparatorText("Tarjeta Gráfica (GPU)");
				ImGui::BulletText("Fabricante (Vendor): %s", hw.gpuVendor.c_str());
				ImGui::BulletText("Modelo / Renderizador: %s", hw.gpuRenderer.c_str());
				ImGui::BulletText("Versión Driver OpenGL: %s", hw.gpuVersion.c_str());
				ImGui::BulletText("Versión de Shading (GLSL): %s", hw.glslVersion.c_str());
				ImGui::BulletText("Tamaño Máx. de Textura: %d x %d px", hw.maxTextureSize, hw.maxTextureSize);
				ImGui::BulletText("Tamaño Máx. de Viewport: %d x %d px", hw.maxViewportDims[0], hw.maxViewportDims[1]);

				// Memoria (RAM y VRAM)
				ImGui::Spacing();
				ImGui::SeparatorText("Consumo de Memoria (RAM y VRAM)");
				ImGui::BulletText("RAM del Proceso (Working Set): %zu MB (Pico: %zu MB)", perf.processMemoryMB, perf.processPeakMemoryMB);
				if (hw.vramTotalMB > 0)
				{
					int vramUsed = std::max(0, hw.vramTotalMB - hw.vramAvailableMB);
					ImGui::BulletText("VRAM Dedicada Total: %d MB", hw.vramTotalMB);
					ImGui::BulletText("VRAM Disponible: %d MB (En uso aprox.: %d MB)", hw.vramAvailableMB, vramUsed);
				}
				else
				{
					ImGui::BulletText("VRAM: No reportada por el driver actual (sin extensión NVX/ATI)");
				}

				// Software y Bibliotecas
				ImGui::Spacing();
				ImGui::SeparatorText("Versiones Activas de Software");
				ImGui::BulletText("Sistema Operativo: %s", hw.osPlatform.c_str());
				ImGui::BulletText("SDL3: v%s", hw.sdlVersion.c_str());
				ImGui::BulletText("OpenGL: %s", hw.openglVersion.c_str());
				ImGui::BulletText("DevIL: v%s", hw.devilVersion.c_str());
				ImGui::BulletText("Assimp: v%s", hw.assimpVersion.c_str());
				ImGui::BulletText("Dear ImGui: v%s", hw.imguiVersion.c_str());

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void ModuleUI::DrawAboutPanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!showAbout) return;

	ImGui::SetNextWindowSize(ImVec2(520, 440), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("About MotorVideojuego", &showAbout))
	{
		const auto& hw = Engine::GetInstance().GetHardwareMetrics();

		ImGui::TextColored(ImVec4(0.35f, 0.8f, 1.0f, 1.0f), "MotorVideojuego 3D Engine");
		ImGui::SameLine();
		ImGui::TextDisabled("v0.2 (A2 - Editor del motor)");
		ImGui::TextWrapped("Motor gráfico 3D educativo desarrollado en c++ con arquitectura modular de Entidades/Componentes y editor integrado.");

		ImGui::Spacing();
		ImGui::SeparatorText("Equipo de Desarrollo");
		ImGui::BulletText("Jan Corredor");
		ImGui::BulletText("Rubén Mateo Guerrero");
		ImGui::BulletText("Marc Jiménez Marquina");

		ImGui::Spacing();
		ImGui::SeparatorText("Librerías Utilizadas");
		ImGui::BulletText("SDL3 v%s (Ventana, Contexto, Input y Audio)", hw.sdlVersion.c_str());
		ImGui::BulletText("OpenGL %s + GLAD (Renderizado 3D)", hw.openglVersion.c_str());
		ImGui::BulletText("Dear ImGui v%s (Interfaz del Editor y Docking)", hw.imguiVersion.c_str());
		ImGui::BulletText("Assimp v%s (Importación de Modelos 3D)", hw.assimpVersion.c_str());
		ImGui::BulletText("DevIL v%s (Carga de Texturas)", hw.devilVersion.c_str());
		ImGui::BulletText("GLM (Matemáticas 3D, Matrices y Transformaciones)");
		ImGui::BulletText("Tracy Profiler (Profiling y Rendimiento)");
		ImGui::BulletText("nlohmann/json (Escenas, .meta y modelos en JSON)");

		ImGui::Spacing();
		ImGui::SeparatorText("Licencia MIT");
		ImGui::BeginChild("MITLicenseChild", ImVec2(0, 130), true);
		ImGui::TextWrapped(
			"MIT License\n\n"
			"Copyright (c) 2026 MotorVideojuego Team\n\n"
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
			"SOFTWARE."
		);
		ImGui::EndChild();
	}
	ImGui::End();
}

void ModuleUI::DrawDepthBufferPanel()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	Render* render = Engine::GetInstance().render.get();
	if (!render) return;

	RenderSettings rs = render->GetSettings();
	if (!rs.showDepthBuffer) return; // Window stays hidden if setting is false

	bool open = rs.showDepthBuffer;
	if (ImGui::Begin("Z-Buffer Visualizer", &open))
	{
		unsigned int depthTexture = render->GetDepthTexture();

		if (depthTexture != 0)
		{
			ImVec2 availSize = ImGui::GetContentRegionAvail();
			if (availSize.x < 50.0f) availSize.x = 200.0f; // Fallback width
			if (availSize.y < 50.0f) availSize.y = 100.0f; // Fallback height
			
			// Draw the depth buffer texture (Invert UVs on Y axis for OpenGL)
			ImGui::Image((ImTextureID)(intptr_t)depthTexture, availSize, ImVec2(0, 1), ImVec2(1, 0));			
		}
		else
		{
			ImGui::TextDisabled("Depth texture ID is 0 (Not initialized).");
		}
	}
	ImGui::End();

	// Sync state if closed via the 'X' button
	if (!open)
	{
		rs.showDepthBuffer = false;
		render->SetSettings(rs);
	}
}