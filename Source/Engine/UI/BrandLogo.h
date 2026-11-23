#pragma once

#include <imgui.h>

// =====================================================================================
// Logo de Maruja Engine, dibujado con primitivas de ImGui (sin imagenes): un cubo isometrico
// en tonos del azul de acento del editor con un anillo orbital que gira a su alrededor.
// Es vectorial, asi que se ve nitido a cualquier tamano (splash, Hub, ventana About).
// =====================================================================================
namespace BrandLogo
{
	// center: centro del logo en coordenadas de pantalla
	// size:   lado del cuadrado que ocupa el logo (px)
	// time:   segundos (anima el anillo); 0 = estatico
	// alpha:  opacidad global (fundidos del splash)
	void Draw(ImDrawList* drawList, const ImVec2& center, float size, float time = 0.0f, float alpha = 1.0f);

	// Reserva un hueco de size x size en la ventana actual y dibuja el logo dentro
	void Widget(float size, float time = 0.0f, float alpha = 1.0f);

	// Texto "MARUJA ENGINE" con el estilo de la marca (titulo grande + subtitulo espaciado).
	// Dibujado a partir de 'topLeft'. Devuelve el tamano ocupado.
	ImVec2 DrawWordmark(ImDrawList* drawList, const ImVec2& topLeft, float titleSize, float alpha = 1.0f);
}
