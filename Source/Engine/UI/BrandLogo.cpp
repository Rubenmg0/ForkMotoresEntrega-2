#include "BrandLogo.h"

#include <cfloat>
#include <cmath>

namespace
{
	constexpr float PI = 3.14159265358979f;

	ImU32 Color(int r, int g, int b, float alpha)
	{
		const int a = static_cast<int>(255.0f * (alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha)));
		return IM_COL32(r, g, b, a);
	}

	ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }

	// Punto del anillo orbital: elipse inclinada alrededor del cubo
	ImVec2 RingPoint(const ImVec2& center, float radius, float angle, float tilt)
	{
		const float x = std::cos(angle) * radius;
		const float y = std::sin(angle) * radius * 0.32f;
		const float c = std::cos(tilt), s = std::sin(tilt);
		return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
	}

	// Dibuja la mitad trasera (sin(angle) < 0) o delantera del anillo
	void DrawRingHalf(ImDrawList* drawList, const ImVec2& center, float radius, float tilt, float thickness, float alpha, bool front)
	{
		constexpr int SEGMENTS = 48;
		ImVec2 points[SEGMENTS + 1];
		const float start = front ? 0.0f : PI;
		for (int i = 0; i <= SEGMENTS; ++i)
		{
			const float angle = start + PI * static_cast<float>(i) / SEGMENTS;
			points[i] = RingPoint(center, radius, angle, tilt);
		}
		drawList->AddPolyline(points, SEGMENTS + 1, front ? Color(150, 200, 255, alpha) : Color(90, 140, 200, alpha * 0.55f),
			ImDrawFlags_None, thickness);
	}
}

namespace BrandLogo
{
	void Draw(ImDrawList* drawList, const ImVec2& center, float size, float time, float alpha)
	{
		if (drawList == nullptr || size <= 0.0f || alpha <= 0.0f) return;

		const float r = size * 0.30f;          // radio del hexagono (silueta del cubo)
		const float ringRadius = size * 0.47f;
		const float tilt = -0.32f;
		const float ringThickness = (size * 0.022f > 1.0f) ? size * 0.022f : 1.0f;

		// Halo suave detras del logo
		drawList->AddCircleFilled(center, size * 0.46f, Color(58, 121, 187, alpha * 0.10f), 64);
		drawList->AddCircleFilled(center, size * 0.36f, Color(58, 121, 187, alpha * 0.12f), 64);

		// Mitad trasera del anillo (queda detras del cubo)
		DrawRingHalf(drawList, center, ringRadius, tilt, ringThickness, alpha, false);

		// Vertices del hexagono "de punta" (cubo isometrico visto desde arriba)
		ImVec2 v[6];
		for (int i = 0; i < 6; ++i)
		{
			const float angle = (-90.0f + 60.0f * i) * PI / 180.0f;
			v[i] = ImVec2(center.x + std::cos(angle) * r, center.y + std::sin(angle) * r);
		}
		// v[0] arriba, v[1] arriba-dcha, v[2] abajo-dcha, v[3] abajo, v[4] abajo-izda, v[5] arriba-izda
		const ImVec2 top[4]   = { center, v[5], v[0], v[1] };
		const ImVec2 left[4]  = { center, v[3], v[4], v[5] };
		const ImVec2 right[4] = { center, v[1], v[2], v[3] };

		drawList->AddConvexPolyFilled(top, 4, Color(120, 176, 236, alpha));   // cara superior (luz)
		drawList->AddConvexPolyFilled(left, 4, Color(58, 121, 187, alpha));   // cara izquierda (acento #3A79BB)
		drawList->AddConvexPolyFilled(right, 4, Color(34, 78, 128, alpha));   // cara derecha (sombra)

		// "M" grabada sobre las dos caras frontales
		const float inset = 0.28f;
		auto Lerp = [](const ImVec2& a, const ImVec2& b, float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); };
		const ImVec2 bottomLeft = Lerp(v[4], v[3], inset);
		const ImVec2 bottomRight = Lerp(v[2], v[3], inset);
		const ImVec2 topLeft = Lerp(v[5], center, inset);
		const ImVec2 topRight = Lerp(v[1], center, inset);
		const ImVec2 middle = Lerp(center, v[3], 0.42f);
		const ImVec2 mPoints[5] = {
			Lerp(bottomLeft, topLeft, 0.0f), Lerp(bottomLeft, topLeft, 1.0f) , middle,
			Lerp(bottomRight, topRight, 1.0f), Lerp(bottomRight, topRight, 0.0f) };
		drawList->AddPolyline(mPoints, 5, Color(235, 244, 255, alpha * 0.95f), ImDrawFlags_None, size * 0.045f);

		// Aristas finas para definir el volumen
		const ImU32 edge = Color(200, 228, 255, alpha * 0.55f);
		const float edgeThickness = (size * 0.012f > 1.0f) ? size * 0.012f : 1.0f;
		drawList->AddPolyline(v, 6, edge, ImDrawFlags_Closed, edgeThickness);
		drawList->AddLine(center, v[5], edge, edgeThickness);
		drawList->AddLine(center, v[1], edge, edgeThickness);
		drawList->AddLine(center, v[3], edge, edgeThickness);

		// Mitad delantera del anillo + "satelite" que lo recorre
		DrawRingHalf(drawList, center, ringRadius, tilt, ringThickness, alpha, true);
		const float moonAngle = std::fmod(time * 1.4f, 2.0f * PI);
		const ImVec2 moon = RingPoint(center, ringRadius, moonAngle + 0.9f, tilt);
		const bool moonInFront = std::sin(moonAngle + 0.9f) >= 0.0f;
		drawList->AddCircleFilled(moon, size * 0.045f, Color(255, 255, 255, alpha * (moonInFront ? 1.0f : 0.35f)), 16);
		drawList->AddCircleFilled(moon, size * 0.075f, Color(150, 200, 255, alpha * (moonInFront ? 0.25f : 0.08f)), 16);
	}

	void Widget(float size, float time, float alpha)
	{
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(size, size));
		Draw(ImGui::GetWindowDrawList(), Add(cursor, ImVec2(size * 0.5f, size * 0.5f)), size, time, alpha);
	}

	ImVec2 DrawWordmark(ImDrawList* drawList, const ImVec2& topLeft, float titleSize, float alpha)
	{
		ImFont* font = ImGui::GetFont();
		const char* title = "MARUJA";
		const ImVec2 titleExtent = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, title);
		drawList->AddText(font, titleSize, topLeft, Color(240, 244, 250, alpha), title);

		// "ENGINE" pequeno y con las letras separadas, alineado al ancho del titulo
		const char* subtitle = "E N G I N E";
		const float subtitleSize = titleSize * 0.36f;
		const ImVec2 subtitleExtent = font->CalcTextSizeA(subtitleSize, FLT_MAX, 0.0f, subtitle);
		const ImVec2 subtitlePos(topLeft.x + (titleExtent.x - subtitleExtent.x), topLeft.y + titleExtent.y * 0.98f);
		drawList->AddText(font, subtitleSize, subtitlePos, Color(120, 176, 236, alpha), subtitle);

		const float width = (titleExtent.x > subtitleExtent.x) ? titleExtent.x : subtitleExtent.x;
		return ImVec2(width, titleExtent.y + subtitleExtent.y);
	}
}
