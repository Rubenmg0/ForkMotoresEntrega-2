#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// =====================================================================================
// SHA-256 (FIPS 180-4). Se usa para publicar la suma de verificacion de cada release
// (<Producto>-<version>-win64.zip.sha256), como hacen Godot, Unreal o cualquier descarga seria:
// quien descarga el zip puede comprobar que esta completo y no se ha modificado.
// =====================================================================================
class Sha256
{
public:
	Sha256();
	void Update(const void* data, size_t size);
	std::string FinalHex(); // 64 caracteres hexadecimales en minuscula

	// Hash de un fichero entero (ruta UTF-8). "" si no se puede leer.
	static std::string HashFile(const std::string& diskPathUtf8);

private:
	void Transform(const uint8_t block[64]);

	uint32_t state[8];
	uint8_t buffer[64];
	uint64_t bitLength = 0;
	size_t bufferSize = 0;
};
