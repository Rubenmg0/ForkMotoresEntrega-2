#include "../Engine.h"
#include "../Render/Render.h"
#include "../Input/Input.h"
#include "../../Utils/Log.h"
#include "../../Utils/PerfTimer.h"
#include "Scene.h"
#include "JsonHelpers.h"
#include "GameObjects/Components/TransformComponent.h"
#include "GameObjects/Components/MeshComponent.h"
#include "GameObjects/Components/MaterialComponent.h"
#include "GameObjects/Components/CameraComponent.h"
#include "../Render/Primitives/GeometryGenerator.h"
#include "../FileSystem/FileSystem.h"
#include "../Project/ProjectInfo.h"

#include "../ResourceManagement/ResourceManager.h"
#include "../ResourceManagement/ResourceModel.h"
#include "../ResourceManagement/Importers/ImporterManager.h"

#include <nlohmann/json.hpp>

#include <unordered_set>
#include <algorithm>
#include <cfloat>
#include <functional>

#ifdef _DEBUG
#include <tracy/Tracy.hpp>
#endif

namespace
{
	// Escena que debe aparecer al arrancar el motor (requisito de la entrega)
	constexpr const char* DEFAULT_MODEL_ASSET = "Assets/Street/StreetEnvironment.fbx";
	constexpr int SCENE_FILE_VERSION = 1;
}

Scene::Scene() : Module()
{
	name = "scene";
}

Scene::~Scene()
{
}

bool Scene::Awake()
{
	LOG("[Scene] Inicializando módulo Scene...");
	return true;
}

bool Scene::Start()
{
	CreateEditorCamera();

	// Escena inicial del proyecto:
	//   1) la ultima escena abierta / guardada (project.json -> lastScene)
	//   2) la calle de ejemplo, si el proyecto la tiene
	//   3) una escena vacia (proyectos nuevos creados desde el Hub)
	const std::string lastScene = Project::HasActive() ? Project::GetActive().lastScene : std::string();
	if (!lastScene.empty() && FileSystem::Exists(lastScene))
	{
		LOG("[Scene] Abriendo la ultima escena del proyecto: '%s'", lastScene.c_str());
		RequestLoadScene(lastScene);
	}
	else if (FileSystem::Exists(DEFAULT_MODEL_ASSET))
	{
		LOG("[Scene] Configurando escena por defecto...");
		CreateDefaultScene();
	}
	else
	{
		LOG("[Scene] Proyecto sin escenas: se crea una escena vacia");
		RequestNewScene();
	}
	return true;
}

void Scene::RememberProjectScene(const std::string& path)
{
	if (!Project::HasActive()) return;
	ProjectInfo& project = Project::GetActive();
	if (project.lastScene == path) return;
	project.lastScene = path;
	Project::SaveActive();
}

void Scene::CreateEditorCamera()
{
	// Camara del editor: interna, no pertenece a la jerarquia ni se guarda en las escenas
	editorCameraObj = std::make_unique<GameObject>("EditorCamera");
	auto camTrans = std::make_unique<TransformComponent>(editorCameraObj.get());
	camTrans->SetPosition(glm::vec3(0.0f, 1.0f, 5.0f));
	editorCameraObj->AddComponent(std::move(camTrans));

	auto camComp = std::make_unique<CameraComponent>(editorCameraObj.get());
	camComp->farPlane = 1000.0f;    // la calle mide unos 100 x 100 metros
	camComp->SetMovementSpeed(15.0f);
	editorCameraObj->AddComponent(std::move(camComp));

	LOG("[Scene] Instanciada 'EditorCamera' interna");
}

void Scene::CreateDefaultScene()
{
	ClearScene();
	currentScenePath.clear();

	// 1) Main Camera: la que ve la ventana Game y la que hace el frustum culling
	GameObject* camera = CreateCamera();
	camera->SetName("Main Camera");
	if (TransformComponent* transform = camera->GetComponent<TransformComponent>())
	{
		transform->SetPosition(glm::vec3(-36.0f, 20.0f, 36.0f));
		transform->LookAt(glm::vec3(0.0f, 0.0f, 0.0f));
	}
	if (CameraComponent* cameraComponent = camera->GetComponent<CameraComponent>())
	{
		cameraComponent->fov = 60.0f;
		cameraComponent->farPlane = 100.0f; // no llega a la esquina mas lejana: ahi se ve actuar el frustum culling
	}
	SetGameCamera(camera);

	// 2) La calle (StreetEnvironment.fbx) ya importada a Library por el ResourceManager
	ResourceManager* resources = Engine::GetInstance().resources.get();
	const UID streetModel = resources ? resources->GetAssetUID(DEFAULT_MODEL_ASSET) : INVALID_UID;
	GameObject* street = (streetModel != INVALID_UID) ? InstantiateModel(streetModel) : nullptr;
	if (street == nullptr)
	{
		LOG_ERROR("[Scene] No se pudo cargar la escena por defecto '%s'", DEFAULT_MODEL_ASSET);
	}

	// 3) Encuadramos la camara del editor para ver toda la escena
	if (CameraComponent* editorCamera = editorCameraObj ? editorCameraObj->GetComponent<CameraComponent>() : nullptr)
	{
		const AABB bounds = street ? street->GetWorldAABB(true) : AABB();
		if (bounds.IsValid())
		{
			const glm::vec3 center = bounds.GetCenter();
			const glm::vec3 size = bounds.GetSize();
			const float distance = std::max({ size.x, size.y, size.z, 10.0f });
			editorCamera->LookAtFrom(center + glm::vec3(distance * 0.55f, distance * 0.45f, distance * 0.75f), center);
		}
		else
		{
			editorCamera->ResetCamera();
		}
	}

	SetSelected(nullptr);
	LOG("[Scene] Escena inicial cargada con éxito (%zu entidades registradas)", sceneEntities.size());
}

void Scene::ClearScene()
{
	selection.clear();
	activeSelected = nullptr;
	++selectionVersion;
	gameCameraPtr = nullptr;
	pendingDestruction.clear();
	hasLastPickRay = false;

	// Desenganchamos los hijos de la raiz y despues liberamos la memoria
	while (!sceneRoot.GetChildren().empty())
	{
		sceneRoot.RemoveChild(sceneRoot.GetChildren().back());
	}
	objectsByUID.clear();
	sceneEntities.clear(); // los componentes sueltan aqui sus referencias a recursos

	octree.Clear();
	GameObject::MarkSpatialDirty();
}

bool Scene::PreUpdate()
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	ProcessPendingDestructions();

	// Peticiones de cambio de escena: se atienden aqui, al empezar el frame, cuando nadie
	// tiene punteros a GameObjects a medio usar
	if (pendingRestore)
	{
		pendingRestore = false;
		++sceneLoadVersion;
		if (!playModeBackup.empty() && DeserializeFromString(playModeBackup))
		{
			SelectByUIDs(playModeSelection, playModeActive);
			LOG("[Scene] Stop: escena restaurada al estado anterior a pulsar Play");
		}
		else
		{
			LOG_ERROR("[Scene] No se pudo restaurar el estado de la escena");
		}
		playModeBackup.clear();
	}

	// Undo / Redo: se restaura un estado guardado de la escena (los UID se conservan)
	if (pendingSnapshot)
	{
		pendingSnapshot = false;
		const std::string savedPath = currentScenePath;
		if (DeserializeFromString(snapshotText))
		{
			SelectByUIDs(snapshotSelection, snapshotActive);
		}
		else
		{
			LOG_ERROR("[Scene] Undo: no se pudo restaurar el estado de la escena");
		}
		currentScenePath = savedPath;
		snapshotText.clear();
	}

	if (pendingDefaultScene)
	{
		pendingDefaultScene = false;
		++sceneLoadVersion;
		CreateDefaultScene();
	}

	if (pendingNewScene)
	{
		pendingNewScene = false;
		++sceneLoadVersion;
		ClearScene();
		currentScenePath.clear();
		GameObject* camera = CreateCamera();
		camera->SetName("Main Camera");
		if (TransformComponent* transform = camera->GetComponent<TransformComponent>())
		{
			transform->SetPosition(glm::vec3(0.0f, 2.0f, 8.0f));
			transform->LookAt(glm::vec3(0.0f));
		}
		SetGameCamera(camera);
		LOG("[Scene] Nueva escena vacia creada");
	}

	if (!pendingLoadPath.empty())
	{
		const std::string path = pendingLoadPath;
		pendingLoadPath.clear();
		++sceneLoadVersion;

		std::string text;
		if (!FileSystem::ReadText(path, text))
		{
			LOG_ERROR("[Scene] No se pudo leer la escena '%s'", path.c_str());
		}
		else if (!DeserializeFromString(text))
		{
			LOG_ERROR("[Scene] '%s' no es una escena valida", path.c_str());
		}
		else
		{
			currentScenePath = path;
			RememberProjectScene(path);
			LOG("[Scene] Escena '%s' cargada (%zu GameObjects)", path.c_str(), sceneEntities.size());
		}
	}

	return true;
}

bool Scene::Update(float dt)
{
#ifdef _DEBUG
	ZoneScoped;
#endif

	// Centralización del drag & drop de archivos
	if (Engine::GetInstance().input && Engine::GetInstance().input->HasDroppedFile())
	{
		std::string droppedPath = Engine::GetInstance().input->GetDroppedFilePath();
		HandleDroppedFile(droppedPath);
		Engine::GetInstance().input->ClearDroppedFile();
	}

	// La camara del editor siempre usa el tiempo real
	if (editorCameraObj)
	{
		editorCameraObj->Update(dt);
	}

	// Los GameObjects de la escena usan el tiempo de JUEGO: 0 si la simulacion no esta en marcha
	float gameDt = 0.0f;
	if (playState == PlayState::Playing)
	{
		gameDt = dt * timeScale;
	}
	else if (playState == PlayState::Paused && stepRequested)
	{
		gameDt = (1000.0f / 60.0f) * timeScale; // un frame de 60 FPS
		stepRequested = false;
	}
	gameTime += gameDt / 1000.0f;

	sceneRoot.Update(gameDt);
	ProcessPendingDestructions();
	return true;
}

bool Scene::PostUpdate()
{
#ifdef _DEBUG
	ZoneScoped;
#endif
	return true;
}

bool Scene::CleanUp()
{
	LOG("[Scene] Limpiando entidades de la escena (%zu entidades)...", sceneEntities.size());
	ClearScene();
	editorCameraObj.reset();
	playModeBackup.clear();
	return true;
}

// =====================================================================================
// GESTION DE GAMEOBJECTS
// =====================================================================================
std::string Scene::MakeUniqueName(const std::string& baseName) const
{
	std::unordered_set<std::string> usedNames;
	usedNames.reserve(sceneEntities.size());
	for (const auto& entity : sceneEntities)
	{
		usedNames.insert(entity->GetName());
	}

	if (usedNames.find(baseName) == usedNames.end()) return baseName;

	for (int index = 1;; ++index)
	{
		std::string candidate = baseName + " (" + std::to_string(index) + ")";
		if (usedNames.find(candidate) == usedNames.end()) return candidate;
	}
}

bool Scene::OwnsObject(const GameObject* object) const
{
	if (!object || object == &sceneRoot) return false;

	const auto it = objectsByUID.find(static_cast<uint64_t>(object->GetUUID()));
	if (it == objectsByUID.end() || it->second != object) return false;

	// Si está encolado para destrucción, ya no se considera disponible
	return std::find(pendingDestruction.begin(), pendingDestruction.end(), object) == pendingDestruction.end();
}

GameObject* Scene::FindByUID(uint64_t uid) const
{
	if (uid == 0) return nullptr;
	const auto it = objectsByUID.find(uid);
	if (it == objectsByUID.end()) return nullptr;
	return OwnsObject(it->second) ? it->second : nullptr;
}

// =====================================================================================
// SELECCION (multiple, estilo Unity)
// =====================================================================================
bool Scene::IsSelected(const GameObject* object) const
{
	return object != nullptr && std::find(selection.begin(), selection.end(), object) != selection.end();
}

void Scene::Select(GameObject* object)
{
	const bool valid = object != nullptr && OwnsObject(object);
	if (valid && selection.size() == 1 && selection[0] == object && activeSelected == object) return;
	if (!valid && selection.empty() && activeSelected == nullptr) return;

	selection.clear();
	activeSelected = nullptr;
	if (valid)
	{
		selection.push_back(object);
		activeSelected = object;
	}
	++selectionVersion;
}

void Scene::AddToSelection(GameObject* object)
{
	if (object == nullptr || !OwnsObject(object)) return;
	if (!IsSelected(object)) selection.push_back(object);
	activeSelected = object;
	++selectionVersion;
}

void Scene::RemoveFromSelection(GameObject* object)
{
	auto it = std::find(selection.begin(), selection.end(), object);
	if (it == selection.end()) return;
	selection.erase(it);
	if (activeSelected == object) activeSelected = selection.empty() ? nullptr : selection.back();
	++selectionVersion;
}

void Scene::ToggleSelection(GameObject* object)
{
	if (IsSelected(object)) RemoveFromSelection(object);
	else AddToSelection(object);
}

void Scene::SetSelection(const std::vector<GameObject*>& objects, GameObject* active)
{
	selection.clear();
	for (GameObject* object : objects)
	{
		if (object != nullptr && OwnsObject(object) && !IsSelected(object)) selection.push_back(object);
	}
	activeSelected = (active != nullptr && IsSelected(active)) ? active : (selection.empty() ? nullptr : selection.back());
	++selectionVersion;
}

void Scene::ClearSelection()
{
	if (selection.empty() && activeSelected == nullptr) return;
	selection.clear();
	activeSelected = nullptr;
	++selectionVersion;
}

void Scene::SelectAll()
{
	std::vector<GameObject*> all;
	all.reserve(sceneEntities.size());
	for (const auto& entity : sceneEntities)
	{
		if (OwnsObject(entity.get())) all.push_back(entity.get());
	}
	SetSelection(all, activeSelected);
}

std::vector<GameObject*> Scene::GetTopLevelSelection() const
{
	std::vector<GameObject*> result;
	for (GameObject* object : selection)
	{
		bool hasSelectedAncestor = false;
		for (GameObject* other : selection)
		{
			if (other != object && object->IsDescendantOf(other))
			{
				hasSelectedAncestor = true;
				break;
			}
		}
		if (!hasSelectedAncestor) result.push_back(object);
	}
	return result;
}

std::vector<uint64_t> Scene::GetSelectionUIDs() const
{
	std::vector<uint64_t> uids;
	uids.reserve(selection.size());
	for (const GameObject* object : selection) uids.push_back(static_cast<uint64_t>(object->GetUUID()));
	return uids;
}

void Scene::SelectByUIDs(const std::vector<uint64_t>& uids, uint64_t activeUID)
{
	std::vector<GameObject*> objects;
	for (uint64_t uid : uids)
	{
		if (GameObject* object = FindByUID(uid)) objects.push_back(object);
	}
	SetSelection(objects, FindByUID(activeUID));
}

void Scene::RequestRestoreSnapshot(const std::string& text, const std::vector<uint64_t>& selectionUIDs, uint64_t activeUID)
{
	pendingSnapshot = true;
	snapshotText = text;
	snapshotSelection = selectionUIDs;
	snapshotActive = activeUID;
}

std::vector<GameObject*> Scene::GetCameras() const
{
	std::vector<GameObject*> cameras;
	std::vector<GameObject*> stack(sceneRoot.GetChildren().rbegin(), sceneRoot.GetChildren().rend());
	while (!stack.empty())
	{
		GameObject* node = stack.back();
		stack.pop_back();
		if (node->GetComponent<CameraComponent>() != nullptr && OwnsObject(node)) cameras.push_back(node);
		const std::vector<GameObject*>& children = node->GetChildren();
		for (auto it = children.rbegin(); it != children.rend(); ++it) stack.push_back(*it);
	}
	return cameras;
}

GameObject* Scene::GetCameraForDisplay(int display) const
{
	const auto renders = [display](const GameObject* object)
	{
		const CameraComponent* camera = object ? object->GetComponent<CameraComponent>() : nullptr;
		return camera && camera->IsActive() && object->IsActiveInHierarchy() && camera->targetDisplay == display;
	};
	if (renders(gameCameraPtr)) return gameCameraPtr;
	for (GameObject* camera : GetCameras())
	{
		if (renders(camera)) return camera;
	}
	return nullptr;
}

void Scene::SetGameCamera(GameObject* cameraObject)
{
	if (cameraObject == nullptr || (OwnsObject(cameraObject) && cameraObject->GetComponent<CameraComponent>() != nullptr))
	{
		gameCameraPtr = cameraObject;
	}
}

GameObject* Scene::Register(std::unique_ptr<GameObject> newObj, GameObject* parent)
{
	GameObject* ptr = newObj.get();
	GameObject* finalParent = (parent != nullptr && (parent == &sceneRoot || OwnsObject(parent))) ? parent : &sceneRoot;

	// false = se conserva el transform LOCAL (un hijo nuevo aparece en la posicion de su padre)
	ptr->SetParent(finalParent, false);

	objectsByUID[static_cast<uint64_t>(ptr->GetUUID())] = ptr;
	sceneEntities.push_back(std::move(newObj));
	GameObject::MarkSpatialDirty();
	return ptr;
}

GameObject* Scene::CreateNewGameObject(std::vector<std::unique_ptr<Component>> objComponents, const std::string& name, GameObject* parent)
{
	std::string uniqueName = MakeUniqueName(name);
	auto newObj = std::make_unique<GameObject>(uniqueName);

	// El Transform siempre va primero (otros componentes, como la camara, lo necesitan)
	bool hasTransform = false;
	for (const auto& comp : objComponents)
	{
		if (comp && comp->GetType() == COMPONENT_TRANSFORM) hasTransform = true;
	}
	if (!hasTransform)
	{
		newObj->AddComponent(std::make_unique<TransformComponent>(newObj.get()));
	}

	for (auto& comp : objComponents)
	{
		if (comp) newObj->AddComponent(std::move(comp));
	}

	GameObject* registered = Register(std::move(newObj), parent);
	LOG("[Scene] GameObject '%s' creado (Total entidades: %zu)", uniqueName.c_str(), sceneEntities.size());
	return registered;
}

GameObject* Scene::CreateEmptyGameObject(GameObject* parent)
{
	std::string uniqueName = MakeUniqueName("GameObject");
	auto newObj = std::make_unique<GameObject>(uniqueName);
	newObj->AddComponent(std::make_unique<TransformComponent>(newObj.get()));

	GameObject* registered = Register(std::move(newObj), parent);
	LOG("[Scene] Empty GameObject '%s' creado (Total entidades: %zu)", uniqueName.c_str(), sceneEntities.size());
	return registered;
}

GameObject* Scene::CreatePrimitive(PrimitiveType type, GameObject* parent)
{
	std::string uniqueName = MakeUniqueName(GeometryGenerator::GetName(type));
	auto newObj = std::make_unique<GameObject>(uniqueName);
	newObj->AddComponent(std::make_unique<TransformComponent>(newObj.get()));

	// Las primitivas son mallas "built-in" del ResourceManager (tambien cuentan referencias)
	newObj->AddComponent(std::make_unique<MeshComponent>(newObj.get(), ResourceManager::GetBuiltInMeshUID(type)));

	auto mat = std::make_unique<MaterialComponent>(newObj.get());
	mat->SetColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	mat->SetUseTexture(false);
	newObj->AddComponent(std::move(mat));

	GameObject* registered = Register(std::move(newObj), parent);
	LOG("[Scene] Primitiva '%s' creada (Total entidades: %zu)", uniqueName.c_str(), sceneEntities.size());
	return registered;
}

GameObject* Scene::CreateCamera(GameObject* parent)
{
	auto newObj = std::make_unique<GameObject>(MakeUniqueName("Camera"));
	newObj->AddComponent(std::make_unique<TransformComponent>(newObj.get()));
	newObj->AddComponent(std::make_unique<CameraComponent>(newObj.get()));

	// Cada camara nueva va al primer display que no usa ninguna otra (asi se puede ver en la
	// vista Game eligiendo su display); la primera de la escena es la Main Camera, en el Display 1
	bool used[CameraComponent::MAX_DISPLAYS] = {};
	for (const GameObject* camera : GetCameras())
	{
		used[camera->GetComponent<CameraComponent>()->targetDisplay] = true;
	}
	for (int display = 0; display < CameraComponent::MAX_DISPLAYS; ++display)
	{
		if (!used[display])
		{
			newObj->GetComponent<CameraComponent>()->targetDisplay = display;
			break;
		}
	}

	GameObject* registered = Register(std::move(newObj), parent);

	// Si la escena no tiene camara de juego, esta pasa a serlo
	if (gameCameraPtr == nullptr) SetGameCamera(registered);
	LOG("[Scene] Camara '%s' creada", registered->GetName().c_str());
	return registered;
}

GameObject* Scene::InstantiateModel(UID modelUID, GameObject* parent)
{
	ResourceManager* resources = Engine::GetInstance().resources.get();
	if (!resources) return nullptr;

	// Pedimos el modelo solo mientras lo instanciamos (es la "receta"); las mallas y texturas
	// las piden los componentes y se quedan contadas mientras existan los GameObjects
	Resource* resource = resources->RequestResource(modelUID);
	if (resource == nullptr || resource->GetType() != ResourceType::Model || !resource->IsLoaded())
	{
		if (resource) resources->ReleaseResource(modelUID);
		LOG_ERROR("[Scene] El modelo con UID %llu no existe o no se pudo cargar", (unsigned long long)modelUID);
		return nullptr;
	}

	const std::string modelName = resource->GetName();
	const std::vector<ModelNode>& nodes = static_cast<const ResourceModel*>(resource)->GetNodes();
	std::vector<GameObject*> created(nodes.size(), nullptr);

	for (size_t i = 0; i < nodes.size(); ++i)
	{
		const ModelNode& node = nodes[i];
		auto go = std::make_unique<GameObject>(i == 0 ? MakeUniqueName(node.name) : node.name);

		auto transform = std::make_unique<TransformComponent>(go.get());
		transform->SetPosition(node.position);
		transform->SetRotation(node.rotation);
		transform->SetScale(node.scale);
		go->AddComponent(std::move(transform));

		if (node.mesh != INVALID_UID)
		{
			go->AddComponent(std::make_unique<MeshComponent>(go.get(), node.mesh));

			auto material = std::make_unique<MaterialComponent>(go.get());
			material->SetColor(node.color);
			material->SetTexture(node.texture);
			material->SetUseTexture(node.texture != INVALID_UID);
			go->AddComponent(std::move(material));
		}

		if (node.isCamera)
		{
			go->AddComponent(std::make_unique<CameraComponent>(go.get()));
		}

		// Los nodos vienen ordenados: el padre de 'i' siempre se ha creado antes
		GameObject* nodeParent = (node.parent >= 0 && node.parent < static_cast<int>(i)) ? created[node.parent] : parent;
		created[i] = Register(std::move(go), nodeParent);
	}

	GameObject* root = created.empty() ? nullptr : created[0];
	if (root)
	{
		LOG("[Scene] Modelo '%s' instanciado (%zu GameObjects)", modelName.c_str(), created.size());
	}

	// Ya no necesitamos la "receta": si nadie mas la usa se descargara al empezar el siguiente frame
	resources->ReleaseResource(modelUID);
	return root;
}

void Scene::DestroyGameObject(GameObject* target)
{
	if (target == nullptr || target == &sceneRoot) return;

	// Verificar pertenencia real a la escena
	if (!OwnsObject(target)) return;

	std::vector<GameObject*> toDestroy;
	std::function<void(GameObject*)> collectPostOrder = [&](GameObject* node)
	{
		for (GameObject* child : node->GetChildren())
		{
			collectPostOrder(child);
		}
		toDestroy.push_back(node);
	};
	collectPostOrder(target);

	for (GameObject* obj : toDestroy)
	{
		RemoveFromSelection(obj);
	}

	// Si la cámara de juego estuviera en el subárbol, desvincularla para no dejar puntero colgante
	for (GameObject* obj : toDestroy)
	{
		if (gameCameraPtr == obj) gameCameraPtr = nullptr;
	}

	// Desvinculación inmediata del padre superior
	if (target->GetParent())
	{
		target->GetParent()->RemoveChild(target);
	}

	for (GameObject* obj : toDestroy)
	{
		if (std::find(pendingDestruction.begin(), pendingDestruction.end(), obj) == pendingDestruction.end())
		{
			pendingDestruction.push_back(obj);
		}
	}

	GameObject::MarkSpatialDirty();
	LOG("[Scene] GameObject '%s' y %zu descendiente(s) marcados para eliminación segura",
		target->GetName().c_str(), toDestroy.size() - 1);
}

void Scene::ProcessPendingDestructions()
{
	if (pendingDestruction.empty()) return;

	std::unordered_set<GameObject*> destroySet(pendingDestruction.begin(), pendingDestruction.end());
	pendingDestruction.clear();

	// Desvincular relaciones padre-hijo internas antes de la destrucción de memoria
	for (GameObject* obj : destroySet)
	{
		if (obj->GetParent() && destroySet.find(obj->GetParent()) == destroySet.end())
		{
			obj->GetParent()->RemoveChild(obj);
		}
		objectsByUID.erase(static_cast<uint64_t>(obj->GetUUID()));
	}

	const size_t initialCount = sceneEntities.size();
	sceneEntities.erase(
		std::remove_if(sceneEntities.begin(), sceneEntities.end(), [&](const std::unique_ptr<GameObject>& ptr)
		{
			return destroySet.find(ptr.get()) != destroySet.end();
		}),
		sceneEntities.end()
	);

	GameObject::MarkSpatialDirty();
	LOG("[Scene] Liberados %zu GameObjects de la memoria de la escena (%zu restantes)",
		initialCount - sceneEntities.size(), sceneEntities.size());
}

GameObject* Scene::DuplicateGameObject(GameObject* target, GameObject* parent)
{
	if (target == nullptr || !OwnsObject(target))
	{
		LOG_WARNING("[Scene] No se puede duplicar: GameObject nulo o no pertenece a la escena.");
		return nullptr;
	}

	GameObject* targetParent = (parent != nullptr) ? parent : target->GetParent();
	if (targetParent == nullptr)
	{
		targetParent = &sceneRoot;
	}

	// El clon copia todos los componentes (y pide sus propias referencias a mallas y texturas)
	std::function<GameObject*(GameObject*, GameObject*)> duplicateRecursive =
		[&](GameObject* src, GameObject* parentNode) -> GameObject*
	{
		std::unique_ptr<GameObject> clone = src->Duplicate();
		clone->SetName(MakeUniqueName(src->GetName()));
		GameObject* registeredClone = Register(std::move(clone), parentNode);

		for (GameObject* child : src->GetChildren())
		{
			duplicateRecursive(child, registeredClone);
		}

		return registeredClone;
	};

	GameObject* duplicatedRoot = duplicateRecursive(target, targetParent);
	SetSelected(duplicatedRoot);
	LOG("[Scene] GameObject '%s' duplicado como '%s' (Total entidades: %zu)",
		target->GetName().c_str(), duplicatedRoot->GetName().c_str(), sceneEntities.size());

	return duplicatedRoot;
}

void Scene::HandleDroppedFile(const std::string& filePath)
{
	ImporterManager::HandleDroppedFile(filePath, activeSelected);
}

void Scene::OnResourcesRemoved(const std::vector<UID>& uids)
{
	if (uids.empty() || sceneEntities.empty()) return;

	const std::unordered_set<UID> removed(uids.begin(), uids.end());
	size_t affected = 0;
	for (const auto& entity : sceneEntities)
	{
		if (MeshComponent* mesh = entity->GetComponent<MeshComponent>())
		{
			if (mesh->GetMeshUID() != INVALID_UID && removed.count(mesh->GetMeshUID()) > 0)
			{
				mesh->MarkMeshMissing();
				++affected;
			}
		}
		if (MaterialComponent* material = entity->GetComponent<MaterialComponent>())
		{
			if (material->GetTextureUID() != INVALID_UID && removed.count(material->GetTextureUID()) > 0)
			{
				material->MarkTextureMissing();
				++affected;
			}
		}
	}

	if (affected > 0)
	{
		LOG_WARNING("[Scene] %zu componente(s) usaban recursos borrados y se han quedado sin ellos", affected);
	}
}

// =====================================================================================
// OCTREE, CULLING Y PICKING
// =====================================================================================
const Octree& Scene::GetOctree()
{
	if (octreeVersion != GameObject::GetSpatialVersion())
	{
		RebuildOctree();
	}
	return octree;
}

void Scene::RebuildOctree()
{
	std::vector<GameObject*> renderables;
	CollectRenderables(renderables);

	std::vector<Octree::Entry> entries;
	entries.reserve(renderables.size());
	for (GameObject* object : renderables)
	{
		const AABB box = object->GetWorldAABB(false);
		if (box.IsValid()) entries.push_back({ object, box });
	}

	octree.Build(entries);
	octreeVersion = GameObject::GetSpatialVersion();
	++octreeRebuilds;
}

void Scene::CollectRenderables(std::vector<GameObject*>& outObjects) const
{
	// Recorrido en profundidad con pila: si un objeto esta desactivado se salta toda su rama
	std::vector<GameObject*> stack(sceneRoot.GetChildren().rbegin(), sceneRoot.GetChildren().rend());
	while (!stack.empty())
	{
		GameObject* node = stack.back();
		stack.pop_back();
		if (!node->IsActive()) continue;

		const MeshComponent* mesh = node->GetComponent<MeshComponent>();
		if (mesh != nullptr && mesh->IsActive() && mesh->GetPrimitive() != nullptr)
		{
			outObjects.push_back(node);
		}

		const std::vector<GameObject*>& children = node->GetChildren();
		for (auto it = children.rbegin(); it != children.rend(); ++it)
		{
			stack.push_back(*it);
		}
	}
}

void Scene::CollectVisible(const Frustum& frustum, std::vector<GameObject*>& outObjects, OctreeQueryStats* stats)
{
	if (useOctree)
	{
		GetOctree().CollectVisible(frustum, outObjects, stats);
		return;
	}

	// Sin octree ("fuerza bruta"): se prueba la AABB de todos los objetos
	std::vector<GameObject*> renderables;
	CollectRenderables(renderables);
	for (GameObject* object : renderables)
	{
		if (stats) stats->objectsTested++;
		if (frustum.IsVisible(object->GetWorldAABB(false))) outObjects.push_back(object);
	}
}

GameObject* Scene::PickObject(const Ray& ray, float* outDistance)
{
	PerfTimer timer;
	lastPickStats = PickStats();

	// 1) Candidatos: objetos cuya AABB atraviesa el rayo (con el octree solo se miran los nodos que toca)
	std::vector<std::pair<float, GameObject*>> candidates;
	if (useOctree)
	{
		OctreeQueryStats stats;
		GetOctree().CollectRayHits(ray, candidates, &stats);
		lastPickStats.nodesVisited = stats.nodesVisited;
	}
	else
	{
		std::vector<GameObject*> renderables;
		CollectRenderables(renderables);
		for (GameObject* object : renderables)
		{
			float distance = 0.0f;
			if (Intersection::RayAABB(ray, object->GetWorldAABB(false), distance)) candidates.emplace_back(distance, object);
		}
	}
	lastPickStats.candidates = static_cast<int>(candidates.size());

	// 2) Ordenados por distancia a su AABB: en cuanto la AABB del siguiente esta mas lejos que
	//    el mejor impacto encontrado, ya no puede haber nada mas cerca y paramos
	std::sort(candidates.begin(), candidates.end(),
		[](const std::pair<float, GameObject*>& a, const std::pair<float, GameObject*>& b) { return a.first < b.first; });

	GameObject* best = nullptr;
	float bestDistance = FLT_MAX;
	for (const auto& candidate : candidates)
	{
		if (candidate.first > bestDistance) break;

		lastPickStats.meshesTested++;
		float distance = 0.0f;
		if (RayIntersectsMesh(ray, *candidate.second, distance, lastPickStats.trianglesTested) && distance < bestDistance)
		{
			bestDistance = distance;
			best = candidate.second;
		}
	}

	lastPickStats.milliseconds = timer.ReadMs();
	if (outDistance) *outDistance = best ? bestDistance : 0.0f;
	return best;
}

bool Scene::RayIntersectsMesh(const Ray& worldRay, const GameObject& object, float& outDistance, int& trianglesTested) const
{
	const MeshComponent* mesh = object.GetComponent<MeshComponent>();
	const Primitive* primitive = mesh ? mesh->GetPrimitive() : nullptr;
	if (primitive == nullptr) return false;

	// Pasamos el rayo al espacio LOCAL de la malla (mas barato que transformar todos los vertices).
	// La direccion NO se normaliza a proposito: asi la distancia 't' del impacto sigue siendo
	// la distancia en el mundo (el rayo del mundo tiene direccion unitaria).
	const glm::mat4 inverseWorld = glm::inverse(object.GetWorldMatrix());
	Ray localRay;
	localRay.origin = glm::vec3(inverseWorld * glm::vec4(worldRay.origin, 1.0f));
	localRay.direction = glm::vec3(inverseWorld * glm::vec4(worldRay.direction, 0.0f));

	const std::vector<Vertex>& vertices = primitive->GetVertices();
	const std::vector<unsigned int>& indices = primitive->GetIndices();

	bool hit = false;
	float closest = FLT_MAX;
	for (size_t i = 0; i + 2 < indices.size(); i += 3)
	{
		++trianglesTested;
		float distance = 0.0f;
		if (Intersection::RayTriangle(localRay, vertices[indices[i]].position, vertices[indices[i + 1]].position,
			vertices[indices[i + 2]].position, distance) && distance < closest)
		{
			closest = distance;
			hit = true;
		}
	}

	if (hit) outDistance = closest;
	return hit;
}

// =====================================================================================
// SERIALIZACION DE ESCENAS (JSON)
// =====================================================================================
nlohmann::json Scene::SerializeToJson() const
{
	nlohmann::json root;
	root["version"] = SCENE_FILE_VERSION;
	nlohmann::json gameObjects = nlohmann::json::array();

	// Recorrido en pre-orden: los padres siempre se escriben antes que sus hijos
	std::vector<const GameObject*> stack(sceneRoot.GetChildren().rbegin(), sceneRoot.GetChildren().rend());
	while (!stack.empty())
	{
		const GameObject* node = stack.back();
		stack.pop_back();

		nlohmann::json object;
		object["uid"] = static_cast<uint64_t>(node->GetUUID());
		object["parent"] = (node->GetParent() == nullptr || node->GetParent() == &sceneRoot) ? 0 : static_cast<uint64_t>(node->GetParent()->GetUUID());
		object["name"] = node->GetName();
		object["active"] = node->IsActive();
		if (node == gameCameraPtr) object["mainCamera"] = true;

		nlohmann::json components = nlohmann::json::object();
		for (int type = 0; type < COMPONENT_COUNT; ++type)
		{
			const Component* component = node->GetComponentByType(static_cast<ComponentType>(type));
			if (component == nullptr) continue;
			nlohmann::json data = nlohmann::json::object();
			component->Save(data);
			components[ComponentTypeToString(static_cast<ComponentType>(type))] = data;
		}
		object["components"] = components;
		gameObjects.push_back(std::move(object));

		const std::vector<GameObject*>& children = node->GetChildren();
		for (auto it = children.rbegin(); it != children.rend(); ++it)
		{
			stack.push_back(*it);
		}
	}

	root["gameObjects"] = std::move(gameObjects);
	return root;
}

std::string Scene::SerializeToString() const
{
	return SerializeToJson().dump(1, '\t', false, nlohmann::json::error_handler_t::replace);
}

bool Scene::DeserializeFromString(const std::string& text)
{
	const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
	if (root.is_discarded()) return false;
	return LoadFromJson(root);
}

bool Scene::LoadFromJson(const nlohmann::json& root)
{
	// Validamos ANTES de borrar la escena actual, para no perderla si el fichero esta mal
	if (!root.is_object()) return false;
	const auto objectsIt = root.find("gameObjects");
	if (objectsIt == root.end() || !objectsIt->is_array()) return false;
	for (const nlohmann::json& object : *objectsIt)
	{
		if (!object.is_object()) return false;
	}

	ClearScene();

	// UID del fichero -> GameObject creado (para enlazar cada hijo con su padre)
	std::unordered_map<uint64_t, GameObject*> created;
	GameObject* mainCamera = nullptr;
	GameObject* firstCamera = nullptr;

	try
	{
		for (const nlohmann::json& object : *objectsIt)
		{
			uint64_t fileUID = 0;
			uint64_t parentUID = 0;
			std::string objectName = "GameObject";
			bool isActive = true;
			bool isMainCamera = false;
			JsonHelpers::Read(object, "uid", fileUID);
			JsonHelpers::Read(object, "parent", parentUID);
			JsonHelpers::Read(object, "name", objectName);
			JsonHelpers::Read(object, "active", isActive);
			JsonHelpers::Read(object, "mainCamera", isMainCamera);

			// Se conserva el UID del fichero (la seleccion y el modo Play lo usan); si falta o esta repetido, uno nuevo
			const bool reuseUID = fileUID != 0 && objectsByUID.find(fileUID) == objectsByUID.end();
			auto go = reuseUID ? std::make_unique<GameObject>(objectName, GameUUID(fileUID)) : std::make_unique<GameObject>(objectName);

			nlohmann::json components = nlohmann::json::object();
			const auto componentsIt = object.find("components");
			if (componentsIt != object.end() && componentsIt->is_object()) components = *componentsIt;

			// El Transform siempre existe y va primero (la camara lo necesita al construirse)
			auto transform = std::make_unique<TransformComponent>(go.get());
			if (components.contains("Transform")) transform->Load(components["Transform"]);
			go->AddComponent(std::move(transform));

			if (components.contains("Mesh"))
			{
				auto mesh = std::make_unique<MeshComponent>(go.get());
				mesh->Load(components["Mesh"]);
				go->AddComponent(std::move(mesh));
			}
			if (components.contains("Material"))
			{
				auto material = std::make_unique<MaterialComponent>(go.get());
				material->Load(components["Material"]);
				go->AddComponent(std::move(material));
			}
			if (components.contains("Camera"))
			{
				auto camera = std::make_unique<CameraComponent>(go.get());
				camera->Load(components["Camera"]);
				go->AddComponent(std::move(camera));
			}

			if (!isActive) go->Disable();

			const auto parentIt = created.find(parentUID);
			GameObject* parent = (parentUID != 0 && parentIt != created.end()) ? parentIt->second : nullptr;
			GameObject* registered = Register(std::move(go), parent);
			if (fileUID != 0) created[fileUID] = registered;

			if (registered->GetComponent<CameraComponent>() != nullptr)
			{
				if (isMainCamera && mainCamera == nullptr) mainCamera = registered;
				if (firstCamera == nullptr) firstCamera = registered;
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("[Scene] Error leyendo la escena: %s", e.what());
	}

	SetGameCamera(mainCamera ? mainCamera : firstCamera);
	GameObject::MarkSpatialDirty();
	return true;
}

bool Scene::SaveScene(const std::string& projectPath)
{
	if (IsInPlayMode())
	{
		LOG_WARNING("[Scene] No se puede guardar la escena durante el modo Play (pulsa Stop primero)");
		return false;
	}

	std::string path = FileSystem::Normalize(projectPath);
	if (FileSystem::GetExtensionLower(path) != FileSystem::SCENE_EXTENSION) path += FileSystem::SCENE_EXTENSION;
	if (!FileSystem::IsInsideFolder(path, FileSystem::ASSETS_DIR))
	{
		path = FileSystem::Join(FileSystem::SCENES_DIR, FileSystem::GetFileName(path));
	}

	if (!FileSystem::WriteText(path, SerializeToString()))
	{
		LOG_ERROR("[Scene] No se pudo guardar la escena en '%s'", path.c_str());
		return false;
	}

	currentScenePath = path;
	if (Project::HasActive())
	{
		Project::GetActive().lastModifiedAt = Project::Now();
		Project::GetActive().lastScene.clear(); // fuerza el guardado de project.json
	}
	RememberProjectScene(path);
	LOG("[Scene] Escena guardada en '%s' (%zu GameObjects)", path.c_str(), sceneEntities.size());
	return true;
}

void Scene::RequestLoadScene(const std::string& projectPath)
{
	// Cargar una escena saca del modo Play (el estado guardado ya no tendria sentido)
	if (IsInPlayMode())
	{
		playState = PlayState::Stopped;
		pendingRestore = false;
		playModeBackup.clear();
	}
	pendingLoadPath = FileSystem::Normalize(projectPath);
}

void Scene::RequestNewScene()
{
	if (IsInPlayMode())
	{
		playState = PlayState::Stopped;
		pendingRestore = false;
		playModeBackup.clear();
	}
	pendingNewScene = true;
}

void Scene::RequestDefaultScene()
{
	if (IsInPlayMode())
	{
		playState = PlayState::Stopped;
		pendingRestore = false;
		playModeBackup.clear();
	}
	pendingDefaultScene = true;
}

// =====================================================================================
// MODO PLAY
// =====================================================================================
void Scene::Play()
{
	if (playState == PlayState::Stopped)
	{
		// Guardamos el estado inicial para poder recuperarlo al pulsar Stop
		playModeBackup = SerializeToString();
		playModeSelection = GetSelectionUIDs();
		playModeActive = activeSelected ? static_cast<uint64_t>(activeSelected->GetUUID()) : 0;
		gameTime = 0.0f;
		playState = PlayState::Playing;
		LOG("[Scene] Play: simulacion iniciada (estado de la escena guardado)");
	}
	else if (playState == PlayState::Paused)
	{
		playState = PlayState::Playing;
		LOG("[Scene] Play: simulacion reanudada");
	}
}

void Scene::Pause()
{
	if (playState == PlayState::Playing)
	{
		playState = PlayState::Paused;
		LOG("[Scene] Pause: simulacion en pausa (%.2f s)", gameTime);
	}
	else if (playState == PlayState::Paused)
	{
		playState = PlayState::Playing;
		LOG("[Scene] Pause: simulacion reanudada");
	}
}

void Scene::Stop()
{
	if (playState == PlayState::Stopped) return;

	playState = PlayState::Stopped;
	stepRequested = false;
	gameTime = 0.0f;
	pendingRestore = true; // la restauracion se hace al principio del siguiente frame
	LOG("[Scene] Stop: volviendo al modo edicion");
}

void Scene::Step()
{
	if (playState == PlayState::Paused)
	{
		stepRequested = true;
	}
}
