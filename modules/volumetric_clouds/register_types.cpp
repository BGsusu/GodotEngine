#include "register_types.h"
#include "core/object/class_db.h"
#include "volumetric_cloud_effect.h"
#include "volumetric_cloud_material.h"

void initialize_volumetric_clouds_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(VolumetricCloudMaterial);
		GDREGISTER_CLASS(VolumetricCloudEffect);
	}
}

void uninitialize_volumetric_clouds_module(ModuleInitializationLevel p_level) {
}
