#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "psp/mesh.h"
#include "psp/room.h"

#define MAX_OBJECTS 512
#define MAX_DEPTH 64
#define MAX_TRIANGLES 16384
#define MAX_BATCHES 512

_Static_assert(sizeof(RecvxTexturedVertex) == 24, "GU textured vertex layout");
_Static_assert(offsetof(RecvxTexturedVertex, x) == 12, "GU position follows UV and color");

typedef struct Matrix { float value[3][4]; } Matrix;

typedef struct MeshReader {
    const unsigned char *bytes;
    size_t size, body, objects;
    unsigned int object_count;
    unsigned char *visited;
    uint32_t last_texture, last_flags;
    RecvxRoomMesh mesh;
    float minimum[3], maximum[3];
} MeshReader;

static unsigned int u16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float f32(const unsigned char *p)
{
    uint32_t bits = u32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int range(const MeshReader *reader, size_t offset, size_t length)
{
    return offset <= reader->size && length <= reader->size - offset;
}

static int relative(const MeshReader *reader, uint32_t offset, size_t length, size_t *result)
{
    if (offset == UINT32_MAX || offset > reader->size - reader->body)
        return -1;
    *result = reader->body + offset;
    return range(reader, *result, length) ? 0 : -1;
}

static int coordinate(float value)
{
    return isfinite(value) && fabsf(value) <= 1000000.0f;
}

static void rotate(Matrix *matrix, unsigned int axis, unsigned int angle)
{
    unsigned int row;
    float rad = (angle & 0xffff) * (6.283185307179586f / 65536.0f);
    float sine = sinf(rad), cosine = cosf(rad);
    unsigned int a = (axis + 1) % 3, b = (axis + 2) % 3;
    for (row = 0; row < 3; ++row) {
        float old_a = matrix->value[row][a], old_b = matrix->value[row][b];
        matrix->value[row][a] = old_a * cosine + old_b * sine;
        matrix->value[row][b] = -old_a * sine + old_b * cosine;
    }
}

static int object_matrix(const unsigned char *object, uint32_t flags,
                         const Matrix *parent, Matrix *matrix)
{
    unsigned int row, axis;
    *matrix = *parent;
    if (!(flags & 1)) {
        float position[3];
        for (axis = 0; axis < 3; ++axis) {
            position[axis] = f32(object + 8 + axis * 4);
            if (!coordinate(position[axis]))
                return -1;
        }
        for (row = 0; row < 3; ++row)
            for (axis = 0; axis < 3; ++axis)
                matrix->value[row][3] += matrix->value[row][axis] * position[axis];
    }
    if (!(flags & 2)) {
        static const unsigned int orders[2][3] = {{2, 1, 0}, {1, 0, 2}};
        for (axis = 0; axis < 3; ++axis) {
            unsigned int rotation_axis = orders[(flags & 0x20) != 0][axis];
            rotate(matrix, rotation_axis, u32(object + 20 + rotation_axis * 4));
        }
    }
    if (!(flags & 4))
        for (axis = 0; axis < 3; ++axis) {
            float scale = f32(object + 32 + axis * 4);
            if (!coordinate(scale))
                return -1;
            for (row = 0; row < 3; ++row)
                matrix->value[row][axis] *= scale;
        }
    for (row = 0; row < 3; ++row)
        for (axis = 0; axis < 4; ++axis)
            if (!coordinate(matrix->value[row][axis]))
                return -1;
    return 0;
}

static int point(const MeshReader *reader, size_t vertex_data, unsigned int first,
                 unsigned int count, unsigned int index, const Matrix *matrix, float result[3])
{
    float local[3];
    unsigned int axis, row;
    if (index < first || index - first >= count)
        return -1;
    for (axis = 0; axis < 3; ++axis) {
        local[axis] = f32(reader->bytes + vertex_data + (index - first) * 32 + axis * 4);
        if (!coordinate(local[axis]))
            return -1;
    }
    for (row = 0; row < 3; ++row) {
        result[row] = matrix->value[row][3];
        for (axis = 0; axis < 3; ++axis)
            result[row] += matrix->value[row][axis] * local[axis];
        if (!coordinate(result[row]))
            return -1;
    }
    return 0;
}

static uint32_t face_color(uint32_t argb, const float points[3][3])
{
    float a[3], b[3], normal[3], length, shade = 0.4f;
    unsigned int axis;
    for (axis = 0; axis < 3; ++axis) {
        a[axis] = points[1][axis] - points[0][axis];
        b[axis] = points[2][axis] - points[0][axis];
    }
    normal[0] = a[1] * b[2] - a[2] * b[1];
    normal[1] = a[2] * b[0] - a[0] * b[2];
    normal[2] = a[0] * b[1] - a[1] * b[0];
    length = sqrtf(normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
    if (length > 0)
        shade += 0.6f * fminf(1, fabsf(normal[0]*0.4f + normal[1]*0.8f + normal[2]*0.45f) / length);
    return 0xff000000u | ((uint32_t)(((argb >> 16) & 255) * shade)) |
           ((uint32_t)(((argb >> 8) & 255) * shade) << 8) |
           ((uint32_t)((argb & 255) * shade) << 16);
}

static int triangle(MeshReader *reader, size_t vertices, unsigned int first,
                    unsigned int count, const unsigned int indices[3],
                    const float uv[3][2], const Matrix *matrix, uint32_t color,
                    uint32_t texture, uint32_t flags)
{
    float points[3][3];
    unsigned int i, axis;
    if (reader->mesh.triangle_count == MAX_TRIANGLES)
        return -2;
    for (i = 0; i < 3; ++i)
        if (point(reader, vertices, first, count, indices[i], matrix, points[i]) != 0)
            return -1;
    if (!reader->mesh.batch_count || texture != reader->last_texture || flags != reader->last_flags) {
        if (reader->mesh.batch_count == MAX_BATCHES)
            return -2;
        if (reader->mesh.batches) {
            RecvxMeshBatch *batch = &reader->mesh.batches[reader->mesh.batch_count];
            batch->first_vertex = reader->mesh.vertex_count;
            batch->vertex_count = 0;
            batch->texture_index = texture;
            batch->flags = flags;
        }
        ++reader->mesh.batch_count;
        reader->last_texture = texture;
        reader->last_flags = flags;
    }
    if (reader->mesh.vertices != NULL) {
        uint32_t shaded = face_color(color, points);
        for (i = 0; i < 3; ++i) {
            RecvxSceneVertex *vertex = &reader->mesh.vertices[reader->mesh.vertex_count + i];
            vertex->color = shaded;
            vertex->x = points[i][0];
            vertex->y = points[i][1];
            vertex->z = points[i][2];
            RecvxTexturedVertex *textured = &reader->mesh.textured_vertices[reader->mesh.vertex_count + i];
            textured->u = uv[i][0];
            textured->v = uv[i][1];
            textured->color = shaded;
            textured->x = points[i][0];
            textured->y = points[i][1];
            textured->z = points[i][2];
        }
        reader->mesh.batches[reader->mesh.batch_count - 1].vertex_count += 3;
        for (i = 0; i < 3; ++i) {
            reader->mesh.wire_vertices[reader->mesh.triangle_count * 6 + i * 2] =
                reader->mesh.vertices[reader->mesh.vertex_count + i];
            reader->mesh.wire_vertices[reader->mesh.triangle_count * 6 + i * 2 + 1] =
                reader->mesh.vertices[reader->mesh.vertex_count + (i + 1) % 3];
        }
    }
    for (i = 0; i < 3; ++i)
        for (axis = 0; axis < 3; ++axis) {
            reader->minimum[axis] = fminf(reader->minimum[axis], points[i][axis]);
            reader->maximum[axis] = fmaxf(reader->maximum[axis], points[i][axis]);
        }
    ++reader->mesh.triangle_count;
    reader->mesh.vertex_count += 3;
    return 0;
}

static int model(MeshReader *reader, uint32_t offset, const Matrix *matrix)
{
    size_t header, vertex_chunk, polygon;
    unsigned int first, count;
    uint32_t color = 0xffb2b2b2;
    uint32_t texture = UINT32_MAX, sampler = 4, flip = 0;
    int result;
    if (relative(reader, offset, 24, &header) != 0)
        return -1;
    if (u32(reader->bytes + header) == UINT32_MAX &&
        u32(reader->bytes + header + 4) == UINT32_MAX)
        return 0;
    if (relative(reader, u32(reader->bytes + header), 64, &vertex_chunk) != 0 ||
        relative(reader, u32(reader->bytes + header + 4), 2, &polygon) != 0)
        return -1;
    if (reader->bytes[vertex_chunk] != 51)
        return -4;
    first = u16(reader->bytes + vertex_chunk + 4);
    count = u16(reader->bytes + vertex_chunk + 6);
    if (count == 0 || first + count > 65536 ||
        u16(reader->bytes + vertex_chunk + 2) * 4u + 4u != 64u + count * 32u ||
        !range(reader, vertex_chunk, 64u + count * 32u))
        return -1;
    ++reader->mesh.model_count;
    for (;;) {
        unsigned int type, words;
        size_t end, position;
        if (!range(reader, polygon, 2))
            return -1;
        type = reader->bytes[polygon];
        if (type == 255)
            return 0;
        if (type <= 3) {
            polygon += 2;
            continue;
        }
        if (type == 8) {
            if (!range(reader, polygon, 4))
                return -1;
            unsigned int parameters = u16(reader->bytes + polygon + 2);
            texture = parameters & 0xfff;
            sampler = ((reader->bytes[polygon + 1] >> 4) & 3) | ((parameters & 0xc000) ? 4 : 0);
            flip = reader->bytes[polygon + 1] >> 6;
            polygon += 4;
            continue;
        }
        if (!((type >= 17 && type <= 23) || (type >= 64 && type <= 66)))
            return -4;
        if (!range(reader, polygon, 4))
            return -1;
        words = u16(reader->bytes + polygon + 2);
        if (!range(reader, polygon, 4u + words * 2u))
            return -1;
        end = polygon + 4 + words * 2;
        position = polygon + 4;
        if (type < 64) {
            unsigned int mask = type - 16;
            unsigned int components = (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1);
            if (words != components * 2)
                return -1;
            if (mask & 1)
                color = u32(reader->bytes + position);
        } else {
            unsigned int strips, attributes, stride = type == 64 ? 2 : 6;
            if (end - position < 2)
                return -1;
            strips = u16(reader->bytes + position);
            attributes = strips >> 14;
            strips &= 0x3fff;
            position += 2;
            while (strips-- != 0) {
                unsigned int encoded, length, i, previous[2] = {0, 0};
                float previous_uv[2][2] = {{0}};
                if (end - position < 2)
                    return -1;
                encoded = u16(reader->bytes + position);
                length = encoded & 0x8000 ? 65536 - encoded : encoded;
                position += 2;
                if (length < 3)
                    return -1;
                for (i = 0; i < length; ++i) {
                    unsigned int index;
                    float current_uv[2] = {0, 0};
                    size_t bytes = stride + (i >= 2 ? attributes * 2 : 0);
                    if (bytes > end - position)
                        return -1;
                    index = u16(reader->bytes + position);
                    if (index < first || index - first >= count)
                        return -1;
                    if (type != 64) {
                        float divisor = type == 65 ? 255.0f : 1023.0f;
                        current_uv[0] = u16(reader->bytes + position + 2) / divisor;
                        current_uv[1] = u16(reader->bytes + position + 4) / divisor;
                        if (flip & 1)
                            current_uv[0] = 1 - current_uv[0];
                        if (flip & 2)
                            current_uv[1] = 1 - current_uv[1];
                    }
                    if (i >= 2) {
                        unsigned int indices[3] = {previous[0], previous[1], index};
                        float uv[3][2];
                        memcpy(uv[0], previous_uv[0], sizeof(uv[0]));
                        memcpy(uv[1], previous_uv[1], sizeof(uv[1]));
                        memcpy(uv[2], current_uv, sizeof(uv[2]));
                        if (((i - 2) & 1) ^ ((encoded & 0x8000) != 0)) {
                            indices[0] = previous[1];
                            indices[1] = previous[0];
                            memcpy(uv[0], previous_uv[1], sizeof(uv[0]));
                            memcpy(uv[1], previous_uv[0], sizeof(uv[1]));
                        }
                        result = triangle(reader, vertex_chunk + 64, first, count,
                                          indices, uv, matrix, color,
                                          type == 64 ? UINT32_MAX : texture,
                                          type == 64 ? 0 : sampler);
                        if (result != 0)
                            return result;
                    }
                    previous[0] = previous[1];
                    previous[1] = index;
                    memcpy(previous_uv[0], previous_uv[1], sizeof(previous_uv[0]));
                    memcpy(previous_uv[1], current_uv, sizeof(previous_uv[1]));
                    position += bytes;
                }
            }
            // The exporter optionally pads strips by one zero word
            if (end - position > 2 || (end > position && u16(reader->bytes + position) != 0))
                return -1;
        }
        polygon = end;
    }
}

static int object_index(const MeshReader *reader, uint32_t offset, unsigned int *index)
{
    size_t absolute;
    if (relative(reader, offset, 52, &absolute) != 0 || absolute < reader->objects ||
        (absolute - reader->objects) % 52 != 0)
        return -1;
    *index = (unsigned int)((absolute - reader->objects) / 52);
    return *index < reader->object_count ? 0 : -1;
}

static int tree(MeshReader *reader, unsigned int index, const Matrix *parent, unsigned int depth)
{
    if (depth >= MAX_DEPTH)
        return -2;
    for (;;) {
        const unsigned char *object = reader->bytes + reader->objects + index * 52;
        uint32_t flags = u32(object), model_offset = u32(object + 4);
        uint32_t child = u32(object + 44), sibling = u32(object + 48);
        Matrix matrix;
        unsigned int next;
        int result;
        if (reader->visited[index])
            return -1;
        reader->visited[index] = 1;
        if (object_matrix(object, flags, parent, &matrix) != 0)
            return -1;
        if (!(flags & 8) && model_offset != UINT32_MAX) {
            result = model(reader, model_offset, &matrix);
            if (result != 0)
                return result;
        }
        if (!(flags & 16) && child != UINT32_MAX) {
            if (object_index(reader, child, &next) != 0)
                return -1;
            result = tree(reader, next, &matrix, depth + 1);
            if (result != 0)
                return result;
        }
        if (sibling == UINT32_MAX)
            return 0;
        if (object_index(reader, sibling, &index) != 0)
            return -1;
    }
}

void recvxMeshFree(RecvxRoomMesh *mesh)
{
    if (mesh != NULL) {
        free(mesh->storage);
        memset(mesh, 0, sizeof(*mesh));
    }
}

int recvxBuildRoomMesh(const void *room, size_t size, RecvxRoomMesh *mesh)
{
    MeshReader reader = {0};
    RecvxRoomInfo info;
    Matrix identity = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}}};
    size_t directory, blob;
    uint32_t object_offset;
    unsigned int axis, pass;
    int result = 0;
    if (mesh == NULL || recvxInspectRoom(room, size, &info) != 0)
        return -1;
    reader.bytes = room;
    reader.size = size;
    directory = u32(reader.bytes + 20);
    if (directory == 0) {
        recvxMeshFree(mesh);
        return 0;
    }
    if (!range(&reader, directory, 4))
        return -1;
    blob = u32(reader.bytes + directory);
    if (!range(&reader, blob, 16) || memcmp(reader.bytes + blob, "MDL", 3) != 0)
        return -1;
    if (!(reader.bytes[blob + 3] & 0x80))
        return -4;
    reader.body = blob + u16(reader.bytes + blob + 4);
    reader.object_count = u16(reader.bytes + blob + 6);
    if (u16(reader.bytes + blob + 4) < 16 || reader.body > size || reader.object_count == 0)
        return -1;
    if (reader.object_count > MAX_OBJECTS)
        return -2;
    object_offset = u32(reader.bytes + blob + 12);
    if (relative(&reader, object_offset, reader.object_count * 52, &reader.objects) != 0)
        return -1;
    reader.visited = calloc(reader.object_count, 1);
    if (reader.visited == NULL)
        return -3;
    reader.mesh.object_count = reader.object_count;
    for (pass = 0; pass < 2; ++pass) {
        memset(reader.visited, 0, reader.object_count);
        for (axis = 0; axis < 3; ++axis) {
            reader.minimum[axis] = INFINITY;
            reader.maximum[axis] = -INFINITY;
        }
        reader.mesh.model_count = reader.mesh.vertex_count = reader.mesh.triangle_count = 0;
        reader.mesh.batch_count = 0;
        result = tree(&reader, 0, &identity, 0);
        if (result != 0)
            goto done;
        if (pass == 0 && reader.mesh.vertex_count != 0) {
            reader.mesh.storage_bytes = reader.mesh.vertex_count *
                (3 * sizeof(RecvxSceneVertex) + sizeof(RecvxTexturedVertex)) +
                reader.mesh.batch_count * sizeof(RecvxMeshBatch);
            reader.mesh.storage = malloc(reader.mesh.storage_bytes + 63);
            if (reader.mesh.storage == NULL) {
                result = -3;
                goto done;
            }
            reader.mesh.vertices = (RecvxSceneVertex *)
                (((uintptr_t)reader.mesh.storage + 63) & ~(uintptr_t)63);
            reader.mesh.wire_vertices = reader.mesh.vertices + reader.mesh.vertex_count;
            reader.mesh.textured_vertices = (RecvxTexturedVertex *)(reader.mesh.vertices + reader.mesh.vertex_count * 3);
            reader.mesh.batches = (RecvxMeshBatch *)(reader.mesh.textured_vertices + reader.mesh.vertex_count);
        }
    }
    if (reader.mesh.vertex_count) {
        float diagonal = 0;
        for (axis = 0; axis < 3; ++axis) {
            float extent = reader.maximum[axis] - reader.minimum[axis];
            reader.mesh.center[axis] = (reader.maximum[axis] + reader.minimum[axis]) * 0.5f;
            diagonal += extent * extent;
        }
        reader.mesh.radius = fmaxf(1, sqrtf(diagonal) * 0.5f);
    }
    recvxMeshFree(mesh);
    *mesh = reader.mesh;
    memset(&reader.mesh, 0, sizeof(reader.mesh));
done:
    free(reader.visited);
    recvxMeshFree(&reader.mesh);
    return result;
}

int recvxValidateMeshTextures(const RecvxRoomMesh *mesh, const RecvxTextureSet *textures)
{
    uint32_t i;
    if (!mesh || !textures || (textures->count && !textures->textures))
        return -1;
    for (i = 0; i < mesh->batch_count; ++i)
        if (mesh->batches[i].texture_index != UINT32_MAX &&
            mesh->batches[i].texture_index >= textures->count)
            return -1;
    return 0;
}
