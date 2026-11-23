#pragma once

// =====================================================================================
// Tema del editor: copia del tema oscuro de Unity ("Pro skin").
//   - Paneles gris medio (#383838), barras de pestanas y de herramientas mas oscuras
//   - Campos hundidos (#2A2A2A) con un borde fino, botones claros (#585858)
//   - Seleccion azul (#2C5D87) solo para lo que esta seleccionado
//   - En modo Play todo el editor se tine (como Unity) para que se note que se esta simulando
// =====================================================================================
namespace EditorTheme
{
	// Aplica el tema completo (geometria + colores + colores del gizmo 3D)
	void ApplyUnityDark();

	// Activa / quita el tinte del modo Play. Se puede llamar cada frame: solo hace algo al cambiar.
	void SetPlayModeTint(bool enabled);
}
