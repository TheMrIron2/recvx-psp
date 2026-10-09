"""Native ctypes layout for host-side scene validation and previews."""
import ctypes


class SceneVertex(ctypes.Structure):
    _fields_ = [("color", ctypes.c_uint32), ("x", ctypes.c_float),
                ("y", ctypes.c_float), ("z", ctypes.c_float)]


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
                ("wire_vertices", ctypes.POINTER(SceneVertex))]


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
    return library
