#pragma once

#include "../Geometry/AABB.h"
#include "../Geometry/Frustum.h"
#include "../Geometry/Ray.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

class GameObject;

// Estadisticas de una consulta (se muestran en el editor para ver el ahorro del octree)
struct OctreeQueryStats
{
	int nodesVisited = 0;   // nodos del arbol que se han tenido que mirar
	int objectsTested = 0;  // objetos cuya AABB se ha comprobado individualmente
};

// =====================================================================================
// Octree: estructura de aceleracion espacial. Divide el espacio de la escena en una caja
// raiz que se subdivide en 8 hijos cuando contiene demasiados objetos.
//
// Cada objeto se guarda en el nodo MAS PEQUENO que contiene su AABB entera. Asi:
//   - Frustum culling: si un nodo queda fuera del frustum, se descartan de golpe TODOS
//     los objetos que cuelgan de el (sin comprobarlos uno a uno). Si un nodo queda
//     totalmente dentro, todos sus objetos son visibles sin mas tests.
//   - Mouse picking: solo se comprueban los objetos de los nodos que el rayo atraviesa.
//
// El octree lo reconstruye la escena automaticamente cuando algo cambia (un objeto se
// mueve, se crea, se borra o cambia de malla).
// =====================================================================================
class Octree
{
public:
	static constexpr size_t MAX_OBJECTS_PER_NODE = 8; // al superar este numero el nodo se divide
	static constexpr int MAX_DEPTH = 8;               // profundidad maxima del arbol

	struct Entry
	{
		GameObject* object = nullptr;
		AABB box; // AABB en espacio mundo del objeto
	};

	// Construye el arbol desde cero con los objetos indicados
	void Build(const std::vector<Entry>& entries);
	void Clear();

	// Frustum culling: devuelve los objetos cuya AABB no esta fuera del frustum
	void CollectVisible(const Frustum& frustum, std::vector<GameObject*>& outObjects, OctreeQueryStats* stats = nullptr) const;

	// Picking: devuelve (distancia, objeto) de los objetos cuya AABB atraviesa el rayo (sin ordenar)
	void CollectRayHits(const Ray& ray, std::vector<std::pair<float, GameObject*>>& outHits, OctreeQueryStats* stats = nullptr) const;

	// Cajas de todos los nodos (para dibujar el octree en el editor)
	void CollectNodeBoxes(std::vector<AABB>& outBoxes) const;

	bool IsEmpty() const { return root == nullptr; }
	AABB GetBounds() const { return root ? root->box : AABB(); }
	int GetNodeCount() const { return nodeCount; }
	int GetDepth() const { return depthReached; }
	size_t GetObjectCount() const { return objectCount; }

private:
	struct Node
	{
		AABB box;
		int depth = 0;
		std::vector<Entry> entries;                    // objetos que no caben enteros en ningun hijo
		std::array<std::unique_ptr<Node>, 8> children; // vacios mientras el nodo es una hoja

		bool IsLeaf() const { return children[0] == nullptr; }
	};

	void Insert(Node& node, const Entry& entry);
	void Subdivide(Node& node);
	static int FindContainingChild(const Node& node, const AABB& box);

	void CollectVisibleRecursive(const Node& node, const Frustum& frustum, bool fullyInside,
		std::vector<GameObject*>& outObjects, OctreeQueryStats* stats) const;
	void CollectRayHitsRecursive(const Node& node, const Ray& ray,
		std::vector<std::pair<float, GameObject*>>& outHits, OctreeQueryStats* stats) const;
	void CollectNodeBoxesRecursive(const Node& node, std::vector<AABB>& outBoxes) const;

	std::unique_ptr<Node> root;
	int nodeCount = 0;
	int depthReached = 0;
	size_t objectCount = 0;
};
