#include "Component.h"

const char* ComponentTypeToString(ComponentType type)
{
	switch (type)
	{
	case COMPONENT_TRANSFORM: return "Transform";
	case COMPONENT_MESH:      return "Mesh";
	case COMPONENT_MATERIAL:  return "Material";
	case COMPONENT_CAMERA:    return "Camera";
	default:                  return "Unknown";
	}
}
