"""Native ctypes layout for host-side scene validation and previews."""
import ctypes


class SceneVertex(ctypes.Structure):
    _fields_ = [("color", ctypes.c_uint32), ("x", ctypes.c_float),
                ("y", ctypes.c_float), ("z", ctypes.c_float)]


class TexturedVertex(ctypes.Structure):
    _fields_ = [("u", ctypes.c_float), ("v", ctypes.c_float), ("color", ctypes.c_uint32),
                ("x", ctypes.c_float), ("y", ctypes.c_float), ("z", ctypes.c_float)]


class MeshBatch(ctypes.Structure):
    _fields_ = [("first_vertex", ctypes.c_uint32), ("vertex_count", ctypes.c_uint32),
                ("texture_index", ctypes.c_uint32), ("flags", ctypes.c_uint32)]


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
    _fields_ = [("storage", ctypes.c_void_p), ("vertices", ctypes.POINTER(SceneVertex)),
                ("vertex_count", ctypes.c_uint32), ("object_count", ctypes.c_uint32),
                ("model_count", ctypes.c_uint32), ("triangle_count", ctypes.c_uint32),
                ("center", ctypes.c_float * 3), ("radius", ctypes.c_float),
                ("wire_vertices", ctypes.POINTER(SceneVertex)),
                ("textured_vertices", ctypes.POINTER(TexturedVertex)),
                ("batches", ctypes.POINTER(MeshBatch)), ("batch_count", ctypes.c_uint32),
                ("storage_bytes", ctypes.c_uint32)]


def bind_scene(library):
    library.recvxBuildRoomScene.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                          ctypes.POINTER(Scene)]
    library.recvxBuildRoomScene.restype = ctypes.c_int
    library.recvxSceneFree.argtypes = [ctypes.POINTER(Scene)]
    library.recvxSceneFree.restype = None
    library.recvxBuildRoomMesh.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                         ctypes.POINTER(Mesh)]
    library.recvxBuildRoomMesh.restype = ctypes.c_int
    library.recvxMeshFree.argtypes = [ctypes.POINTER(Mesh)]
    library.recvxMeshFree.restype = None
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
