#include "Render.h"
#include "../Build/BuildManifest.h"

//GLM Math Library
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "../Scene/GameObjects/GameObject.h"

//////////////////////////
#include "../Engine.h"
#include "../Window/Window.h"
#include "../../Utils/Log.h"
#include "../../Utils/OpenGLDebug.h"

#include <glad/glad.h>

#include "../Scene/Scene.h"
#include "../Scene/GameObjects/Components/TransformComponent.h"
#include "../Scene/GameObjects/Components/MeshComponent.h"
#include "../Scene/GameObjects/Components/MaterialComponent.h"
#include "../Scene/GameObjects/Components/CameraComponent.h"

#include <algorithm>
#include <cstddef>
#include <unordered_set>

#ifdef _DEBUG
#include <tracy/Tracy.hpp>
#endif


// -----------------------------------------------------------------------------
// Helper: compila y enlaza un programa de shaders registrando cualquier error
// -----------------------------------------------------------------------------
static unsigned int BuildProgram(const char* vertexSource, const char* fragmentSource, const char* label)
{
	char infoLog[512];
	int success = 0;

	unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &vertexSource, NULL);
	glCompileShader(vs);
	glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(vs, 512, NULL, infoLog);
		LOG_ERROR("[OpenGL] Error compilando Vertex Shader (%s): %s", label, infoLog);
	} else {
		LOG("[OpenGL] Vertex Shader (%s) compilado con éxito", label);
	}

	unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &fragmentSource, NULL);
	glCompileShader(fs);
	glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(fs, 512, NULL, infoLog);
		LOG_ERROR("[OpenGL] Error compilando Fragment Shader (%s): %s", label, infoLog);
	} else {
		LOG("[OpenGL] Fragment Shader (%s) compilado con éxito", label);
	}

	unsigned int program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(program, 512, NULL, infoLog);
		LOG_ERROR("[OpenGL] Error enlazando Shader Program (%s): %s", label, infoLog);
	} else {
		LOG("[OpenGL] Shader Program (%s) enlazado exitosamente (Program ID: %u)", label, program);
	}

	glDeleteShader(vs);
	glDeleteShader(fs);
	return program;
}

Render::Render() : Module()
{
	name = "render";
	// Gris azulado neutro (como el cielo por defecto de la vista Scene de Unity)
	background.r = 72;
	background.g = 77;
	background.b = 86;
	background.a = 255;
}

// Destructor
Render::~Render()
{
}

// Called before render is available
bool Render::Awake()
{
	LOG("Initializing OpenGL renderer");

	EnableOpenGLDebugOutput();

	// Enable Depth Test
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	// Face Culling
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW); // Counter-Clockwise

	// Alpha
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// V-Sync: Render es el único dueño de este estado
	SetVSync(settings.vsync);

	return true;
}

// Called before the first frame
bool Render::Start() {
	LOG("render start");

	const char* vertexShaderSource = "#version 460 core\n"
		"layout (location = 0) in vec3 aPos;\n"
		"layout (location = 1) in vec3 aNormal;\n"
		"layout (location = 2) in vec2 aTexCoords;\n"
		"out vec3 FragPos;\n"
		"out vec3 Normal;\n"
		"out vec2 TexCoords;\n"
		"uniform mat4 model;\n"
		"uniform mat4 view;\n"
		"uniform mat4 projection;\n"
		"void main()\n"
		"{\n"
		"   FragPos = vec3(model * vec4(aPos, 1.0));\n"
		"   Normal = mat3(transpose(inverse(model))) * aNormal;\n"
		"   TexCoords = aTexCoords;\n"
		"   gl_Position = projection * view * vec4(FragPos, 1.0);\n"
		"}\0";

	const char* fragmentShaderSource = "#version 460 core\n"
		"out vec4 FragColor;\n"
		"in vec3 FragPos;\n"
		"in vec3 Normal;\n"
		"in vec2 TexCoords;\n"
		"uniform vec4 uColor;\n"         
		"uniform bool uUseTexture;\n"
		"uniform bool uLighting;\n"
		"uniform sampler2D uTexture;\n"
		"void main()\n"
		"{\n"
		"   vec4 objectColor = uColor;\n"
		"   if (uUseTexture) {\n"
		"       objectColor *= texture(uTexture, TexCoords);\n" 
		"   }\n"
		"\n"
		"   // Alpha Test: Descarte de píxeles transparentes para corte de profundidad correcto\n"
		"   if (objectColor.a < 0.05)\n"
		"       discard;\n"
		"\n"
		"   vec3 result = objectColor.rgb;\n"
		"   if (uLighting) {\n"
		"       vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));\n"
		"       vec3 norm = normalize(Normal);\n"
		"       float ambientStrength = 0.25;\n"
		"       vec3 ambient = ambientStrength * vec3(1.0);\n"
		"       float diff = max(dot(norm, lightDir), 0.0);\n"
		"       vec3 diffuse = diff * vec3(1.0);\n"
		"       result = (ambient + diffuse) * objectColor.rgb;\n"
		"   }\n"
		"   FragColor = vec4(result, objectColor.a);\n"
		"}\0";

	shaderProgram = BuildProgram(vertexShaderSource, fragmentShaderSource, "Scene");

	/////////////////////  Line Shader (grid, ejes, normales y contorno de selección) /////////////////////////////
	const char* lineVS = "#version 460 core\n"
		"layout (location = 0) in vec3 aPos;\n"
		"uniform mat4 model;\n"
		"uniform mat4 view;\n"
		"uniform mat4 projection;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = projection * view * model * vec4(aPos, 1.0);\n"
		"}\0";

	const char* lineFS = "#version 460 core\n"
		"out vec4 FragColor;\n"
		"uniform vec3 uLineColor;\n"
		"void main()\n"
		"{\n"
		"   FragColor = vec4(uLineColor, 1.0);\n"
		"}\0";

	lineShaderProgram = BuildProgram(lineVS, lineFS, "Line");

	/////////////////////  Single Color Shader (Stencil Outline) /////////////////////////////
	const char* singleColorVS = 
		"#version 460 core\n"
		"layout (location = 0) in vec3 aPos;\n"
		"\n"
		"uniform mat4 model;\n"
		"uniform mat4 view;\n"
		"uniform mat4 projection;\n"
		"uniform float scaleFactor;\n" // Scale
		"\n"
		"void main()\n"
		"{\n"
		"   vec3 scaledPos = aPos * scaleFactor;\n"
		"   gl_Position = projection * view * model * vec4(scaledPos, 1.0);\n"
		"}\0";

	const char* singleColorFS = 
		"#version 460 core\n"
		"out vec4 FragColor;\n"
		"uniform vec3 uColor;\n"
		"void main()\n"
		"{\n"
		"   FragColor = vec4(uColor, 1.0);\n"
		"}\0";

	singleColorShaderProgram = BuildProgram(singleColorVS, singleColorFS, "SingleColor");

	///////////////////////////////////// Z Buffer //////////////////////////////////////////////////////////////
	const char* depthVS = R"(#version 460 core
	layout (location = 0) in vec2 aPos;
	layout (location = 1) in vec2 aTexCoords;
	out vec2 TexCoords;
	void main() {
		TexCoords = aTexCoords;
		gl_Position = vec4(aPos, 0.0, 1.0);
	})";

	const char* depthFS = R"(#version 460 core
	out vec4 FragColor;
	in vec2 TexCoords;

	uniform sampler2D depthMap;
	uniform float nearPlane;
	uniform float farPlane;

	float LinearizeDepth(float depth) {
		float z = depth * 2.0 - 1.0; // De [0, 1] a NDC [-1, 1]
		return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));
	}

	void main() {
		float depthVal = texture(depthMap, TexCoords).r;
    
		// Convertir a profundidad lineal en unidades del mundo
		float linearDepth = LinearizeDepth(depthVal);
    
		// Normalizar respecto a una distancia visiva razonable (ej. hasta 50 unidades)
		// o escalar exponencialmente para resaltar el degradado de grises
		float c = linearDepth / (farPlane * 0.1); // Multiplicar por un factor para ajustar la sensibilidad
		c = clamp(c, 0.0, 1.0);

		FragColor = vec4(vec3(c), 1.0);
	})";

	depthDebugProgram = BuildProgram(depthVS, depthFS, "DepthLinearizer");

	// Cuadrilátero para renderizar en pantalla completa el shader
	float quadVertices[] = {
		-1.0f,  1.0f,  0.0f, 1.0f,
		-1.0f, -1.0f,  0.0f, 0.0f,
		 1.0f, -1.0f,  1.0f, 0.0f,

		-1.0f,  1.0f,  0.0f, 1.0f,
		 1.0f, -1.0f,  1.0f, 0.0f,
		 1.0f,  1.0f,  1.0f, 1.0f
	};

	glGenVertexArrays(1, &depthDebugVAO);
	glGenBuffers(1, &depthDebugVBO);
	glBindVertexArray(depthDebugVAO);
	glBindBuffer(GL_ARRAY_BUFFER, depthDebugVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
	glBindVertexArray(0);

	/////////////////////  Gizmo Shader (AABB, frustum, octree, rayo: color por vertice) //////////////////////
	const char* gizmoVS = "#version 460 core\n"
		"layout (location = 0) in vec3 aPos;\n"
		"layout (location = 1) in vec3 aColor;\n"
		"uniform mat4 viewProjection;\n"
		"out vec3 vColor;\n"
		"void main()\n"
		"{\n"
		"   vColor = aColor;\n"
		"   gl_Position = viewProjection * vec4(aPos, 1.0);\n"
		"}\0";

	const char* gizmoFS = "#version 460 core\n"
		"in vec3 vColor;\n"
		"out vec4 FragColor;\n"
		"void main()\n"
		"{\n"
		"   FragColor = vec4(vColor, 1.0);\n"
		"}\0";

	debugLineProgram = BuildProgram(gizmoVS, gizmoFS, "Gizmos");
	debugLineViewProjection = glGetUniformLocation(debugLineProgram, "viewProjection");

	glGenVertexArrays(1, &gizmoVAO);
	glGenBuffers(1, &gizmoVBO);
	glBindVertexArray(gizmoVAO);
	glBindBuffer(GL_ARRAY_BUFFER, gizmoVBO);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugLineVertex), (void*)offsetof(DebugLineVertex, position));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(DebugLineVertex), (void*)offsetof(DebugLineVertex, color));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	gizmoLines.reserve(INITIAL_DEBUG_BUFFER_CAPACITY);

	////////////////////////////////////////////////////////////////////////////////////////////////////////

	// Ubicaciones de uniforms: se consultan una sola vez
	u.model = glGetUniformLocation(shaderProgram, "model");
	u.view = glGetUniformLocation(shaderProgram, "view");
	u.projection = glGetUniformLocation(shaderProgram, "projection");
	u.color = glGetUniformLocation(shaderProgram, "uColor");
	u.useTexture = glGetUniformLocation(shaderProgram, "uUseTexture");
	u.texture = glGetUniformLocation(shaderProgram, "uTexture");
	u.lighting = glGetUniformLocation(shaderProgram, "uLighting");

	lu.model = glGetUniformLocation(lineShaderProgram, "model");
	lu.view = glGetUniformLocation(lineShaderProgram, "view");
	lu.projection = glGetUniformLocation(lineShaderProgram, "projection");
	lu.color = glGetUniformLocation(lineShaderProgram, "uLineColor");

	scu.model = glGetUniformLocation(singleColorShaderProgram, "model");
	scu.view = glGetUniformLocation(singleColorShaderProgram, "view");
	scu.projection = glGetUniformLocation(singleColorShaderProgram, "projection");
	scu.color = glGetUniformLocation(singleColorShaderProgram, "uColor");
	scu.scaleFactor = glGetUniformLocation(singleColorShaderProgram, "scaleFactor");

	// Buffers persistentes para el dibujo de depuración
	glGenVertexArrays(1, &debugVAO);
	glGenBuffers(1, &debugVBO);
	glBindVertexArray(debugVAO);
	glBindBuffer(GL_ARRAY_BUFFER, debugVBO);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// Inicializar geometría persistente de Grid y Axis
	CreateGridAndAxisBuffers();

	int width = Engine::GetInstance().window->GetWidth();
	int height = Engine::GetInstance().window->GetHeight();
	viewportWidth = width > 0 ? width : 800;
	viewportHeight = height > 0 ? height : 600;
	CreateFramebuffer(viewportWidth, viewportHeight);

	gameViewportWidth = viewportWidth;
	gameViewportHeight = viewportHeight;
	CreateGameFramebuffer(gameViewportWidth, gameViewportHeight);

	return true;
}

void Render::CreateGridAndAxisBuffers()
{
	// ---------------------------------------------------------------------------------
	// Rejilla infinita (estilo Unity). Se dibuja un cuadrado que cubre toda la pantalla; para cada
	// pixel se reconstruye el rayo de la camara (deshaciendo view y projection) y se corta con el
	// plano de la rejilla. Las lineas se calculan con derivadas de pantalla (fwidth), asi miden
	// siempre ~1 pixel, y el espaciado cambia (1, 10, 100...) segun la distancia al plano.
	// ---------------------------------------------------------------------------------
	const char* gridVS = R"(#version 460 core
	layout (location = 0) in vec2 aPos;
	uniform mat4 invViewProjection;
	out vec3 nearPoint;
	out vec3 farPoint;
	vec3 Unproject(vec3 p)
	{
		vec4 r = invViewProjection * vec4(p, 1.0);
		return r.xyz / r.w;
	}
	void main()
	{
		nearPoint = Unproject(vec3(aPos, -1.0));
		farPoint = Unproject(vec3(aPos, 1.0));
		gl_Position = vec4(aPos, 0.0, 1.0);
	})";

	const char* gridFS = R"(#version 460 core
	in vec3 nearPoint;
	in vec3 farPoint;
	out vec4 FragColor;
	uniform mat4 viewProjection;
	uniform int planeAxis;      // 0: plano XZ (normal Y) | 1: plano XY (normal Z) | 2: plano ZY (normal X)
	uniform vec3 fadeOrigin;    // punto desde el que se desvanece la rejilla
	uniform float fadeDistance;
	uniform float lodHeight;    // distancia al plano: decide el espaciado de las lineas
	uniform bool showGrid;
	uniform bool showAxis;

	// Intensidad de una linea de rejilla de separacion 'spacing' en 'coord' (antialiasing con fwidth)
	float GridLine(vec2 coord, float spacing)
	{
		vec2 c = coord / spacing;
		vec2 d = fwidth(c);
		vec2 g = abs(fract(c - 0.5) - 0.5) / max(d, vec2(1e-6));
		return 1.0 - min(min(g.x, g.y), 1.0);
	}

	void main()
	{
		vec3 dir = farPoint - nearPoint;
		float denom = (planeAxis == 0) ? dir.y : ((planeAxis == 1) ? dir.z : dir.x);
		float origin = (planeAxis == 0) ? nearPoint.y : ((planeAxis == 1) ? nearPoint.z : nearPoint.x);
		if (abs(denom) < 1e-8) discard;
		float t = -origin / denom;
		if (t < 0.0 || t > 1.0) discard;

		vec3 p = nearPoint + t * dir;
		vec2 coord = (planeAxis == 0) ? p.xz : ((planeAxis == 1) ? p.xy : p.zy);

		// Profundidad del punto del plano: los objetos tapan la rejilla
		vec4 clip = viewProjection * vec4(p, 1.0);
		gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

		// Nivel de detalle: separacion 10^n, mezclando suavemente con el nivel siguiente
		float lod = max(0.0, log(max(lodHeight, 1e-3)) / log(10.0) - 0.35);
		float spacing = pow(10.0, floor(lod));
		float blend = fract(lod);

		// Las lineas finas se desvanecen antes que las gruesas (menos ruido a lo lejos)
		float minorFade = 1.0 - smoothstep(fadeDistance * 0.08, fadeDistance * 0.35, length(p - fadeOrigin));
		float minor = GridLine(coord, spacing) * (1.0 - blend) * minorFade;
		float major = GridLine(coord, spacing * 10.0);
		float alpha = showGrid ? max(minor * 0.30, major * 0.50) : 0.0;
		vec3 color = vec3(0.62);

		// Ejes sobre el plano. coord.x recorre el primer eje del plano y coord.y el segundo:
		//   XZ -> X rojo, Z azul | XY -> X rojo, Y verde | ZY -> Z azul, Y verde
		if (showAxis)
		{
			vec2 d = fwidth(coord);
			float alongFirst = 1.0 - min(abs(coord.y) / max(d.y, 1e-6) * 0.7, 1.0);  // linea coord.y == 0
			float alongSecond = 1.0 - min(abs(coord.x) / max(d.x, 1e-6) * 0.7, 1.0); // linea coord.x == 0
			vec3 red = vec3(0.90, 0.30, 0.30);
			vec3 green = vec3(0.40, 0.80, 0.30);
			vec3 blue = vec3(0.30, 0.50, 0.95);
			vec3 firstColor = (planeAxis == 2) ? blue : red;
			vec3 secondColor = (planeAxis == 0) ? blue : green;
			if (alongFirst > 0.0) { color = mix(color, firstColor, alongFirst); alpha = max(alpha, alongFirst * 0.9); }
			if (alongSecond > 0.0) { color = mix(color, secondColor, alongSecond); alpha = max(alpha, alongSecond * 0.9); }
		}

		// Se desvanece con la distancia
		float dist = length(p - fadeOrigin);
		float fade = 1.0 - smoothstep(fadeDistance * 0.35, fadeDistance, dist);
		alpha *= fade;
		if (alpha < 0.01) discard;
		FragColor = vec4(color, alpha);
	})";

	gridProgram = BuildProgram(gridVS, gridFS, "InfiniteGrid");
	gu.invViewProjection = glGetUniformLocation(gridProgram, "invViewProjection");
	gu.viewProjection = glGetUniformLocation(gridProgram, "viewProjection");
	gu.planeAxis = glGetUniformLocation(gridProgram, "planeAxis");
	gu.fadeOrigin = glGetUniformLocation(gridProgram, "fadeOrigin");
	gu.fadeDistance = glGetUniformLocation(gridProgram, "fadeDistance");
	gu.lodHeight = glGetUniformLocation(gridProgram, "lodHeight");
	gu.showGrid = glGetUniformLocation(gridProgram, "showGrid");
	gu.showAxis = glGetUniformLocation(gridProgram, "showAxis");
}

void Render::DestroyGridAndAxisBuffers()
{
	if (gridProgram != 0) { glDeleteProgram(gridProgram); gridProgram = 0; }
}

void Render::DrawGridAndAxis(const glm::mat4& view, const glm::mat4& projection)
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!settings.showGrid && !settings.showAxis) return;
	if (gridProgram == 0 || depthDebugVAO == 0) return;

	Scene* scene = Engine::GetInstance().scene.get();
	GameObject* cameraObj = scene ? scene->GetEditorCameraObject() : nullptr;
	const CameraComponent* camera = cameraObj ? cameraObj->GetComponent<CameraComponent>() : nullptr;
	if (camera == nullptr) return;

	// Plano de la rejilla: el suelo (XZ). En las vistas isometricas laterales (Right, Front...) el
	// suelo se ve de canto, asi que se usa el plano que mira a la camara, como hace Unity.
	int planeAxis = 0;
	const glm::vec3 front = camera->GetFront();
	if (camera->IsOrthographic())
	{
		const glm::vec3 a = glm::abs(front);
		if (a.z > a.y && a.z > a.x) planeAxis = 1;
		else if (a.x > a.y && a.x > a.z) planeAxis = 2;
	}

	const glm::vec3 cameraPos = camera->GetWorldPosition();
	const float halfHeight = camera->GetOrbitDistance() * std::tan(glm::radians(camera->fov) * 0.5f);
	float lodHeight = 0.0f;
	glm::vec3 fadeOrigin = cameraPos;
	float fadeDistance = 0.0f;
	if (camera->IsOrthographic())
	{
		lodHeight = halfHeight * 1.5f;
		fadeOrigin = camera->GetPivot();
		fadeDistance = std::max(halfHeight * 12.0f, 20.0f);
	}
	else
	{
		const float planeCoord = (planeAxis == 0) ? cameraPos.y : ((planeAxis == 1) ? cameraPos.z : cameraPos.x);
		lodHeight = std::abs(planeCoord);
		fadeDistance = std::clamp(std::max(lodHeight, camera->GetOrbitDistance()) * 25.0f, 30.0f, std::max(camera->farPlane, 30.0f));
	}

	const glm::mat4 viewProjection = projection * view;
	const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);

	glUseProgram(gridProgram);
	glUniformMatrix4fv(gu.invViewProjection, 1, GL_FALSE, glm::value_ptr(inverseViewProjection));
	glUniformMatrix4fv(gu.viewProjection, 1, GL_FALSE, glm::value_ptr(viewProjection));
	glUniform1i(gu.planeAxis, planeAxis);
	glUniform3fv(gu.fadeOrigin, 1, glm::value_ptr(fadeOrigin));
	glUniform1f(gu.fadeDistance, fadeDistance);
	glUniform1f(gu.lodHeight, lodHeight);
	glUniform1i(gu.showGrid, settings.showGrid ? 1 : 0);
	glUniform1i(gu.showAxis, settings.showAxis ? 1 : 0);

	// Transparente, con test de profundidad pero sin escribirla (no tapa nada de lo que venga despues)
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glBindVertexArray(depthDebugVAO);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);

	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);
	if (settings.cullFace) glEnable(GL_CULL_FACE);
}

void Render::SetViewportSize(int width, int height)
{
	if (width > 0 && height > 0)
	{
		viewportWidth = width;
		viewportHeight = height;
	}
}

void Render::SetGameViewportSize(int width, int height)
{
	if (width > 0 && height > 0)
	{
		gameViewportWidth = width;
		gameViewportHeight = height;
	}
}

void Render::DestroyGameFramebuffer()
{
	if (gameFramebuffer != 0) { glDeleteFramebuffers(1, &gameFramebuffer); gameFramebuffer = 0; }
	if (gameTextureColorbuffer != 0) { glDeleteTextures(1, &gameTextureColorbuffer); gameTextureColorbuffer = 0; }
	if (gameDepthTextureBuffer != 0) { glDeleteTextures(1, &gameDepthTextureBuffer); gameDepthTextureBuffer = 0; }
	gameFboComplete = false;
}

void Render::CreateGameFramebuffer(int width, int height)
{
	if (width <= 0 || height <= 0) return;

	gameFboWidth = width;
	gameFboHeight = height;

	DestroyGameFramebuffer();

	glGenFramebuffers(1, &gameFramebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, gameFramebuffer);

	glGenTextures(1, &gameTextureColorbuffer);
	glBindTexture(GL_TEXTURE_2D, gameTextureColorbuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gameTextureColorbuffer, 0);

	glGenTextures(1, &gameDepthTextureBuffer);
	glBindTexture(GL_TEXTURE_2D, gameDepthTextureBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, gameDepthTextureBuffer, 0);

	gameFboComplete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Render::DestroyFramebuffer()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (framebuffer != 0) { glDeleteFramebuffers(1, &framebuffer); framebuffer = 0; }
	if (textureColorbuffer != 0) { glDeleteTextures(1, &textureColorbuffer); textureColorbuffer = 0; }
	if (depthTextureBuffer != 0) { glDeleteTextures(1, &depthTextureBuffer); depthTextureBuffer = 0; }
	if (linearDepthTexture != 0) { glDeleteTextures(1, &linearDepthTexture); linearDepthTexture = 0; } // <- Añadido
	fboComplete = false;
}

void Render::CreateFramebuffer(int width, int height)
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (width <= 0 || height <= 0) return;

	fboWidth = width;
	fboHeight = height;

	DestroyFramebuffer();

	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	// 1. Textura de Color principal
	glGenTextures(1, &textureColorbuffer);
	glBindTexture(GL_TEXTURE_2D, textureColorbuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureColorbuffer, 0);

	// 2. Textura del Depth Buffer nativo de OpenGL
	glGenTextures(1, &depthTextureBuffer);
	glBindTexture(GL_TEXTURE_2D, depthTextureBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTextureBuffer, 0);

	// 3. Textura de Color procesada (Z-Buffer Lineal para ImGui)
	glGenTextures(1, &linearDepthTexture);
	glBindTexture(GL_TEXTURE_2D, linearDepthTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	fboComplete = (fbStatus == GL_FRAMEBUFFER_COMPLETE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Called each loop iteration
bool Render::PreUpdate()
{
#ifdef _DEBUG
	ZoneScoped;
#endif
	if (viewportVisible && viewportWidth > 0 && viewportHeight > 0)
	{
		if (viewportWidth != fboWidth || viewportHeight != fboHeight)
		{
			CreateFramebuffer(viewportWidth, viewportHeight);
		}
	}
	if (gameViewportVisible && gameViewportWidth > 0 && gameViewportHeight > 0)
	{
		if (gameViewportWidth != gameFboWidth || gameViewportHeight != gameFboHeight)
		{
			CreateGameFramebuffer(gameViewportWidth, gameViewportHeight);
		}
	}
	return true;
}

bool Render::Update(float dt)
{
#ifdef _DEBUG
	ZoneScoped;
#endif
	if (viewportVisible)
	{
		RenderScene();
	}
	if (gameViewportVisible)
	{
		RenderGame();
	}

	BindDefaultFramebuffer();

	// Player (build de un juego): la imagen de la camara principal ocupa toda la ventana
	if (Runtime::IsPlayer() && gameFramebuffer != 0 && gameFboComplete)
	{
		const int winW = Engine::GetInstance().window->GetWidth();
		const int winH = Engine::GetInstance().window->GetHeight();
		glBindFramebuffer(GL_READ_FRAMEBUFFER, gameFramebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glBlitFramebuffer(0, 0, gameFboWidth, gameFboHeight, 0, 0, winW, winH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	return true;
}

void Render::BindDefaultFramebuffer()
{
	// Unbind FBO and prepare default framebuffer for UI rendering
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	int winW = Engine::GetInstance().window->GetWidth();
	int winH = Engine::GetInstance().window->GetHeight();
	glViewport(0, 0, winW > 0 ? winW : 800, winH > 0 ? winH : 600);

	// Reset states that might affect ImGui
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_LINE);

	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void Render::RenderScene()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (framebuffer == 0 || fboWidth <= 0 || fboHeight <= 0) return;

	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glViewport(0, 0, fboWidth, fboHeight);

	// Configuración de Stencil y Clear
	glEnable(GL_STENCIL_TEST);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

	glClearColor(
		background.r / 255.0f,
		background.g / 255.0f,
		background.b / 255.0f,
		background.a / 255.0f
	);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	if (settings.depthTest) glEnable(GL_DEPTH_TEST);
	else glDisable(GL_DEPTH_TEST);

	if (settings.cullFace) glEnable(GL_CULL_FACE);
	else glDisable(GL_CULL_FACE);

	if (settings.shading == ShadingMode::Wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	BuildHighlightSet();

	glUseProgram(shaderProgram);

	Scene* scene = Engine::GetInstance().scene.get();
	GameObject* cameraObj = scene ? scene->GetEditorCameraObject() : nullptr;
	CameraComponent* camera = cameraObj ? cameraObj->GetComponent<CameraComponent>() : nullptr;

	const float aspectRatio = (fboHeight > 0) ? (float)fboWidth / (float)fboHeight : 1.0f;
	glm::mat4 view = glm::mat4(1.0f);
	glm::mat4 projection = glm::mat4(1.0f);
	glm::vec3 cameraPos = glm::vec3(0.0f);

	if (camera) {
		view = camera->GetViewMatrix();
		projection = camera->GetProjectionMatrix(aspectRatio);
		cameraPos = camera->GetWorldPosition();
	}

	glUniformMatrix4fv(u.view, 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(u.projection, 1, GL_FALSE, glm::value_ptr(projection));
	glUniform1i(u.lighting, settings.lighting ? 1 : 0);
	glUniform1i(u.texture, 0);

	// -------------------------------------------------------------------------
	// Frustum culling (con el octree): solo se dibuja lo que puede verse.
	//  - Normal: se usa el frustum de la camara del editor.
	//  - "Culling preview": se usa el frustum de la Main Camera, asi en la vista Scene se ve
	//    exactamente que objetos descarta la camara de juego.
	// -------------------------------------------------------------------------
	std::vector<GameObject*> visibleObjects;
	stats.sceneQuery = OctreeQueryStats();
	stats.sceneCulling = false;
	stats.sceneCandidates = 0;

	if (scene)
	{
		stats.sceneCandidates = static_cast<int>(scene->GetOctree().GetObjectCount());

		GameObject* gameCameraObj = scene->GetGameCameraObject();
		const CameraComponent* gameCamera = (gameCameraObj && gameCameraObj->IsActiveInHierarchy()) ?
			gameCameraObj->GetComponent<CameraComponent>() : nullptr;

		if (settings.cullingPreview && gameCamera && gameCamera->frustumCulling)
		{
			scene->CollectVisible(gameCamera->GetFrustum(GetGameAspectRatio()), visibleObjects, &stats.sceneQuery);
			stats.sceneCulling = true;
		}
		else if (settings.editorFrustumCulling && camera)
		{
			scene->CollectVisible(camera->GetFrustum(aspectRatio), visibleObjects, &stats.sceneQuery);
			stats.sceneCulling = true;
		}
		else
		{
			scene->CollectRenderables(visibleObjects);
		}
	}
	stats.sceneDrawn = static_cast<int>(visibleObjects.size());

	DrawObjects(visibleObjects, cameraPos);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// "Shaded Wireframe": las aristas de las mallas encima del sombreado
	if (settings.shading == ShadingMode::ShadedWireframe)
	{
		DrawShadedWireframe(visibleObjects, view, projection);
	}

	// -------------------------------------------------------------------------
	// PASE DE LINEARIZACIÓN DEL Z-BUFFER (Para ImGui)
	// -------------------------------------------------------------------------
	if (settings.showDepthBuffer && linearDepthTexture != 0 && depthTextureBuffer != 0)
	{
		// 1. Guardar estados actuales para no contaminar el pipeline
		GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);

		// 2. Apuntar temporalmente el FBO a la textura del Z-Buffer
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, linearDepthTexture, 0);

		glDisable(GL_DEPTH_TEST);
		glUseProgram(depthDebugProgram);

		float nearP = camera ? camera->nearPlane : 0.1f;
		float farP = camera ? camera->farPlane : 100.0f;

		glUniform1f(glGetUniformLocation(depthDebugProgram, "nearPlane"), nearP);
		glUniform1f(glGetUniformLocation(depthDebugProgram, "farPlane"), farP);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, depthTextureBuffer);
		glUniform1i(glGetUniformLocation(depthDebugProgram, "depthMap"), 0);

		glBindVertexArray(depthDebugVAO);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glBindVertexArray(0);

		// 3. RESTAURACIÓN STRICTA: Volver a conectar la textura de color principal a la escena
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureColorbuffer, 0);

		// 4. Restaurar el estado previo de Depth Test
		if (depthTestWasEnabled) {
			glEnable(GL_DEPTH_TEST);
		}
	}
	else if (linearDepthTexture != 0)
	{
		// Aseguramos que el FBO siempre vuelva a la textura de color original aunque showDepthBuffer se apague
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureColorbuffer, 0);
	}

	// -------------------------------------------------------------------------
	// Grid, Axis, Debug (normales y contorno) y Gizmos (AABB, frustum, octree, rayo)
	// -------------------------------------------------------------------------

	DrawGridAndAxis(view, projection);
	DrawDebug(view, projection);
	DrawSceneGizmos(view, projection);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

float Render::GetGameAspectRatio() const
{
	if (gameViewportWidth <= 0 || gameViewportHeight <= 0) return 16.0f / 9.0f;
	return static_cast<float>(gameViewportWidth) / static_cast<float>(gameViewportHeight);
}

void Render::DrawObjects(const std::vector<GameObject*>& objects, const glm::vec3& cameraPosition)
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	// -------------------------------------------------------------------------
	// Clasificación (Opacos vs Transparentes)
	// -------------------------------------------------------------------------
	struct RenderItem {
		GameObject* go;
		glm::mat4 modelMatrix;
		float distanceToCamera;
	};

	std::vector<RenderItem> opaqueItems;
	std::vector<RenderItem> transparentItems;
	opaqueItems.reserve(objects.size());

	for (GameObject* go : objects)
	{
		const glm::mat4 worldMatrix = go->GetWorldMatrix();
		const MaterialComponent* mat = go->GetComponent<MaterialComponent>();
		const bool isTransparent = mat && mat->IsActive() && mat->HasTransparentAlpha();
		const float dist = glm::length(cameraPosition - glm::vec3(worldMatrix[3]));

		if (isTransparent) transparentItems.push_back({ go, worldMatrix, dist });
		else opaqueItems.push_back({ go, worldMatrix, dist });
	}

	// Ordenar transparentes de más lejano a más cercano (Back-to-Front)
	std::sort(transparentItems.begin(), transparentItems.end(), [](const RenderItem& a, const RenderItem& b) {
		return a.distanceToCamera > b.distanceToCamera;
		});

	// -------------------------------------------------------------------------
	// Objetos Opacos (Escriben en Depth Buffer)
	// -------------------------------------------------------------------------
	glDepthMask(GL_TRUE);
	for (const auto& item : opaqueItems)
	{
		DrawSingleGameObject(item.go, item.modelMatrix);
	}

	// -------------------------------------------------------------------------
	// Objetos Transparentes (blending, sin escribir profundidad)
	// -------------------------------------------------------------------------
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glDepthMask(GL_FALSE);
	for (const auto& item : transparentItems)
	{
		DrawSingleGameObject(item.go, item.modelMatrix);
	}
	glDepthMask(GL_TRUE); // Restaurar la máscara por defecto

	glDisable(GL_BLEND);
}

void Render::AddGizmoLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color)
{
	gizmoLines.push_back({ a, color });
	gizmoLines.push_back({ b, color });
}

void Render::AddGizmoBox(const std::array<glm::vec3, 8>& corners, const glm::vec3& color)
{
	// Las esquinas estan ordenadas por bits (bit0 = X, bit1 = Y, bit2 = Z): cada arista une dos
	// esquinas que solo se diferencian en un bit
	static const int edges[12][2] = {
		{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },   // aristas en X
		{ 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },   // aristas en Y
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }    // aristas en Z
	};
	for (const auto& edge : edges)
	{
		AddGizmoLine(corners[edge[0]], corners[edge[1]], color);
	}
}

void Render::DrawSceneGizmos(const glm::mat4& view, const glm::mat4& projection)
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	Scene* scene = Engine::GetInstance().scene.get();
	if (!scene) return;

	gizmoLines.clear();

	// Main Camera (camara de juego): su frustum decide el color de las AABB
	GameObject* gameCameraObj = scene->GetGameCameraObject();
	const CameraComponent* gameCamera = (gameCameraObj && gameCameraObj->IsActiveInHierarchy()) ?
		gameCameraObj->GetComponent<CameraComponent>() : nullptr;
	Frustum gameFrustum;
	if (gameCamera) gameFrustum = gameCamera->GetFrustum(GetGameAspectRatio());

	// 1) Nodos del octree (azul)
	if (settings.showOctree)
	{
		std::vector<AABB> nodeBoxes;
		scene->GetOctree().CollectNodeBoxes(nodeBoxes);
		for (const AABB& box : nodeBoxes)
		{
			AddGizmoBox(box.GetCorners(), glm::vec3(0.25f, 0.55f, 0.95f));
		}
	}

	// 2) AABB de todas las mallas: verde = dentro del frustum de la Main Camera, rojo = descartada
	if (settings.showAABBs)
	{
		std::vector<GameObject*> renderables;
		scene->CollectRenderables(renderables);

		std::unordered_set<const GameObject*> visibleForGame;
		if (gameCamera)
		{
			std::vector<GameObject*> visible;
			scene->CollectVisible(gameFrustum, visible);
			visibleForGame.insert(visible.begin(), visible.end());
		}

		for (const GameObject* object : renderables)
		{
			const AABB box = object->GetWorldAABB(false);
			if (!box.IsValid()) continue;

			glm::vec3 color(0.9f, 0.9f, 0.9f); // sin Main Camera: gris
			if (gameCamera)
			{
				color = visibleForGame.count(object) > 0 ? glm::vec3(0.2f, 1.0f, 0.3f) : glm::vec3(1.0f, 0.25f, 0.25f);
			}
			AddGizmoBox(box.GetCorners(), color);
		}
	}

	// 3) AABB del objeto seleccionado (amarillo, incluye a sus hijos)
	if (settings.showSelectedAABB)
	{
		for (const GameObject* object : scene->GetTopLevelSelection())
		{
			const AABB box = object->GetWorldAABB(true);
			if (box.IsValid()) AddGizmoBox(box.GetCorners(), glm::vec3(1.0f, 0.85f, 0.1f));
		}
	}

	// 4) Frustum de la Main Camera (blanco) y de las camaras seleccionadas (como en Unity)
	if (settings.showCameraFrustum && gameCamera && gameFrustum.IsValid())
	{
		AddGizmoBox(gameFrustum.GetCorners(), glm::vec3(1.0f, 1.0f, 1.0f));
	}
	for (const GameObject* object : scene->GetSelection())
	{
		if (object == gameCameraObj && settings.showCameraFrustum) continue;
		const CameraComponent* selectedCamera = object->GetComponent<CameraComponent>();
		if (selectedCamera == nullptr || !object->IsActiveInHierarchy()) continue;
		const Frustum frustum = selectedCamera->GetFrustum(GetGameAspectRatio());
		if (frustum.IsValid()) AddGizmoBox(frustum.GetCorners(), glm::vec3(0.85f, 0.85f, 0.85f));
	}

	// 5) Ultimo rayo del mouse picking (magenta)
	if (settings.showPickRay && scene->HasLastPickRay())
	{
		const Ray& ray = scene->GetLastPickRay();
		AddGizmoLine(ray.origin, ray.GetPoint(scene->GetLastPickRayLength()), glm::vec3(1.0f, 0.2f, 1.0f));
	}

	FlushGizmoLines(projection * view);
}

void Render::FlushGizmoLines(const glm::mat4& viewProjection)
{
	if (gizmoLines.empty() || debugLineProgram == 0 || gizmoVAO == 0) return;

	glDisable(GL_STENCIL_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	glBindVertexArray(gizmoVAO);
	glBindBuffer(GL_ARRAY_BUFFER, gizmoVBO);

	// Solo se reserva memoria en la GPU cuando el buffer se queda pequeno
	const size_t bytes = gizmoLines.size() * sizeof(DebugLineVertex);
	if (bytes > gizmoVBOCapacityBytes)
	{
		gizmoVBOCapacityBytes = bytes * 2;
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)gizmoVBOCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
	}
	glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, gizmoLines.data());

	glUseProgram(debugLineProgram);
	glUniformMatrix4fv(debugLineViewProjection, 1, GL_FALSE, glm::value_ptr(viewProjection));

	glLineWidth(1.5f);
	glDrawArrays(GL_LINES, 0, (GLsizei)gizmoLines.size());
	glLineWidth(1.0f);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDepthFunc(GL_LESS);
}


bool Render::PostUpdate()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	SDL_Window* window = Engine::GetInstance().window->GetWindow();
	SDL_GL_SwapWindow(window);

	return true;
}

// Called before quitting
bool Render::CleanUp() 
{
	DestroyFramebuffer();
	DestroyGameFramebuffer();
	DestroyGridAndAxisBuffers();

	if (debugVBO != 0) { glDeleteBuffers(1, &debugVBO); debugVBO = 0; }
	if (debugVAO != 0) { glDeleteVertexArrays(1, &debugVAO); debugVAO = 0; }
	debugVBOCapacityBytes = 0;
	debugVertexLines.clear();
	debugVertexLines.shrink_to_fit();
	debugFaceLines.clear();
	debugFaceLines.shrink_to_fit();

	if (gizmoVBO != 0) { glDeleteBuffers(1, &gizmoVBO); gizmoVBO = 0; }
	if (gizmoVAO != 0) { glDeleteVertexArrays(1, &gizmoVAO); gizmoVAO = 0; }
	gizmoVBOCapacityBytes = 0;
	gizmoLines.clear();
	gizmoLines.shrink_to_fit();

	if (depthDebugVBO != 0) { glDeleteBuffers(1, &depthDebugVBO); depthDebugVBO = 0; }
	if (depthDebugVAO != 0) { glDeleteVertexArrays(1, &depthDebugVAO); depthDebugVAO = 0; }

	if (shaderProgram != 0) { glDeleteProgram(shaderProgram); shaderProgram = 0; }
	if (lineShaderProgram != 0) { glDeleteProgram(lineShaderProgram); lineShaderProgram = 0; }
	if (singleColorShaderProgram != 0) { glDeleteProgram(singleColorShaderProgram); singleColorShaderProgram = 0; }
	if (debugLineProgram != 0) { glDeleteProgram(debugLineProgram); debugLineProgram = 0; }
	if (depthDebugProgram != 0) { glDeleteProgram(depthDebugProgram); depthDebugProgram = 0; }

	LOG("[Render] Recursos OpenGL liberados (FBO, RBO, textura de color, shaders, buffers de depuración, Grid y Axis)");
	return true;
}

void Render::SetBackgroundColor(SDL_Color color)
{
	background = color;
}

void Render::SetSettings(const RenderSettings& newSettings)
{
	const bool vsyncChanged = (newSettings.vsync != settings.vsync);
	settings = newSettings;
	if (vsyncChanged)
	{
		SetVSync(settings.vsync);
	}
}

void Render::SetVSync(bool enabled)
{
	settings.vsync = enabled;
	if (!SDL_GL_SetSwapInterval(enabled ? 1 : 0))
	{
		LOG_WARNING("[OpenGL] No se pudo cambiar el V-Sync: %s", SDL_GetError());
		return;
	}
	LOG("[OpenGL] V-Sync %s", enabled ? "Habilitado (1)" : "Deshabilitado (0)");
}

void Render::DrawSingleGameObject(GameObject* go, const glm::mat4& worldMatrix)
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (!go || !go->IsActive()) return;

	MeshComponent* mesh = go->GetComponent<MeshComponent>();
	MaterialComponent* material = go->GetComponent<MaterialComponent>();

	if (mesh && mesh->GetPrimitive()) 
	{
		glUniformMatrix4fv(u.model, 1, GL_FALSE, glm::value_ptr(worldMatrix));

		glActiveTexture(GL_TEXTURE0);
		if (material && material->IsActive()) 
		{
			unsigned int texId = material->GetActiveTextureID();
			glUniform4fv(u.color, 1, glm::value_ptr(material->GetColor()));
			if (texId != 0) 
			{
				glBindTexture(GL_TEXTURE_2D, texId);
				glUniform1i(u.useTexture, 1);
			}
			else 
			{
				glUniform1i(u.useTexture, 0);
				glBindTexture(GL_TEXTURE_2D, 0);
			}
		}
		else 
		{
			glUniform4f(u.color, 0.8f, 0.8f, 0.8f, 1.0f);
			glUniform1i(u.useTexture, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		const bool isSelected = settings.highlightSelection && IsHighlighted(go);

		// Configurar Stencil para la selección (Aplica tanto a opacos como a transparentes)
		if (isSelected)
		{
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(GL_ALWAYS, 1, 0xFF); // Siempre pasa y escribe 1
			glStencilMask(0xFF);                // Habilita escritura en el stencil buffer
			glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);
		}

		mesh->GetPrimitive()->Draw();

		if (isSelected)
		{
			glStencilMask(0x00);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		}
	}
}
// -----------------------------------------------------------------------------
// Dibujo de depuración
// -----------------------------------------------------------------------------
void Render::DrawDebug(const glm::mat4& view, const glm::mat4& projection)
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	Scene* scene = Engine::GetInstance().scene.get();
	GameObject* selected = scene ? scene->GetSelected() : nullptr;

	const bool wantNormals = settings.normals != NormalsMode::None;
	const bool wantOutline = settings.highlightSelection && !highlightObjects.empty();
	if (!wantNormals && !wantOutline) return;

	if (wantOutline)
	{
		DrawSelectionOutline(view, projection);
	}

	if (wantNormals)
	{
		glUseProgram(lineShaderProgram);
		glUniformMatrix4fv(lu.view, 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(lu.projection, 1, GL_FALSE, glm::value_ptr(projection));

		// Limpiamos los elementos pero reservamos la memoria previamente para evitar reallocations
		debugVertexLines.clear();
		debugFaceLines.clear();

		// Reserva estimada para evitar petar la RAM con push_back() en cada frame
		debugVertexLines.reserve(10000);
		debugFaceLines.reserve(10000);

		if (settings.normalsAllObjects)
		{
			CollectNormals(scene->GetRoot(), glm::mat4(1.0f));
		}
		else if (selected)
		{
			glm::mat4 parentWorld = selected->GetParent() ? selected->GetParent()->GetWorldMatrix() : glm::mat4(1.0f);
			CollectNormals(selected, parentWorld);
		}

		const size_t vertexBytes = debugVertexLines.size() * sizeof(float);
		const size_t faceBytes = debugFaceLines.size() * sizeof(float);
		const size_t totalBytes = vertexBytes + faceBytes;

		if (totalBytes > 0)
		{
			glBindVertexArray(debugVAO);
			glBindBuffer(GL_ARRAY_BUFFER, debugVBO);

			// Reasignar buffer dinámico de OpenGL solo si la capacidad actual no es suficiente
			if (totalBytes > debugVBOCapacityBytes)
			{
				debugVBOCapacityBytes = totalBytes * 2;
				glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)debugVBOCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
			}

			if (vertexBytes > 0)
				glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)vertexBytes, debugVertexLines.data());
			if (faceBytes > 0)
				glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)vertexBytes, (GLsizeiptr)faceBytes, debugFaceLines.data());

			glUniformMatrix4fv(lu.model, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
			glLineWidth(1.5f);

			const GLsizei vertexCount = (GLsizei)(debugVertexLines.size() / 3);
			const GLsizei faceCount = (GLsizei)(debugFaceLines.size() / 3);

			if (vertexCount > 0)
			{
				glUniform3f(lu.color, 1.0f, 1.0f, 0.0f); // Amarillo: vértices
				glDrawArrays(GL_LINES, 0, vertexCount);
			}
			if (faceCount > 0)
			{
				glUniform3f(lu.color, 0.0f, 1.0f, 1.0f); // Cian: caras
				glDrawArrays(GL_LINES, vertexCount, faceCount);
			}

			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
	}
}

void Render::CollectNormals(GameObject* go, const glm::mat4& parentMatrix)
{
	if (!go || !go->IsActive()) return;

	glm::mat4 globalMatrix = parentMatrix;
	TransformComponent* transform = go->GetComponent<TransformComponent>();
	if (transform) {
		globalMatrix = parentMatrix * transform->GetLocalModelMatrix();
	}

	AppendNormals(go, globalMatrix);

	for (GameObject* child : go->GetChildren()) {
		CollectNormals(child, globalMatrix);
	}
}

void Render::AppendNormals(GameObject* go, const glm::mat4& worldMatrix)
{
	MeshComponent* mesh = go->GetComponent<MeshComponent>();
	if (!mesh || !mesh->GetPrimitive()) return;

	const Primitive* prim = mesh->GetPrimitive();
	const bool wantVertex = (settings.normals == NormalsMode::Vertex || settings.normals == NormalsMode::Both);
	const bool wantFace = (settings.normals == NormalsMode::Face || settings.normals == NormalsMode::Both);

	// Longitud proporcional al tamaño del objeto en mundo
	const float maxScale = std::max({ glm::length(glm::vec3(worldMatrix[0])), glm::length(glm::vec3(worldMatrix[1])), glm::length(glm::vec3(worldMatrix[2])) });
	const float diagonal = glm::length(prim->GetBoundsMax() - prim->GetBoundsMin());
	const float normalLength = std::max(0.05f, 0.1f * diagonal * maxScale);

	// Matriz para transformar la normal correctamente (sin escalas deformadas)
	const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(worldMatrix)));

	auto pushLine = [](std::vector<float>& buffer, const glm::vec3& a, const glm::vec3& b) {
		buffer.push_back(a.x); buffer.push_back(a.y); buffer.push_back(a.z);
		buffer.push_back(b.x); buffer.push_back(b.y); buffer.push_back(b.z);
	};

	if (wantVertex)
	{
		for (const Vertex& v : prim->GetVertices())
		{
			glm::vec3 worldPos = glm::vec3(worldMatrix * glm::vec4(v.position, 1.0f));
			glm::vec3 worldNormal = glm::normalize(normalMatrix * v.normal);
			pushLine(debugVertexLines, worldPos, worldPos + worldNormal * normalLength);
		}
	}

	if (wantFace)
	{
		for (const FaceData& f : prim->GetFaces())
		{
			glm::vec3 worldPos = glm::vec3(worldMatrix * glm::vec4(f.centroid, 1.0f));
			glm::vec3 worldNormal = glm::normalize(normalMatrix * f.normal);
			pushLine(debugFaceLines, worldPos, worldPos + worldNormal * normalLength);
		}
	}
}

bool Render::IsHighlighted(const GameObject* go) const
{
	return go != nullptr && highlightObjects.find(go) != highlightObjects.end();
}

void Render::BuildHighlightSet()
{
	highlightObjects.clear();
	highlightActive = nullptr;
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;

	highlightActive = scene->GetSelected();
	std::function<void(const GameObject*)> addRecursive = [&](const GameObject* node)
	{
		if (node == nullptr || !highlightObjects.insert(node).second) return;
		for (const GameObject* child : node->GetChildren()) addRecursive(child);
	};
	for (const GameObject* object : scene->GetSelection()) addRecursive(object);
}

void Render::DrawShadedWireframe(const std::vector<GameObject*>& objects, const glm::mat4& view, const glm::mat4& projection)
{
	glUseProgram(singleColorShaderProgram);
	glUniformMatrix4fv(scu.view, 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(scu.projection, 1, GL_FALSE, glm::value_ptr(projection));
	glUniform1f(scu.scaleFactor, 1.0f);
	glUniform3f(scu.color, 0.08f, 0.08f, 0.09f);

	// Las aristas no deben tocar el stencil (lo usa el contorno de seleccion)
	GLint previousStencilMask = 0xFF;
	glGetIntegerv(GL_STENCIL_WRITEMASK, &previousStencilMask);
	glStencilMask(0x00);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glEnable(GL_POLYGON_OFFSET_LINE);
	glPolygonOffset(-1.0f, -1.0f);

	for (GameObject* go : objects)
	{
		if (go == nullptr || !go->IsActive()) continue;
		MeshComponent* mesh = go->GetComponent<MeshComponent>();
		if (mesh == nullptr || mesh->GetPrimitive() == nullptr) continue;
		glUniformMatrix4fv(scu.model, 1, GL_FALSE, glm::value_ptr(go->GetWorldMatrix()));
		mesh->GetPrimitive()->Draw();
	}

	glDisable(GL_POLYGON_OFFSET_LINE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDepthFunc(GL_LESS);
	glEnable(GL_STENCIL_TEST);
	glStencilMask(static_cast<GLuint>(previousStencilMask));
}

void Render::DrawSelectionOutline(const glm::mat4& view, const glm::mat4& projection)
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (highlightObjects.empty()) return;

	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
	glStencilMask(0x00);

	glDisable(GL_DEPTH_TEST);

	glUseProgram(singleColorShaderProgram);
	glUniformMatrix4fv(scu.view, 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(scu.projection, 1, GL_FALSE, glm::value_ptr(projection));
	glUniform1f(scu.scaleFactor, 1.03f);
	// El objeto activo (y sus hijos) con el color del contorno; el resto de la seleccion algo mas claro
	const glm::vec3 activeColor = settings.outlineColor;
	const glm::vec3 otherColor = glm::mix(settings.outlineColor, glm::vec3(1.0f, 0.95f, 0.75f), 0.45f);

	for (const GameObject* node : highlightObjects)
	{
		if (!node->IsActiveInHierarchy()) continue;
		const MeshComponent* mesh = node->GetComponent<MeshComponent>();
		if (mesh == nullptr || mesh->GetPrimitive() == nullptr) continue;

		const bool inActiveGroup = (highlightActive != nullptr) && (node == highlightActive || node->IsDescendantOf(highlightActive));
		glUniform3fv(scu.color, 1, glm::value_ptr(inActiveGroup ? activeColor : otherColor));
		glUniformMatrix4fv(scu.model, 1, GL_FALSE, glm::value_ptr(node->GetWorldMatrix()));
		mesh->GetPrimitive()->Draw();
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	glStencilMask(0xFF);
	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glDisable(GL_STENCIL_TEST);
}

void Render::RenderGame()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	if (gameFramebuffer == 0 || gameFboWidth <= 0 || gameFboHeight <= 0) return;

	glBindFramebuffer(GL_FRAMEBUFFER, gameFramebuffer);
	glViewport(0, 0, gameFboWidth, gameFboHeight);

	glClearColor(
		background.r / 255.0f,
		background.g / 255.0f,
		background.b / 255.0f,
		background.a / 255.0f
	);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	Scene* scene = Engine::GetInstance().scene.get();
	// La vista Game dibuja la camara del display elegido (Display 1 = la Main Camera normalmente)
	GameObject* gameCameraObj = scene ? scene->GetCameraForDisplay(gameDisplay) : nullptr;
	CameraComponent* camera = (gameCameraObj && gameCameraObj->IsActiveInHierarchy()) ? gameCameraObj->GetComponent<CameraComponent>() : nullptr;

	stats.gameCandidates = 0;
	stats.gameDrawn = 0;
	stats.gameCulling = false;
	stats.gameQuery = OctreeQueryStats();

	// Si no hay Main Camera activa en la escena, dejamos el fondo limpio y salimos
	if (!camera || !camera->IsActive())
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return;
	}

	if (settings.depthTest) glEnable(GL_DEPTH_TEST);
	else glDisable(GL_DEPTH_TEST);

	if (settings.cullFace) glEnable(GL_CULL_FACE);
	else glDisable(GL_CULL_FACE);

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // La vista Game siempre se ve sólida

	glUseProgram(shaderProgram);

	float aspectRatio = (gameFboHeight > 0) ? (float)gameFboWidth / (float)gameFboHeight : 1.0f;
	glm::mat4 view = camera->GetViewMatrix();
	glm::mat4 projection = camera->GetProjectionMatrix(aspectRatio);
	glm::vec3 cameraPos = camera->GetWorldPosition();

	glUniformMatrix4fv(u.view, 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(u.projection, 1, GL_FALSE, glm::value_ptr(projection));
	glUniform1i(u.lighting, settings.lighting ? 1 : 0);
	glUniform1i(u.texture, 0);

	// Frustum culling de la camara de juego (acelerado con el octree)
	std::vector<GameObject*> visibleObjects;
	stats.gameCandidates = static_cast<int>(scene->GetOctree().GetObjectCount());
	if (camera->frustumCulling)
	{
		scene->CollectVisible(camera->GetFrustum(aspectRatio), visibleObjects, &stats.gameQuery);
		stats.gameCulling = true;
	}
	else
	{
		scene->CollectRenderables(visibleObjects);
	}
	stats.gameDrawn = static_cast<int>(visibleObjects.size());

	// Desactivamos temporalmente el highlight de selección para no ensuciar el stencil en la vista Game
	bool prevHighlight = settings.highlightSelection;
	settings.highlightSelection = false;

	DrawObjects(visibleObjects, cameraPos);

	settings.highlightSelection = prevHighlight;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
