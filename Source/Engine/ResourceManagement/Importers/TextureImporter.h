#pragma once

#include "../ImportSettings.h"

#include <cstddef>
#include <string>

// =====================================================================================
// TextureImporter: convierte imagenes (png, jpg, tga, dds, bmp) a NUESTRO FORMATO PROPIO
// ".tex" y carga los ".tex" en la GPU.
//
// FORMATO .tex (binario):
//   1) Cabecera de 32 bytes:
//        char[4]  "MTEX"        -> identificador del formato
//        uint32   version
//        uint32   ancho, alto
//        uint32   canales       -> 3 (RGB) o 4 (RGBA)
//        uint32   minFilter, magFilter, wrapMode (opciones de importacion)
//   2) Pixeles: ancho * alto * canales bytes, filas de ABAJO a ARRIBA (como espera OpenGL)
//
// La imagen ya va volteada / reducida segun las opciones de importacion, asi que cargar
// un .tex es solo leer el fichero y subirlo a la GPU (no hay que decodificar PNG/JPG).
// =====================================================================================
class TextureImporter
{
public:
	// .png .jpg .jpeg .tga .dds .bmp
	static bool IsSupportedExtension(const std::string& extension);

	// Asset (ruta de proyecto) -> fichero .tex en Library
	static bool Import(const std::string& assetPath, const TextureImportSettings& settings, const std::string& libraryPath);

	struct GPUTexture
	{
		unsigned int id = 0;
		int width = 0;
		int height = 0;
		int channels = 0;
		size_t bytes = 0; // memoria aproximada en GPU
	};

	// Fichero .tex -> textura OpenGL lista para usar
	static bool LoadToGPU(const std::string& libraryPath, GPUTexture& outTexture);
};
