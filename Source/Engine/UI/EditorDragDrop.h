#pragma once

#include "../ResourceManagement/Resource.h"

#include <algorithm>
#include <cstring>
#include <string>

// =====================================================================================
// Tipos de "payload" del drag & drop de ImGui que usa el editor.
//  - GAMEOBJECT: se arrastran GameObjects de la jerarquia (dato: array de UID, uint64_t)
//  - ASSET_*:    se arrastra un asset de la ventana Assets (dato: AssetPayload)
// Cada zona donde se puede soltar acepta solo los tipos que entiende (por ejemplo el campo
// "Malla" del Inspector solo acepta ASSET_MESH).
// =====================================================================================
namespace EditorDragDrop
{
	constexpr const char* GAMEOBJECT = "GAMEOBJECT_UID";
	constexpr const char* ASSET_TEXTURE = "ASSET_TEXTURE";
	constexpr const char* ASSET_MODEL = "ASSET_MODEL";
	constexpr const char* ASSET_MESH = "ASSET_MESH";
	constexpr const char* ASSET_SCENE = "ASSET_SCENE";
	constexpr const char* ASSET_FOLDER = "ASSET_FOLDER"; // carpeta del Project (se puede mover a otra)
	constexpr const char* ASSET_OTHER = "ASSET_OTHER";   // fichero sin tipo conocido (solo se puede mover)

	// ImGui copia el payload byte a byte: tiene que ser un struct simple (sin std::string)
	struct AssetPayload
	{
		UID uid = INVALID_UID;
		char path[512] = {};
	};

	inline AssetPayload MakeAssetPayload(UID uid, const std::string& path)
	{
		AssetPayload payload;
		payload.uid = uid;
		const size_t length = std::min(path.size(), sizeof(payload.path) - 1);
		std::memcpy(payload.path, path.data(), length);
		payload.path[length] = '\0';
		return payload;
	}

	inline AssetPayload ReadAssetPayload(const void* data, int size)
	{
		AssetPayload payload;
		if (data != nullptr && size == static_cast<int>(sizeof(AssetPayload)))
		{
			std::memcpy(&payload, data, sizeof(AssetPayload));
			payload.path[sizeof(payload.path) - 1] = '\0';
		}
		return payload;
	}
}
