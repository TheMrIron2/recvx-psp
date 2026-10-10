"""Native ctypes layout for host-side scene validation and previews."""
import ctypes


class SceneVertex(ctypes.Structure):
    _fields_ = [("color", ctypes.c_uint32), ("x", ctypes.c_float),
                ("y", ctypes.c_float), ("z", ctypes.c_float)]


class TexturedVertex(ctypes.Structure):
    _fields_ = [("u", ctypes.c_float), ("v", ctypes.c_float), ("color", ctypes.c_uint32),
                ("nx", ctypes.c_float), ("ny", ctypes.c_float), ("nz", ctypes.c_float),
                ("x", ctypes.c_float), ("y", ctypes.c_float), ("z", ctypes.c_float)]


class MeshBatch(ctypes.Structure):
    _fields_ = [("first_vertex", ctypes.c_uint32), ("vertex_count", ctypes.c_uint32),
                ("texture_index", ctypes.c_uint32), ("flags", ctypes.c_uint32),
                ("object_index", ctypes.c_uint32), ("blend_state", ctypes.c_uint32)]


class RoomCut(ctypes.Structure):
    _fields_ = [("hide_objects", (ctypes.c_uint32 * 16) * 2),
                ("hide_lights", (ctypes.c_uint32 * 8) * 2),
                ("area_first", ctypes.c_uint32), ("area_count", ctypes.c_uint32),
                ("active", ctypes.c_uint8)]


class RoomCamera(ctypes.Structure):
    _fields_ = [("flags", ctypes.c_uint16), ("light_clip", ctypes.c_uint8),
                ("speed", ctypes.c_uint8), ("position", ctypes.c_float * 3),
                ("lens", ctypes.c_float), ("width", ctypes.c_float),
                ("height", ctypes.c_float), ("depth", ctypes.c_float),
                ("active_y", ctypes.c_float * 4), ("angle_speed", ctypes.c_float),
                ("angles", ctypes.c_int32 * 3), ("angle_limits", ctypes.c_int32 * 2),
                ("roll_limits", ctypes.c_int16 * 4), ("angle_speed8", ctypes.c_uint8),
                ("filter_number", ctypes.c_uint8), ("filter_rate", ctypes.c_uint8),
                ("reserved", ctypes.c_uint8), ("perspective", ctypes.c_uint32),
                ("cut_center_x", ctypes.c_float), ("cut_center_z", ctypes.c_float),
                ("cut_width", ctypes.c_float), ("cut_depth", ctypes.c_float)]


class RoomLight(ctypes.Structure):
    _fields_ = [("flags", ctypes.c_uint32), ("type", ctypes.c_uint32),
                ("source", ctypes.c_uint32), ("position", ctypes.c_float * 3),
                ("direction", ctypes.c_float * 3), ("color", ctypes.c_float * 3),
                ("near_range", ctypes.c_float), ("far_range", ctypes.c_float)]


class Texture(ctypes.Structure):
    _fields_ = [("storage", ctypes.c_void_p), ("pixels", ctypes.POINTER(ctypes.c_ubyte)),
                ("palette", ctypes.POINTER(ctypes.c_uint32)), ("width", ctypes.c_uint32),
                ("height", ctypes.c_uint32), ("image_bytes", ctypes.c_uint32),
                ("global_index", ctypes.c_uint32)]


class TextureSet(ctypes.Structure):
    _fields_ = [("textures", ctypes.POINTER(Texture)), ("count", ctypes.c_uint32),
                ("storage_bytes", ctypes.c_uint32)]


class Scene(ctypes.Structure):
    _fields_ = [("storage", ctypes.c_void_p), ("vertices", ctypes.POINTER(SceneVertex)),
                ("vertex_count", ctypes.c_uint32), ("box_count", ctypes.c_uint32),
                ("marker_count", ctypes.c_uint32), ("center", ctypes.c_float * 3),
                ("radius", ctypes.c_float)]


class Mesh(ctypes.Structure):
    _fields_ = [("storage", ctypes.c_void_p), ("vertices", ctypes.POINTER(TexturedVertex)),
                ("vertex_count", ctypes.c_uint32), ("object_count", ctypes.c_uint32),
                ("model_count", ctypes.c_uint32), ("triangle_count", ctypes.c_uint32),
                ("center", ctypes.c_float * 3), ("radius", ctypes.c_float),
                ("wire_vertices", ctypes.POINTER(SceneVertex)),
                ("material_colors", ctypes.POINTER(ctypes.c_uint32)),
                ("textured_vertices", ctypes.POINTER(TexturedVertex)),
                ("batches", ctypes.POINTER(MeshBatch)), ("batch_count", ctypes.c_uint32),
                ("storage_bytes", ctypes.c_uint32), ("cuts", ctypes.POINTER(RoomCut)),
                ("cut_count", ctypes.c_uint32),
                ("cameras", ctypes.POINTER(RoomCamera)),
                ("cut_areas", ctypes.c_void_p), ("cut_area_count", ctypes.c_uint32),
                ("lights", ctypes.POINTER(RoomLight)),
                ("light_count", ctypes.c_uint32), ("ambient", ctypes.c_float * 3),
                ("wire_storage", ctypes.c_void_p)]


def bind_scene(library):
    library.recvxBuildRoomScene.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                          ctypes.POINTER(Scene)]
    library.recvxBuildRoomScene.restype = ctypes.c_int
    library.recvxSceneFree.argtypes = [ctypes.POINTER(Scene)]
    library.recvxSceneFree.restype = None
    library.recvxBuildRoomMesh.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                         ctypes.POINTER(Mesh)]
    library.recvxBuildRoomMesh.restype = ctypes.c_int
    library.recvxMeshSetWireframe.argtypes = [ctypes.POINTER(Mesh), ctypes.c_int]
    library.recvxMeshSetWireframe.restype = ctypes.c_int
    library.recvxMeshFree.argtypes = [ctypes.POINTER(Mesh)]
    library.recvxMeshFree.restype = None
    library.recvxApplyRoomLighting.argtypes = [ctypes.POINTER(Mesh), ctypes.c_uint32,
                                               ctypes.c_uint32]
    library.recvxApplyRoomLighting.restype = ctypes.c_int
    library.recvxRoomCutAttributeForPosition.argtypes = [ctypes.POINTER(Mesh),
                                                        ctypes.c_uint32,
                                                        ctypes.c_float,
                                                        ctypes.c_float,
                                                        ctypes.c_int]
    library.recvxRoomCutAttributeForPosition.restype = ctypes.c_uint32
    library.recvxRoomCameraForPosition.argtypes = [ctypes.POINTER(Mesh),
                                                   ctypes.c_uint32,
                                                   ctypes.c_float,
                                                   ctypes.c_float,
                                                   ctypes.c_int]
    library.recvxRoomCameraForPosition.restype = ctypes.c_uint32
    library.recvxDecodeTim2.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Texture)]
    library.recvxDecodeTim2.restype = ctypes.c_int
    library.recvxBuildRoomTextures.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(TextureSet)]
    library.recvxBuildRoomTextures.restype = ctypes.c_int
    library.recvxTexturesFree.argtypes = [ctypes.POINTER(TextureSet)]
    library.recvxTexturesFree.restype = None
    library.recvxTextureFree.argtypes = [ctypes.POINTER(Texture)]
    library.recvxTextureFree.restype = None
    library.recvxValidateMeshTextures.argtypes = [ctypes.POINTER(Mesh), ctypes.POINTER(TextureSet)]
    library.recvxValidateMeshTextures.restype = ctypes.c_int
    return library
