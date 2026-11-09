#include "Octree.h"

#include <algorithm>

void Octree::Clear()
{
	root.reset();
	nodeCount = 0;
	depthReached = 0;
	objectCount = 0;
}

void Octree::Build(const std::vector<Entry>& entries)
{
	Clear();
	if (entries.empty()) return;

	// 1) Caja raiz = union de todas las AABB
	AABB bounds;
	for (const Entry& entry : entries)
	{
		bounds.Enclose(entry.box);
	}
	if (!bounds.IsValid()) return;

	// 2) La convertimos en un cubo (subdivisiones regulares) y la agrandamos un poco para
	//    que ningun objeto quede justo en el borde por errores de redondeo
	const glm::vec3 center = bounds.GetCenter();
	const glm::vec3 size = bounds.GetSize();
	const float halfSide = std::max({ size.x, size.y, size.z, 0.01f }) * 0.5f * 1.01f;
	const glm::vec3 half(halfSide);

	root = std::make_unique<Node>();
	root->box = AABB(center - half, center + half);
	root->depth = 0;
	nodeCount = 1;

	// 3) Insertamos los objetos uno a uno
	for (const Entry& entry : entries)
	{
		if (entry.object == nullptr || !entry.box.IsValid()) continue;
		Insert(*root, entry);
		++objectCount;
	}
}

void Octree::Insert(Node& node, const Entry& entry)
{
	if (node.IsLeaf())
	{
		// La hoja todavia tiene sitio (o ya no se puede dividir mas): se queda aqui
		if (node.entries.size() < MAX_OBJECTS_PER_NODE || node.depth >= MAX_DEPTH)
		{
			node.entries.push_back(entry);
			return;
		}

		// Hoja llena: se divide en 8 y se reparten los objetos que caben enteros en un hijo
		Subdivide(node);

		std::vector<Entry> previous;
		previous.swap(node.entries);
		for (const Entry& old : previous)
		{
			const int childIndex = FindContainingChild(node, old.box);
			if (childIndex >= 0) Insert(*node.children[childIndex], old);
			else node.entries.push_back(old); // ocupa varios hijos: se queda en el padre
		}
	}

	const int childIndex = FindContainingChild(node, entry.box);
	if (childIndex >= 0) Insert(*node.children[childIndex], entry);
	else node.entries.push_back(entry);
}

void Octree::Subdivide(Node& node)
{
	const glm::vec3 center = node.box.GetCenter();
	for (int i = 0; i < 8; ++i)
	{
		auto child = std::make_unique<Node>();
		// Cada bit del indice elige la mitad del eje: bit0 = X, bit1 = Y, bit2 = Z
		child->box.min = glm::vec3((i & 1) ? center.x : node.box.min.x,
			(i & 2) ? center.y : node.box.min.y,
			(i & 4) ? center.z : node.box.min.z);
		child->box.max = glm::vec3((i & 1) ? node.box.max.x : center.x,
			(i & 2) ? node.box.max.y : center.y,
			(i & 4) ? node.box.max.z : center.z);
		child->depth = node.depth + 1;
		node.children[i] = std::move(child);
	}
	nodeCount += 8;
	depthReached = std::max(depthReached, node.depth + 1);
}

int Octree::FindContainingChild(const Node& node, const AABB& box)
{
	if (node.IsLeaf()) return -1;

	// El hijo candidato es el que contiene el centro de la caja; despues comprobamos que la contenga entera
	const glm::vec3 nodeCenter = node.box.GetCenter();
	const glm::vec3 boxCenter = box.GetCenter();
	const int index = (boxCenter.x >= nodeCenter.x ? 1 : 0) |
		(boxCenter.y >= nodeCenter.y ? 2 : 0) |
		(boxCenter.z >= nodeCenter.z ? 4 : 0);

	return node.children[index]->box.Contains(box) ? index : -1;
}

void Octree::CollectVisible(const Frustum& frustum, std::vector<GameObject*>& outObjects, OctreeQueryStats* stats) const
{
	if (!root) return;
	CollectVisibleRecursive(*root, frustum, false, outObjects, stats);
}

void Octree::CollectVisibleRecursive(const Node& node, const Frustum& frustum, bool fullyInside,
	std::vector<GameObject*>& outObjects, OctreeQueryStats* stats) const
{
	if (stats) stats->nodesVisited++;

	bool inside = fullyInside;
	if (!inside)
	{
		const FrustumTestResult result = frustum.Test(node.box);
		if (result == FrustumTestResult::Outside) return; // se descarta todo lo que cuelga de este nodo
		inside = (result == FrustumTestResult::Inside);
	}

	for (const Entry& entry : node.entries)
	{
		if (inside)
		{
			outObjects.push_back(entry.object); // el nodo esta dentro: el objeto tambien
			continue;
		}
		if (stats) stats->objectsTested++;
		if (frustum.IsVisible(entry.box)) outObjects.push_back(entry.object);
	}

	if (!node.IsLeaf())
	{
		for (const std::unique_ptr<Node>& child : node.children)
		{
			CollectVisibleRecursive(*child, frustum, inside, outObjects, stats);
		}
	}
}

void Octree::CollectRayHits(const Ray& ray, std::vector<std::pair<float, GameObject*>>& outHits, OctreeQueryStats* stats) const
{
	if (!root) return;
	CollectRayHitsRecursive(*root, ray, outHits, stats);
}

void Octree::CollectRayHitsRecursive(const Node& node, const Ray& ray,
	std::vector<std::pair<float, GameObject*>>& outHits, OctreeQueryStats* stats) const
{
	float distance = 0.0f;
	if (!Intersection::RayAABB(ray, node.box, distance)) return; // el rayo no pasa por este nodo

	if (stats) stats->nodesVisited++;

	for (const Entry& entry : node.entries)
	{
		if (stats) stats->objectsTested++;
		if (Intersection::RayAABB(ray, entry.box, distance))
		{
			outHits.emplace_back(distance, entry.object);
		}
	}

	if (!node.IsLeaf())
	{
		for (const std::unique_ptr<Node>& child : node.children)
		{
			CollectRayHitsRecursive(*child, ray, outHits, stats);
		}
	}
}

void Octree::CollectNodeBoxes(std::vector<AABB>& outBoxes) const
{
	if (!root) return;
	CollectNodeBoxesRecursive(*root, outBoxes);
}

void Octree::CollectNodeBoxesRecursive(const Node& node, std::vector<AABB>& outBoxes) const
{
	outBoxes.push_back(node.box);
	if (node.IsLeaf()) return;
	for (const std::unique_ptr<Node>& child : node.children)
	{
		CollectNodeBoxesRecursive(*child, outBoxes);
	}
}
