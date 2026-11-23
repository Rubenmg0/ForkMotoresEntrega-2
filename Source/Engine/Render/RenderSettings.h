#pragma once

#include <glm/glm.hpp>

// Modo de visualización de normales (solo depuración, lo dibuja Render)
enum class NormalsMode
{
	None = 0,
	Vertex,  // una línea por vértice
	Face,    // una línea por triángulo (desde su centroide)
	Both
};

// Modo de dibujo de la vista Scene (menu "Shading Mode" de la vista Scene, como en Unity)
enum class ShadingMode
{
	Shaded = 0,
	Wireframe,
	ShadedWireframe
};

// Única fuente de verdad de las opciones de render. El Editor las edita a través de
// Render::SetSettings(); nadie más guarda copias persistentes de estos valores.
struct RenderSettings
{
	bool depthTest = true;
	bool cullFace = true;
	bool lighting = true;
	ShadingMode shading = ShadingMode::Shaded;
	bool vsync = true;

	bool showDepthBuffer = false;

	NormalsMode normals = NormalsMode::None;
	bool normalsAllObjects = false;   // false: solo el GameObject seleccionado
	bool highlightSelection = true;   // contorno del GameObject seleccionado
	glm::vec3 outlineColor = glm::vec3(1.0f, 0.6f, 0.1f);

	// Rejilla de referencia en el plano XZ y eje de coordenadas en el origen
	bool showGrid = true;
	bool showAxis = true;

	// ---------------- Gizmos de depuracion de la vista Scene ----------------
	bool showAABBs = false;            // AABB de todas las mallas: verde = la ve la Main Camera, rojo = descartada
	bool showSelectedAABB = true;      // AABB del objeto seleccionado (amarillo)
	bool showOctree = false;           // nodos del octree
	bool showCameraFrustum = true;     // frustum de la Main Camera
	bool showPickRay = true;           // ultimo rayo lanzado al hacer clic en la vista Scene

	// ---------------- Frustum culling ----------------
	bool editorFrustumCulling = true;  // la vista Scene no dibuja lo que esta fuera de la camara del editor
	bool cullingPreview = false;       // la vista Scene solo dibuja lo que ve la Main Camera (para ver el culling)
};
