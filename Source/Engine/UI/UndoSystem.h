#pragma once

#include <glm/vec3.hpp>
#include <cstdint>
#include <string>
#include <vector>

class GameObject;

// =====================================================================================
// UndoSystem: Deshacer / Rehacer del editor (Ctrl+Z / Ctrl+Y), como en Unity.
//
// Hay dos tipos de entrada:
//   - TRANSFORM: posicion / rotacion / escala de uno o varios objetos antes y despues de un
//     cambio (arrastrar el gizmo, editar el Transform en el Inspector). Es barata: no recarga
//     la escena, solo reescribe los valores.
//   - SNAPSHOT: la escena entera serializada (JSON) ANTES de un cambio estructural (crear,
//     borrar, duplicar, reemparentar, renombrar, anadir / quitar componentes, editar sus
//     propiedades...). Deshacer guarda el estado actual para poder rehacer y restaura la copia.
//     Los UID se conservan al restaurar, asi que la seleccion se recupera tambien.
//
// Uso:  undo.RecordSnapshot("Delete");  // justo ANTES de modificar la escena
//       undo.BeginTransform(objetos, "Move"); ... undo.EndTransform();
// =====================================================================================
class UndoSystem
{
public:
	static constexpr size_t MAX_ENTRIES = 100;

	// Guarda el estado actual de la escena antes de un cambio estructural
	void RecordSnapshot(const std::string& name);

	// Cambios de Transform (gizmo, Inspector): Begin al empezar a arrastrar, End al soltar.
	// Si al terminar nada ha cambiado no se guarda ninguna entrada.
	void BeginTransform(const std::vector<GameObject*>& objects, const std::string& name);
	void EndTransform();
	bool IsRecordingTransform() const { return recordingTransform; }

	bool CanUndo() const;
	bool CanRedo() const;
	const std::string& GetUndoName() const;
	const std::string& GetRedoName() const;

	void Undo();
	void Redo();
	void Clear();

private:
	struct TransformState
	{
		uint64_t uid = 0;
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 rotation = glm::vec3(0.0f);
		glm::vec3 scale = glm::vec3(1.0f);
	};

	enum class Kind { Snapshot, Transform };

	struct Entry
	{
		Kind kind = Kind::Snapshot;
		std::string name;
		// Snapshot
		std::string sceneText;
		std::vector<uint64_t> selection;
		uint64_t active = 0;
		// Transform
		std::vector<TransformState> before;
		std::vector<TransformState> after;
	};

	static std::vector<TransformState> CaptureTransforms(const std::vector<uint64_t>& uids);
	static void ApplyTransforms(const std::vector<TransformState>& states);
	static Entry CaptureSnapshot(const std::string& name);
	static void RestoreSnapshot(const Entry& entry);
	bool IsBlocked() const;
	void Push(Entry&& entry);

	std::vector<Entry> undoStack;
	std::vector<Entry> redoStack;

	bool recordingTransform = false;
	Entry pendingTransform;
};
