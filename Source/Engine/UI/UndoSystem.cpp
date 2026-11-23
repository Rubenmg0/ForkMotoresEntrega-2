#include "UndoSystem.h"
#include "../Engine.h"
#include "../Scene/Scene.h"
#include "../Scene/GameObjects/GameObject.h"
#include "../Scene/GameObjects/Components/TransformComponent.h"
#include "../../Utils/Log.h"

namespace
{
	const std::string EMPTY_NAME;

	bool SameState(const glm::vec3& a, const glm::vec3& b)
	{
		return a == b;
	}
}

std::vector<UndoSystem::TransformState> UndoSystem::CaptureTransforms(const std::vector<uint64_t>& uids)
{
	std::vector<TransformState> states;
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return states;

	for (uint64_t uid : uids)
	{
		GameObject* object = scene->FindByUID(uid);
		const TransformComponent* transform = object ? object->GetComponent<TransformComponent>() : nullptr;
		if (transform == nullptr) continue;
		TransformState state;
		state.uid = uid;
		state.position = transform->GetPosition();
		state.rotation = transform->GetRotation();
		state.scale = transform->GetScale();
		states.push_back(state);
	}
	return states;
}

void UndoSystem::ApplyTransforms(const std::vector<TransformState>& states)
{
	Scene* scene = Engine::GetInstance().scene.get();
	if (scene == nullptr) return;

	for (const TransformState& state : states)
	{
		GameObject* object = scene->FindByUID(state.uid);
		TransformComponent* transform = object ? object->GetComponent<TransformComponent>() : nullptr;
		if (transform == nullptr) continue;
		transform->SetPosition(state.position);
		transform->SetRotation(state.rotation);
		transform->SetScale(state.scale);
	}
}

UndoSystem::Entry UndoSystem::CaptureSnapshot(const std::string& name)
{
	Entry entry;
	entry.kind = Kind::Snapshot;
	entry.name = name;
	if (Scene* scene = Engine::GetInstance().scene.get())
	{
		entry.sceneText = scene->SerializeToString();
		entry.selection = scene->GetSelectionUIDs();
		entry.active = scene->GetSelected() ? static_cast<uint64_t>(scene->GetSelected()->GetUUID()) : 0;
	}
	return entry;
}

void UndoSystem::RestoreSnapshot(const Entry& entry)
{
	if (Scene* scene = Engine::GetInstance().scene.get())
	{
		scene->RequestRestoreSnapshot(entry.sceneText, entry.selection, entry.active);
	}
}

bool UndoSystem::IsBlocked() const
{
	const Scene* scene = Engine::GetInstance().scene.get();
	// En modo Play no se registra nada: al pulsar Stop la escena vuelve a su estado anterior
	return scene == nullptr || scene->IsInPlayMode() || scene->HasPendingRestore();
}

void UndoSystem::Push(Entry&& entry)
{
	undoStack.push_back(std::move(entry));
	if (undoStack.size() > MAX_ENTRIES) undoStack.erase(undoStack.begin());
	redoStack.clear();
}

void UndoSystem::RecordSnapshot(const std::string& name)
{
	if (IsBlocked()) return;
	Push(CaptureSnapshot(name));
}

void UndoSystem::BeginTransform(const std::vector<GameObject*>& objects, const std::string& name)
{
	if (IsBlocked() || recordingTransform) return;

	std::vector<uint64_t> uids;
	for (const GameObject* object : objects)
	{
		if (object) uids.push_back(static_cast<uint64_t>(object->GetUUID()));
	}
	if (uids.empty()) return;

	pendingTransform = Entry();
	pendingTransform.kind = Kind::Transform;
	pendingTransform.name = name;
	pendingTransform.before = CaptureTransforms(uids);
	recordingTransform = true;
}

void UndoSystem::EndTransform()
{
	if (!recordingTransform) return;
	recordingTransform = false;

	std::vector<uint64_t> uids;
	for (const TransformState& state : pendingTransform.before) uids.push_back(state.uid);
	pendingTransform.after = CaptureTransforms(uids);

	bool changed = pendingTransform.after.size() != pendingTransform.before.size();
	for (size_t i = 0; !changed && i < pendingTransform.before.size(); ++i)
	{
		const TransformState& a = pendingTransform.before[i];
		const TransformState& b = pendingTransform.after[i];
		changed = !SameState(a.position, b.position) || !SameState(a.rotation, b.rotation) || !SameState(a.scale, b.scale);
	}
	if (changed) Push(std::move(pendingTransform));
	pendingTransform = Entry();
}

bool UndoSystem::CanUndo() const
{
	return !undoStack.empty() && !IsBlocked();
}

bool UndoSystem::CanRedo() const
{
	return !redoStack.empty() && !IsBlocked();
}

const std::string& UndoSystem::GetUndoName() const
{
	return undoStack.empty() ? EMPTY_NAME : undoStack.back().name;
}

const std::string& UndoSystem::GetRedoName() const
{
	return redoStack.empty() ? EMPTY_NAME : redoStack.back().name;
}

void UndoSystem::Undo()
{
	if (!CanUndo()) return;
	if (recordingTransform) EndTransform();

	Entry entry = std::move(undoStack.back());
	undoStack.pop_back();

	if (entry.kind == Kind::Transform)
	{
		ApplyTransforms(entry.before);
		redoStack.push_back(std::move(entry));
	}
	else
	{
		// El estado actual pasa a la pila de rehacer
		Entry current = CaptureSnapshot(entry.name);
		RestoreSnapshot(entry);
		redoStack.push_back(std::move(current));
	}
	LOG("[Editor] Undo %s", redoStack.back().name.c_str());
}

void UndoSystem::Redo()
{
	if (!CanRedo()) return;
	if (recordingTransform) EndTransform();

	Entry entry = std::move(redoStack.back());
	redoStack.pop_back();

	if (entry.kind == Kind::Transform)
	{
		ApplyTransforms(entry.after);
		undoStack.push_back(std::move(entry));
	}
	else
	{
		Entry current = CaptureSnapshot(entry.name);
		RestoreSnapshot(entry);
		undoStack.push_back(std::move(current));
	}
	LOG("[Editor] Redo %s", undoStack.back().name.c_str());
}

void UndoSystem::Clear()
{
	undoStack.clear();
	redoStack.clear();
	recordingTransform = false;
	pendingTransform = Entry();
}
