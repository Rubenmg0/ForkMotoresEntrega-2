#pragma once

#include "../Module.h"
#include "GameObjects/GameObject.h"
#include "Octree.h"
#include "../Geometry/Frustum.h"
#include "../Geometry/Ray.h"
#include "../ResourceManagement/Resource.h"
#include "../Render/Primitives/GeometryGenerator.h"

#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Estado de la simulacion (botones Play / Pause / Stop del editor)
enum class PlayState
{
	Stopped,  // modo edicion
	Playing,  // simulando
	Paused    // simulacion congelada (se puede avanzar frame a frame con Step)
};

// Estadisticas de la ultima seleccion con el raton (se muestran en el editor)
struct PickStats
{
	int candidates = 0;       // objetos cuya AABB toca el rayo
	int meshesTested = 0;     // objetos a los que se les han probado los triangulos
	int trianglesTested = 0;
	int nodesVisited = 0;     // nodos del octree visitados
	double milliseconds = 0.0;
};

class Scene : public Module
{
public:
	Scene();
	virtual ~Scene();

	bool Awake() override;
	bool Start() override;
	bool PreUpdate() override;
	bool Update(float dt) override;
	bool PostUpdate() override;
	bool CleanUp() override;

	// ------------------------------- CREACION -----------------------------------------
	// (parent == nullptr -> cuelga de la raiz de la escena)
	GameObject* CreateNewGameObject(std::vector<std::unique_ptr<Component>> objComponents, const std::string& name = "GameObject", GameObject* parent = nullptr);
	GameObject* CreateEmptyGameObject(GameObject* parent = nullptr);
	GameObject* CreatePrimitive(PrimitiveType type, GameObject* parent = nullptr);
	GameObject* CreateCamera(GameObject* parent = nullptr);
	// Crea un GameObject por cada nodo de un modelo importado (ResourceModel)
	GameObject* InstantiateModel(UID modelUID, GameObject* parent = nullptr);
	GameObject* DuplicateGameObject(GameObject* target, GameObject* parent = nullptr);

	// Destrucción recursiva en cascada y segura de GameObjects (se hace al inicio del siguiente frame)
	void DestroyGameObject(GameObject* target);

	// Archivo soltado en la ventana: modelo => importa e instancia, textura => se asigna al seleccionado
	void HandleDroppedFile(const std::string& filePath);

	GameObject* GetRoot() { return &sceneRoot; }
	const GameObject* GetRoot() const { return &sceneRoot; }
	size_t GetGameObjectCount() const { return sceneEntities.size(); }

	// Camara del editor (interna, no esta en la jerarquia) y camara de juego ("Main Camera")
	GameObject* GetEditorCameraObject() const { return editorCameraObj.get(); }
	GameObject* GetGameCameraObject() const { return gameCameraPtr; }
	void SetGameCamera(GameObject* cameraObject);

	// Selección única de la escena (la consumen Editor y cámara). nullptr = sin selección.
	// El root y objetos que no pertenezcan a la escena se rechazan.
	void SetSelected(GameObject* object);
	GameObject* GetSelected() const { return selected; }

	// Comprueba si un puntero a GameObject es propiedad viva de esta escena
	bool OwnsObject(const GameObject* object) const;
	GameObject* FindByUID(uint64_t uid) const;

	std::string MakeUniqueName(const std::string& baseName) const;
	GameObject* Register(std::unique_ptr<GameObject> newObj, GameObject* parent = nullptr);

	// El ResourceManager avisa de que estos recursos se han borrado: los componentes los sueltan
	void OnResourcesRemoved(const std::vector<UID>& uids);

	// ------------------------- OCTREE, CULLING Y PICKING ------------------------------
	// Devuelve el octree (si algo cambio desde la ultima vez, primero lo reconstruye)
	const Octree& GetOctree();
	void MarkOctreeDirty() { GameObject::MarkSpatialDirty(); }
	int GetOctreeRebuildCount() const { return octreeRebuilds; }

	// true: culling y picking usan el octree. false: prueban todos los objetos (para comparar)
	bool IsUsingOctree() const { return useOctree; }
	void SetUseOctree(bool enabled) { useOctree = enabled; }

	// Todos los GameObjects activos con malla (sin culling)
	void CollectRenderables(std::vector<GameObject*>& outObjects) const;
	// Solo los que estan dentro del frustum (frustum culling)
	void CollectVisible(const Frustum& frustum, std::vector<GameObject*>& outObjects, OctreeQueryStats* stats = nullptr);

	// Objeto mas cercano que atraviesa el rayo (test exacto contra los triangulos)
	GameObject* PickObject(const Ray& ray, float* outDistance = nullptr);
	const PickStats& GetLastPickStats() const { return lastPickStats; }

	// Ultimo rayo lanzado con el raton (se dibuja en la vista Scene para depurar)
	void SetLastPickRay(const Ray& ray, float length) { lastPickRay = ray; lastPickRayLength = length; hasLastPickRay = true; }
	bool HasLastPickRay() const { return hasLastPickRay; }
	const Ray& GetLastPickRay() const { return lastPickRay; }
	float GetLastPickRayLength() const { return lastPickRayLength; }

	// ------------------------------- ESCENAS ------------------------------------------
	// Guarda la escena actual en un fichero .scene (JSON) dentro de Assets
	bool SaveScene(const std::string& projectPath);
	// Carga/crea escenas al inicio del siguiente frame (asi no se destruyen objetos a mitad de frame)
	void RequestLoadScene(const std::string& projectPath);
	void RequestNewScene();
	void RequestDefaultScene();
	const std::string& GetCurrentScenePath() const { return currentScenePath; }

	std::string SerializeToString() const;
	bool DeserializeFromString(const std::string& text);

	// ------------------------------- MODO PLAY ----------------------------------------
	void Play();   // Stopped -> guarda el estado de la escena y empieza | Paused -> continua
	void Pause();  // alterna Playing <-> Paused
	void Stop();   // vuelve a modo edicion y restaura el estado guardado al pulsar Play
	void Step();   // estando en pausa, avanza un unico frame
	PlayState GetPlayState() const { return playState; }
	bool IsInPlayMode() const { return playState != PlayState::Stopped; }
	float GetGameTime() const { return gameTime; }      // segundos de simulacion
	float GetTimeScale() const { return timeScale; }
	void SetTimeScale(float scale) { timeScale = (scale < 0.0f) ? 0.0f : scale; }

private:
	void CreateEditorCamera();
	void CreateDefaultScene();
	void ClearScene();
	void ProcessPendingDestructions();
	void RebuildOctree();
	bool RayIntersectsMesh(const Ray& worldRay, const GameObject& object, float& outDistance, int& trianglesTested) const;

	nlohmann::json SerializeToJson() const;
	bool LoadFromJson(const nlohmann::json& root);

	GameObject sceneRoot;
	std::vector<std::unique_ptr<GameObject>> sceneEntities;    // duenos de todos los GameObjects
	std::unordered_map<uint64_t, GameObject*> objectsByUID;    // busqueda rapida por UID
	std::vector<GameObject*> pendingDestruction;

	std::unique_ptr<GameObject> editorCameraObj;
	GameObject* gameCameraPtr = nullptr;
	GameObject* selected = nullptr;

	// Octree (se reconstruye cuando cambia la "version espacial" de los GameObjects)
	Octree octree;
	uint64_t octreeVersion = 0;
	int octreeRebuilds = 0;
	bool useOctree = true;

	// Picking
	PickStats lastPickStats;
	Ray lastPickRay;
	float lastPickRayLength = 0.0f;
	bool hasLastPickRay = false;

	// Escenas
	std::string currentScenePath;
	std::string pendingLoadPath;
	bool pendingNewScene = false;
	bool pendingDefaultScene = false;

	// Modo Play
	PlayState playState = PlayState::Stopped;
	std::string playModeBackup;      // escena serializada al pulsar Play
	uint64_t playModeSelection = 0;  // UID del seleccionado al pulsar Play
	bool pendingRestore = false;
	bool stepRequested = false;
	float gameTime = 0.0f;
	float timeScale = 1.0f;
};
