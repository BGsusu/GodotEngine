def can_build(env, platform):
    return env["rendering_device"] and not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return ["VolumetricCloudEffect", "VolumetricCloudMaterial"]


def get_doc_path():
    return "doc_classes"
