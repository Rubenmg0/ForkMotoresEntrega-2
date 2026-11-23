#pragma once

// =====================================================================================
// Contenido de la ventana "About Maruja Engine". Lo comparten el editor (Help > About) y la
// pagina "About" del Hub, asi que no depende del singleton Engine: las versiones de las
// librerias se consultan con SystemInfo (necesita un contexto OpenGL activo).
// =====================================================================================
namespace AboutContent
{
	// Dibuja el contenido dentro de la ventana / child actual. 'time' anima el logo.
	void Draw(float time);

	// Texto corto de version: "Maruja Engine 0.4.0 (a1b2c3d)"
	const char* GetVersionLabel();
}
