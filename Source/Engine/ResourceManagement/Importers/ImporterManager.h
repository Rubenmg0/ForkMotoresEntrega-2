#pragma once

#include "../Resource.h"
#include <string>

class GameObject;

// =====================================================================================
// ImporterManager: decide que hacer con un fichero arrastrado desde el explorador del
// sistema operativo a la ventana del motor:
//   1) Se copia dentro de Assets (en la carpeta abierta en la ventana Assets) y se importa
//      a Library con el ResourceManager.
//   2) Segun el tipo:  modelo  -> se instancia en la escena
//                      textura -> se asigna al GameObject seleccionado (y a sus hijos)
//                      escena  -> se abre
// =====================================================================================
class ImporterManager
{
public:
	static bool HandleDroppedFile(const std::string& diskPathUtf8, GameObject* selectedObject);

	// Pone la textura en el material del GameObject y de todos sus hijos con malla
	static bool AssignTextureToHierarchy(UID textureUID, GameObject* target);
};
