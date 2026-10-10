#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "psp/mesh.h"
#include "psp/ninja_math.h"
#include "psp/room.h"

#define MAX_OBJECTS 512
#define MAX_DEPTH 64
#define MAX_TRIANGLES 16384
#define MAX_BATCHES 512
#define MAX_CAMERA_CUTS 64
#define MAX_CUT_AREAS 16384
#define MAX_ROOM_LIGHTS 256
#define CHUNK_OBJECT_SIZE sizeof(RecvxChunkObjectDisk)

_Static_assert(sizeof(RecvxMeshVertex) == 36, "GU mesh vertex layout");
_Static_assert(offsetof(RecvxMeshVertex, nx) == 12, "GU normal follows color");
_Static_assert(offsetof(RecvxMeshVertex, x) == 24, "GU position follows normal");
_Static_assert(sizeof(RecvxMeshBatch) == 24, "Mesh batch layout");

typedef struct Matrix { float value[3][4]; } Matrix;

typedef struct MeshReader {
    const unsigned char *bytes;
    size_t size, body, objects;
    unsigned int object_count;
    unsigned char *visited;
    uint32_t vertex_limit, batch_limit, motion_frame;
    const RecvxPlayerMotionAsset *motion;
    const RecvxPlayerModelAsset *player_model;
    const uint8_t *skin_weights;
    uint32_t skin_weight_count;
    const Matrix *bind_matrices, *skin_parent_matrices, *skin_matrices;
    uint32_t current_object, last_object, last_texture, last_flags, last_blend_state;
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
    float sine, cosine;
    unsigned int a = (axis + 1) % 3, b = (axis + 2) % 3;
    recvxNinjaSinCos((RecvxNinjaAngle)angle, &sine, &cosine);
    for (row = 0; row < 3; ++row) {
        float old_a = matrix->value[row][a], old_b = matrix->value[row][b];
        matrix->value[row][a] = old_a * cosine + old_b * sine;
        matrix->value[row][b] = -old_a * sine + old_b * cosine;
    }
}

static int motion_angles(const MeshReader *reader, unsigned int object_index,
                         unsigned int angles[3])
{
    const RecvxPlayerMotionAsset *motion = reader->motion;
    uint32_t base, reference, frame;
    size_t object_data, key_data;
    if (motion == NULL || object_index >= motion->object_count)
        return 0;
    if (motion->data == NULL || motion->size < 16 ||
        motion->frame_count == 0 || motion->object_count != reader->object_count)
        return -1;
    base = (uint32_t)u16(motion->data +
        offsetof(RecvxPlayerMotionDiskHeader, header_size));
    if (base > motion->size || motion->data_offset > motion->size - base ||
        (size_t)object_index * sizeof(RecvxMotionObjectDiskTrack) >
            motion->size - base - motion->data_offset ||
        motion->size - base - motion->data_offset -
            object_index * sizeof(RecvxMotionObjectDiskTrack) <
                sizeof(RecvxMotionObjectDiskTrack))
        return -1;
    object_data = (size_t)base + motion->data_offset +
                  object_index * sizeof(RecvxMotionObjectDiskTrack);
    reference = u32(motion->data + object_data +
        offsetof(RecvxMotionObjectDiskTrack, angle_offset));
    if (reference == UINT32_MAX)
        return 0;
    frame = reader->motion_frame % motion->frame_count;
    if (reference > motion->size - base ||
        (size_t)frame > (motion->size - base - reference) / 6 ||
        motion->size - base - reference -
            (size_t)frame * sizeof(RecvxMotionAngleDiskKey) <
                sizeof(RecvxMotionAngleDiskKey))
        return -1;
    key_data = (size_t)base + reference +
               (size_t)frame * sizeof(RecvxMotionAngleDiskKey);
    angles[0] = u16(motion->data + key_data);
    angles[1] = u16(motion->data + key_data + 2);
    angles[2] = u16(motion->data + key_data + 4);
    return 1;
}

static int object_matrix(const MeshReader *reader, const unsigned char *object,
                         unsigned int object_index, uint32_t flags,
                         const Matrix *parent, Matrix *matrix)
{
    unsigned int row, axis;
    unsigned int angles[3];
    int has_motion_angles = motion_angles(reader, object_index, angles);
    if (has_motion_angles < 0)
        return -1;
    *matrix = *parent;
    if (!(flags & 1)) {
        float position[3];
        for (axis = 0; axis < 3; ++axis) {
            position[axis] = f32(object +
                offsetof(RecvxChunkObjectDisk, position) + axis * sizeof(float));
            if (!coordinate(position[axis]))
                return -1;
        }
        for (row = 0; row < 3; ++row)
            for (axis = 0; axis < 3; ++axis)
                matrix->value[row][3] += matrix->value[row][axis] * position[axis];
    }
    if (has_motion_angles || !(flags & 2)) {
        static const unsigned int orders[2][3] = {{2, 1, 0}, {1, 0, 2}};
        for (axis = 0; axis < 3; ++axis) {
            unsigned int rotation_axis = orders[(flags & 0x20) != 0][axis];
            uint32_t angle = has_motion_angles ? angles[rotation_axis] :
                u32(object + offsetof(RecvxChunkObjectDisk, angles) +
                    rotation_axis * sizeof(uint32_t));
            rotate(matrix, rotation_axis, angle);
        }
    }
    if (!(flags & 4))
        for (axis = 0; axis < 3; ++axis) {
            float scale = f32(object +
                offsetof(RecvxChunkObjectDisk, scale) + axis * sizeof(float));
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
    int skinned = reader->skin_parent_matrices != NULL &&
                  reader->skin_matrices != NULL && reader->skin_weights != NULL &&
                  reader->player_model != NULL &&
                  reader->current_object < reader->object_count &&
                  reader->current_object < reader->player_model->object_count &&
                  index >= first && index - first < reader->skin_weight_count;
    if (index < first || index - first >= count)
        return -1;
    for (axis = 0; axis < 3; ++axis) {
        local[axis] = f32(reader->bytes + vertex_data + (index - first) * 32 + axis * 4);
        if (!coordinate(local[axis]))
            return -1;
    }
    if (skinned) {
        uint32_t influence = u32(reader->skin_weights + (index - first) * 4);
        unsigned int bone = influence >> 24;
        unsigned int weight_code = (influence >> 16) & 0xff;
        float weight = (weight_code + 1) * 0.1f;
        float parent_skinned[3], skinned[3];
        if (bone >= reader->object_count || weight_code >= 10)
            return -1;
        for (axis = 0; axis < 3; ++axis)
            local[axis] += reader->bind_matrices[reader->current_object].value[axis][3] -
                           reader->bind_matrices[bone].value[axis][3];
        for (axis = 0; axis < 3; ++axis)
            if (!coordinate(local[axis]))
                return -1;
        for (row = 0; row < 3; ++row) {
            parent_skinned[row] = reader->skin_parent_matrices[bone].value[row][3];
            skinned[row] = reader->skin_matrices[bone].value[row][3];
            for (axis = 0; axis < 3; ++axis) {
                parent_skinned[row] +=
                    reader->skin_parent_matrices[bone].value[row][axis] * local[axis];
                skinned[row] += reader->skin_matrices[bone].value[row][axis] * local[axis];
            }
            result[row] = parent_skinned[row] +
                          (skinned[row] - parent_skinned[row]) * weight;
            if (!coordinate(result[row]))
                return -1;
        }
    } else {
        for (row = 0; row < 3; ++row) {
            result[row] = matrix->value[row][3];
            for (axis = 0; axis < 3; ++axis)
                result[row] += matrix->value[row][axis] * local[axis];
            if (!coordinate(result[row]))
                return -1;
        }
    }
    return 0;
}

static uint32_t diffuse_color(uint32_t argb)
{
    return (argb & 0xff000000u) | ((argb & 0x000000ffu) << 16) |
           (argb & 0x0000ff00u) | ((argb & 0x00ff0000u) >> 16);
}

static void triangle_normal(const float points[3][3], float normal[3])
{
    float x1 = points[1][0] - points[0][0];
    float y1 = points[1][1] - points[0][1];
    float z1 = points[1][2] - points[0][2];
    float x2 = points[2][0] - points[0][0];
    float y2 = points[2][1] - points[0][1];
    float z2 = points[2][2] - points[0][2];
    float length;
    normal[0] = y1 * z2 - z1 * y2;
    normal[1] = z1 * x2 - x1 * z2;
    normal[2] = x1 * y2 - y1 * x2;
    length = sqrtf(normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
    if (length > 0) {
        normal[0] /= length;
        normal[1] /= length;
        normal[2] /= length;
    } else {
        normal[0] = 0;
        normal[1] = 1;
        normal[2] = 0;
    }
}

static int triangle(MeshReader *reader, size_t vertices, unsigned int first,
                    unsigned int count, const unsigned int indices[3],
                    const float uv[3][2], const Matrix *matrix, uint32_t color,
                    uint32_t texture, uint32_t flags, uint32_t object_index,
                    uint32_t blend_state)
{
    float points[3][3];
    float normal[3];
    unsigned int i, axis;
    if (reader->mesh.triangle_count == MAX_TRIANGLES)
        return -2;
    for (i = 0; i < 3; ++i)
        if (point(reader, vertices, first, count, indices[i], matrix, points[i]) != 0)
            return -1;
    triangle_normal(points, normal);
    if (!reader->mesh.batch_count || texture != reader->last_texture || flags != reader->last_flags ||
        object_index != reader->last_object || blend_state != reader->last_blend_state) {
        if (reader->mesh.batch_count == MAX_BATCHES)
            return -2;
        if (reader->mesh.vertices != NULL && reader->batch_limit != 0 &&
            reader->mesh.batch_count >= reader->batch_limit)
            return -2;
        if (reader->mesh.batches) {
            RecvxMeshBatch *batch = &reader->mesh.batches[reader->mesh.batch_count];
            batch->first_vertex = reader->mesh.vertex_count;
            batch->vertex_count = 0;
            batch->texture_index = texture;
            batch->flags = flags;
            batch->object_index = object_index;
            batch->blend_state = blend_state;
        }
        ++reader->mesh.batch_count;
        reader->last_object = object_index;
        reader->last_texture = texture;
        reader->last_flags = flags;
        reader->last_blend_state = blend_state;
    }
    if (reader->mesh.vertices != NULL) {
        uint32_t base_color = diffuse_color(color);
        if (reader->vertex_limit != 0 &&
            (reader->vertex_limit < 3 ||
             reader->mesh.vertex_count > reader->vertex_limit - 3))
            return -2;
        for (i = 0; i < 3; ++i) {
            RecvxMeshVertex *vertex = &reader->mesh.vertices[reader->mesh.vertex_count + i];
            vertex->u = uv[i][0];
            vertex->v = uv[i][1];
            vertex->color = base_color;
            vertex->nx = normal[0];
            vertex->ny = normal[1];
            vertex->nz = normal[2];
            vertex->x = points[i][0];
            vertex->y = points[i][1];
            vertex->z = points[i][2];
            reader->mesh.material_colors[reader->mesh.vertex_count + i] = base_color;
        }
        reader->mesh.batches[reader->mesh.batch_count - 1].vertex_count += 3;
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
    static const unsigned char blend_sources[8] = {11, 10, 3, 5, 8, 6, 2, 4};
    static const unsigned char blend_destinations[8] = {11, 10, 9, 7, 8, 6, 2, 4};
    size_t header, vertex_chunk, polygon;
    unsigned int first, count;
    uint32_t color = 0xffb2b2b2;
    uint32_t texture = UINT32_MAX, sampler = 4, flip = 0;
    uint32_t blend_source = 8, blend_destination = 6;
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
    reader->skin_weights = NULL;
    reader->skin_weight_count = 0;
    if (reader->player_model != NULL && reader->skin_matrices != NULL) {
        int skin_result = recvxPlayerSkinWeights(reader->player_model,
                                                 reader->current_object,
                                                 reader->mesh.model_count,
                                                 &reader->skin_weights,
                                                 &reader->skin_weight_count);
        if (skin_result < 0 || (skin_result > 0 &&
            reader->skin_weight_count != count))
            return -1;
    }
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
            blend_source = blend_sources[(reader->bytes[polygon + 1] >> 3) & 7];
            blend_destination = blend_destinations[reader->bytes[polygon + 1] & 7];
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
                        uint32_t blend_state = (blend_source << 8) | blend_destination;
                        if (reader->bytes[polygon + 1] & 8)
                            blend_state |= RECVX_BLEND_ALPHA;
                        if (reader->bytes[polygon + 1] & 0x10)
                            blend_state |= RECVX_BLEND_DOUBLE_SIDED;
                        result = triangle(reader, vertex_chunk + 64, first, count,
                                          indices, uv, matrix, color,
                                          type == 64 ? UINT32_MAX : texture,
                                          type == 64 ? 0 : sampler,
                                          reader->current_object, blend_state);
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
    if (relative(reader, offset, CHUNK_OBJECT_SIZE, &absolute) != 0 ||
        absolute < reader->objects ||
        (absolute - reader->objects) % CHUNK_OBJECT_SIZE != 0)
        return -1;
    *index = (unsigned int)((absolute - reader->objects) / CHUNK_OBJECT_SIZE);
    return *index < reader->object_count ? 0 : -1;
}

static int object_matrix_without_rotation(const unsigned char *object,
                                          uint32_t flags, const Matrix *parent,
                                          Matrix *matrix)
{
    unsigned int row, axis;
    *matrix = *parent;
    if (!(flags & 1)) {
        float position[3];
        for (axis = 0; axis < 3; ++axis) {
            position[axis] = f32(object +
                offsetof(RecvxChunkObjectDisk, position) + axis * sizeof(float));
            if (!coordinate(position[axis]))
                return -1;
        }
        for (row = 0; row < 3; ++row)
            for (axis = 0; axis < 3; ++axis)
                matrix->value[row][3] += matrix->value[row][axis] * position[axis];
    }
    if (!(flags & 4))
        for (axis = 0; axis < 3; ++axis) {
            float scale = f32(object +
                offsetof(RecvxChunkObjectDisk, scale) + axis * sizeof(float));
            if (!coordinate(scale))
                return -1;
            for (row = 0; row < 3; ++row)
                matrix->value[row][axis] *= scale;
        }
    return 0;
}

static int hierarchy_matrices(MeshReader *reader, unsigned int index,
                              const Matrix *parent, unsigned int depth,
                              Matrix *matrices, Matrix *parent_matrices)
{
    if (depth >= MAX_DEPTH)
        return -2;
    for (;;) {
        const unsigned char *object = reader->bytes + reader->objects +
                                      index * CHUNK_OBJECT_SIZE;
        uint32_t flags = u32(object + offsetof(RecvxChunkObjectDisk, flags));
        uint32_t child = u32(object +
            offsetof(RecvxChunkObjectDisk, child_offset));
        uint32_t sibling = u32(object +
            offsetof(RecvxChunkObjectDisk, sibling_offset));
        Matrix matrix;
        unsigned int next;
        int result;
        if (reader->visited[index])
            return -1;
        reader->visited[index] = 1;
        if (object_matrix(reader, object, index, flags, parent, &matrix) != 0)
            return -1;
        matrices[index] = matrix;
        if (parent_matrices != NULL &&
            object_matrix_without_rotation(object, flags, parent,
                                           &parent_matrices[index]) != 0)
            return -1;
        if (!(flags & 16) && child != UINT32_MAX) {
            if (object_index(reader, child, &next) != 0)
                return -1;
            result = hierarchy_matrices(reader, next, &matrix, depth + 1,
                                        matrices, parent_matrices);
            if (result != 0)
                return result;
        }
        if (sibling == UINT32_MAX)
            return 0;
        if (object_index(reader, sibling, &index) != 0)
            return -1;
    }
}

static int tree(MeshReader *reader, unsigned int index, const Matrix *parent, unsigned int depth)
{
    if (depth >= MAX_DEPTH)
        return -2;
    for (;;) {
        const unsigned char *object = reader->bytes + reader->objects +
                                      index * CHUNK_OBJECT_SIZE;
        uint32_t flags = u32(object + offsetof(RecvxChunkObjectDisk, flags));
        uint32_t model_offset = u32(object +
            offsetof(RecvxChunkObjectDisk, model_offset));
        uint32_t child = u32(object +
            offsetof(RecvxChunkObjectDisk, child_offset));
        uint32_t sibling = u32(object +
            offsetof(RecvxChunkObjectDisk, sibling_offset));
        Matrix matrix;
        unsigned int next;
        int result;
        if (reader->visited[index])
            return -1;
        reader->visited[index] = 1;
        if (object_matrix(reader, object, index, flags, parent, &matrix) != 0)
            return -1;
        if (!(flags & 8) && model_offset != UINT32_MAX) {
            reader->current_object = index;
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
        free(mesh->wire_storage);
        free(mesh->storage);
        free(mesh->cuts);
        free(mesh->cameras);
        free(mesh->cut_areas);
        free(mesh->lights);
        memset(mesh, 0, sizeof(*mesh));
    }
}

int recvxBuildPlayerMesh(const RecvxPlayerModelAsset *model, RecvxRoomMesh *mesh)
{
    MeshReader reader = {0};
    Matrix identity = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}}};
    size_t object_bytes;
    unsigned int axis, pass;
    int result = 0;
    if (model == NULL || mesh == NULL || model->data == NULL ||
        !model->chunk_model || model->object_count == 0 ||
        model->object_count > MAX_OBJECTS || model->header_offset > model->size)
        return -1;
    reader.bytes = model->data;
    reader.size = model->size;
    reader.body = model->header_offset;
    reader.object_count = model->object_count;
    object_bytes = (size_t)reader.object_count * CHUNK_OBJECT_SIZE;
    if (relative(&reader, model->object_offset, object_bytes, &reader.objects) != 0)
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
        reader.mesh.model_count = 0;
        reader.mesh.vertex_count = 0;
        reader.mesh.triangle_count = 0;
        reader.mesh.batch_count = 0;
        result = tree(&reader, 0, &identity, 0);
        if (result != 0)
            goto done;
        if (pass == 0 && reader.mesh.vertex_count != 0) {
            reader.mesh.storage_bytes = reader.mesh.vertex_count *
                (sizeof(RecvxMeshVertex) + sizeof(uint32_t)) +
                reader.mesh.batch_count * sizeof(RecvxMeshBatch);
            reader.mesh.storage = malloc(reader.mesh.storage_bytes + 63);
            if (reader.mesh.storage == NULL) {
                result = -3;
                goto done;
            }
            reader.mesh.vertices = (RecvxMeshVertex *)
                (((uintptr_t)reader.mesh.storage + 63) & ~(uintptr_t)63);
            reader.mesh.material_colors = (uint32_t *)
                ((unsigned char *)reader.mesh.vertices +
                 reader.mesh.vertex_count * sizeof(*reader.mesh.vertices));
            reader.mesh.textured_vertices = reader.mesh.vertices;
            reader.mesh.batches = (RecvxMeshBatch *)
                ((unsigned char *)reader.mesh.material_colors +
                 reader.mesh.vertex_count * sizeof(*reader.mesh.material_colors));
            reader.vertex_limit = reader.mesh.vertex_count;
            reader.batch_limit = reader.mesh.batch_count;
        }
    }
    if (reader.mesh.vertex_count != 0) {
        float diagonal = 0;
        for (axis = 0; axis < 3; ++axis) {
            float extent = reader.maximum[axis] - reader.minimum[axis];
            reader.mesh.center[axis] =
                (reader.maximum[axis] + reader.minimum[axis]) * 0.5f;
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

int recvxPosePlayerMesh(const RecvxPlayerModelAsset *model,
                        const RecvxPlayerMotionAsset *motion, uint32_t frame,
                        RecvxRoomMesh *mesh)
{
    MeshReader reader = {0};
    unsigned char visited[MAX_OBJECTS];
    Matrix identity = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}}};
    size_t object_bytes;
    unsigned int axis;
    int result;
    if (model == NULL || motion == NULL || mesh == NULL || model->data == NULL ||
        !model->chunk_model || model->object_count == 0 ||
        model->object_count > MAX_OBJECTS || model->header_offset > model->size ||
        mesh->storage == NULL || mesh->vertices == NULL || motion->present == 0 ||
        motion->object_count != model->object_count || motion->frame_count == 0 ||
        motion->size == 0)
        return -1;
    Matrix bind_matrices[model->object_count];
    Matrix posed_matrices[model->object_count];
    Matrix posed_parent_matrices[model->object_count];
    Matrix skin_parent_matrices[model->object_count];
    Matrix skin_matrices[model->object_count];
    reader.bytes = model->data;
    reader.size = model->size;
    reader.body = model->header_offset;
    reader.object_count = model->object_count;
    object_bytes = (size_t)reader.object_count * CHUNK_OBJECT_SIZE;
    if (relative(&reader, model->object_offset, object_bytes, &reader.objects) != 0)
        return -1;
    reader.visited = visited;
    memset(visited, 0, reader.object_count);
    reader.motion = motion;
    reader.motion_frame = frame % motion->frame_count;
    reader.player_model = model;
    reader.vertex_limit = mesh->vertex_count;
    reader.batch_limit = mesh->batch_count;
    {
        unsigned int bone;
        reader.bind_matrices = bind_matrices;
        memset(visited, 0, reader.object_count);
        reader.motion = NULL;
        result = hierarchy_matrices(&reader, 0, &identity, 0, bind_matrices,
                                    NULL);
        if (result != 0)
            return result;
        memset(visited, 0, reader.object_count);
        reader.motion = motion;
        result = hierarchy_matrices(&reader, 0, &identity, 0, posed_matrices,
                                    posed_parent_matrices);
        if (result != 0)
            return result;
        for (bone = 0; bone < reader.object_count; ++bone) {
            skin_parent_matrices[bone] = posed_parent_matrices[bone];
            skin_matrices[bone] = posed_matrices[bone];
        }
        reader.skin_parent_matrices = skin_parent_matrices;
        reader.skin_matrices = skin_matrices;
    }
    memset(visited, 0, reader.object_count);
    reader.mesh = *mesh;
    reader.mesh.model_count = 0;
    reader.mesh.vertex_count = 0;
    reader.mesh.triangle_count = 0;
    reader.mesh.batch_count = 0;
    for (axis = 0; axis < 3; ++axis) {
        reader.minimum[axis] = INFINITY;
        reader.maximum[axis] = -INFINITY;
    }
    result = tree(&reader, 0, &identity, 0);
    if (result == 0 &&
        (reader.mesh.vertex_count != mesh->vertex_count ||
         reader.mesh.triangle_count != mesh->triangle_count ||
         reader.mesh.batch_count != mesh->batch_count ||
         reader.mesh.model_count != mesh->model_count))
        result = -1;
    if (result == 0)
        *mesh = reader.mesh;
    return result;
}

int recvxMeshSetWireframe(RecvxRoomMesh *mesh, int enabled)
{
    RecvxSceneVertex *wire_vertices;
    size_t wire_count, wire_bytes, i;
    if (mesh == NULL)
        return -1;
    if (!enabled) {
        free(mesh->wire_storage);
        mesh->wire_storage = NULL;
        mesh->wire_vertices = NULL;
        return 0;
    }
    if (mesh->wire_vertices != NULL)
        return 0;
    if (mesh->vertices == NULL || mesh->triangle_count > UINT32_MAX / 3 ||
        mesh->vertex_count != mesh->triangle_count * 3)
        return -1;
    wire_count = (size_t)mesh->triangle_count * 6;
    if (wire_count > SIZE_MAX / sizeof(*wire_vertices))
        return -1;
    wire_bytes = wire_count * sizeof(*wire_vertices);
    if (wire_bytes > SIZE_MAX - 15)
        return -1;
    mesh->wire_storage = malloc(wire_bytes + 15);
    if (mesh->wire_storage == NULL)
        return -3;
    wire_vertices = (RecvxSceneVertex *)
        (((uintptr_t)mesh->wire_storage + 15) & ~(uintptr_t)15);
    for (i = 0; i < mesh->triangle_count; ++i) {
        const RecvxMeshVertex *triangle = &mesh->vertices[i * 3];
        unsigned int edge;
        for (edge = 0; edge < 3; ++edge) {
            const RecvxMeshVertex *a = &triangle[edge];
            const RecvxMeshVertex *b = &triangle[(edge + 1) % 3];
            RecvxSceneVertex *line = &wire_vertices[i * 6 + edge * 2];
            line[0].color = a->color;
            line[0].x = a->x;
            line[0].y = a->y;
            line[0].z = a->z;
            line[1].color = b->color;
            line[1].x = b->x;
            line[1].y = b->y;
            line[1].z = b->z;
        }
    }
    mesh->wire_vertices = wire_vertices;
    return 0;
}

int recvxBuildRoomMesh(const void *room, size_t size, RecvxRoomMesh *mesh)
{
    MeshReader reader = {0};
    RecvxRoomInfo info;
    Matrix identity = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}}};
    size_t directory, blob;
    uint32_t object_offset;
    unsigned int axis, pass, cut, camera, word, light, ambient_index;
    size_t total_cut_areas = 0;
    int result = 0;
    if (mesh == NULL || recvxInspectRoom(room, size, &info) != 0)
        return -1;
    reader.bytes = room;
    reader.size = size;
    reader.mesh.cut_count = info.counts[RECVX_ROOM_COUNT_CUT];
    if (reader.mesh.cut_count > MAX_CAMERA_CUTS)
        return -2;
    if (reader.mesh.cut_count != 0) {
        uint32_t cut_offset = u32(reader.bytes + info.rom_offset);
        for (cut = 0; cut < reader.mesh.cut_count; ++cut) {
            const unsigned char *cut_record = reader.bytes + cut_offset + cut * 0x2a8;
            uint32_t area_offset = u32(cut_record + 4);
            unsigned int area_count = cut_record[3];
            if (area_count != 0 && ((area_offset & 3) != 0 ||
                !range(&reader, area_offset, area_count * 0x20))) {
                result = -1;
                goto done;
            }
            total_cut_areas += area_count;
            if (total_cut_areas > MAX_CUT_AREAS) {
                result = -2;
                goto done;
            }
        }
        reader.mesh.cuts = calloc(reader.mesh.cut_count, sizeof(*reader.mesh.cuts));
        if (reader.mesh.cuts == NULL)
            return -3;
        reader.mesh.cameras = calloc(reader.mesh.cut_count * 2,
                                     sizeof(*reader.mesh.cameras));
        if (reader.mesh.cameras == NULL) {
            result = -3;
            goto done;
        }
        if (total_cut_areas != 0) {
            reader.mesh.cut_areas = calloc(total_cut_areas, sizeof(*reader.mesh.cut_areas));
            if (reader.mesh.cut_areas == NULL) {
                result = -3;
                goto done;
            }
        }
        reader.mesh.cut_area_count = total_cut_areas;
        {
            uint32_t area_index = 0;
            for (cut = 0; cut < reader.mesh.cut_count; ++cut) {
                const unsigned char *cut_record = reader.bytes + cut_offset + cut * 0x2a8;
                uint32_t area_offset = u32(cut_record + 4);
                unsigned int area_count = cut_record[3];
                RecvxRoomCut *destination = &reader.mesh.cuts[cut];
                unsigned int area;
                destination->active = cut_record[0] & 1;
                destination->area_first = area_index;
                destination->area_count = area_count;
                for (area = 0; area < area_count; ++area) {
                    const unsigned char *source = reader.bytes + area_offset + area * 0x20;
                    RecvxCutArea *entry = &reader.mesh.cut_areas[area_index++];
                    entry->attr = u32(source);
                    entry->floor = (int8_t)source[4];
                    entry->type = source[7];
                    entry->min_x = f32(source + 0x10);
                    entry->min_z = f32(source + 0x14);
                    entry->max_x = f32(source + 0x18);
                    entry->max_z = f32(source + 0x1c);
                    if (entry->type > 4 || !coordinate(entry->min_x) ||
                        !coordinate(entry->min_z) || !coordinate(entry->max_x) ||
                        !coordinate(entry->max_z) || entry->min_x > entry->max_x ||
                        entry->min_z > entry->max_z) {
                        result = -1;
                        goto done;
                    }
                }
                for (camera = 0; camera < 2; ++camera) {
                    const unsigned char *source = cut_record + 0x20 + camera * 0xc4;
                    RecvxRoomCamera *camera_data =
                        &reader.mesh.cameras[cut * 2 + camera];
                    unsigned int axis;
                    camera_data->flags = u16(source);
                    camera_data->light_clip = source[2];
                    camera_data->speed = source[3];
                    for (axis = 0; axis < 3; ++axis) {
                        camera_data->position[axis] = f32(source + 4 + axis * 4);
                        camera_data->angles[axis] = (int32_t)u32(source + 0x34 + axis * 4);
                        if (!coordinate(camera_data->position[axis])) {
                            result = -1;
                            goto done;
                        }
                    }
                    camera_data->lens = f32(source + 0x10);
                    camera_data->width = f32(source + 0x14);
                    camera_data->height = f32(source + 0x18);
                    camera_data->depth = f32(source + 0x1c);
                    for (axis = 0; axis < 4; ++axis) {
                        camera_data->active_y[axis] = f32(source + 0x20 + axis * 4);
                        camera_data->roll_limits[axis] =
                            (int16_t)u16(source + 0x48 + axis * 2);
                        if (!coordinate(camera_data->active_y[axis])) {
                            result = -1;
                            goto done;
                        }
                    }
                    camera_data->angle_speed = f32(source + 0x30);
                    camera_data->angle_limits[0] = (int32_t)u32(source + 0x40);
                    camera_data->angle_limits[1] = (int32_t)u32(source + 0x44);
                    camera_data->angle_speed8 = source[0x50];
                    camera_data->filter_number = source[0x51];
                    camera_data->filter_rate = source[0x52];
                    camera_data->reserved = source[0x53];
                    camera_data->perspective = u32(source + 0x54);
                    camera_data->cut_center_x = f32(cut_record + 0x08);
                    camera_data->cut_center_z = f32(cut_record + 0x10);
                    camera_data->cut_width = f32(cut_record + 0x14);
                    camera_data->cut_depth = f32(cut_record + 0x1c);
                    if (!coordinate(camera_data->lens) ||
                        !coordinate(camera_data->width) ||
                        !coordinate(camera_data->height) ||
                        !coordinate(camera_data->depth) ||
                        !coordinate(camera_data->angle_speed) ||
                        !coordinate(camera_data->cut_center_x) ||
                        !coordinate(camera_data->cut_center_z) ||
                        !coordinate(camera_data->cut_width) ||
                        !coordinate(camera_data->cut_depth)) {
                        result = -1;
                        goto done;
                    }
                    for (word = 0; word < 16; ++word)
                        destination->hide_objects[camera][word] =
                            u32(source + 0x58 + word * 4);
                    for (word = 0; word < 8; ++word)
                        destination->hide_lights[camera][word] =
                            u32(source + 0x98 + word * 4);
                }
            }
        }
    }
    reader.mesh.light_count = info.counts[RECVX_ROOM_COUNT_LIGHT];
    if (reader.mesh.light_count > MAX_ROOM_LIGHTS) {
        result = -2;
        goto done;
    }
    if (reader.mesh.light_count != 0) {
        uint32_t light_offset = u32(reader.bytes + info.rom_offset + 4);
        reader.mesh.lights = calloc(reader.mesh.light_count, sizeof(*reader.mesh.lights));
        if (reader.mesh.lights == NULL) {
            result = -3;
            goto done;
        }
        for (light = 0; light < reader.mesh.light_count; ++light) {
            const unsigned char *source = reader.bytes + light_offset + light * 0xe0;
            RecvxRoomLight *destination = &reader.mesh.lights[light];
            destination->flags = u32(source);
            destination->type = u32(source + 4);
            destination->source = u32(source + 0x18);
            for (axis = 0; axis < 3; ++axis) {
                destination->position[axis] = f32(source + 0x1c + axis * 4);
                destination->direction[axis] = f32(source + 0x34 + axis * 4);
                destination->color[axis] = f32(source + 0x4c + axis * 4);
                if (!coordinate(destination->position[axis]) ||
                    !coordinate(destination->direction[axis]) ||
                    !isfinite(destination->color[axis]) ||
                    fabsf(destination->color[axis]) > 64.0f) {
                    result = -1;
                    goto done;
                }
            }
            destination->near_range = f32(source + 0x58);
            destination->far_range = f32(source + 0x5c);
            if (!isfinite(destination->near_range) || !isfinite(destination->far_range) ||
                destination->near_range < 0 || destination->far_range < 0) {
                result = -1;
                goto done;
            }
        }
    }
    reader.mesh.ambient[0] = 0.3f;
    reader.mesh.ambient[1] = 0.3f;
    reader.mesh.ambient[2] = 0.3f;
    ambient_index = reader.bytes[info.rom_offset +
                                 offsetof(RecvxRoomDiskWork, ambient_type)];
    if (ambient_index < 4) {
        const size_t offsets[3] = {
            offsetof(RecvxRoomDiskWork, ambient_red),
            offsetof(RecvxRoomDiskWork, ambient_green),
            offsetof(RecvxRoomDiskWork, ambient_blue)
        };
        for (axis = 0; axis < 3; ++axis) {
            float value = f32(reader.bytes + info.rom_offset + offsets[axis] +
                               ambient_index * sizeof(float));
            if (isfinite(value) && fabsf(value) <= 16.0f)
                reader.mesh.ambient[axis] = value;
        }
    }
    directory = u32(reader.bytes +
                     offsetof(RecvxRoomDiskHeaderPrefix, model_directory_offset));
    if (directory == 0) {
        recvxMeshFree(mesh);
        *mesh = reader.mesh;
        memset(&reader.mesh, 0, sizeof(reader.mesh));
        return 0;
    }
    if (!range(&reader, directory, 4)) {
        result = -1;
        goto done;
    }
    blob = u32(reader.bytes + directory);
    if (!range(&reader, blob, sizeof(RecvxPlayerModelDiskHeader)) ||
        memcmp(reader.bytes + blob, "MDL", 3) != 0) {
        result = -1;
        goto done;
    }
    if (!(reader.bytes[blob + offsetof(RecvxPlayerModelDiskHeader, flags)] & 0x80)) {
        result = -4;
        goto done;
    }
    reader.body = blob + u16(reader.bytes + blob +
        offsetof(RecvxPlayerModelDiskHeader, header_size));
    reader.object_count = u16(reader.bytes + blob +
        offsetof(RecvxPlayerModelDiskHeader, object_count));
    if (reader.body - blob < sizeof(RecvxPlayerModelDiskHeader) ||
        reader.body > size || reader.object_count == 0) {
        result = -1;
        goto done;
    }
    if (reader.object_count > MAX_OBJECTS) {
        result = -2;
        goto done;
    }
    object_offset = u32(reader.bytes + blob +
        offsetof(RecvxPlayerModelDiskHeader, object_offset));
    if (relative(&reader, object_offset,
                 reader.object_count * CHUNK_OBJECT_SIZE, &reader.objects) != 0) {
        result = -1;
        goto done;
    }
    reader.visited = calloc(reader.object_count, 1);
    if (reader.visited == NULL) {
        result = -3;
        goto done;
    }
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
                (sizeof(RecvxMeshVertex) + sizeof(uint32_t)) +
                reader.mesh.batch_count * sizeof(RecvxMeshBatch);
            reader.mesh.storage = malloc(reader.mesh.storage_bytes + 63);
            if (reader.mesh.storage == NULL) {
                result = -3;
                goto done;
            }
            reader.mesh.vertices = (RecvxMeshVertex *)
                (((uintptr_t)reader.mesh.storage + 63) & ~(uintptr_t)63);
            reader.mesh.material_colors = (uint32_t *)
                ((unsigned char *)reader.mesh.vertices + reader.mesh.vertex_count * sizeof(*reader.mesh.vertices));
            reader.mesh.textured_vertices = reader.mesh.vertices;
            reader.mesh.batches = (RecvxMeshBatch *)
                ((unsigned char *)reader.mesh.material_colors +
                 reader.mesh.vertex_count * sizeof(*reader.mesh.material_colors));
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

static int cut_area_contains(const RecvxCutArea *area, float x, float z, int floor)
{
    float x0, z0, x1, z1, delta_x, offset;
    if (area->floor != floor)
        return 0;
    if (area->type == 0)
        return area->min_x <= x && area->max_x > x &&
               area->min_z <= z && area->max_z > z;
    if (x < area->min_x || x > area->max_x ||
        z < area->min_z || z > area->max_z)
        return 0;
    switch (area->type) {
    case 1:
        x0 = area->max_x; z0 = area->max_z;
        x1 = area->min_x; z1 = area->min_z;
        break;
    case 2:
        x0 = area->min_x; z0 = area->max_z;
        x1 = area->max_x; z1 = area->min_z;
        break;
    case 3:
        x0 = area->min_x; z0 = area->min_z;
        x1 = area->max_x; z1 = area->max_z;
        break;
    case 4:
        x0 = area->max_x; z0 = area->min_z;
        x1 = area->min_x; z1 = area->max_z;
        break;
    default:
        return 0;
    }
    delta_x = x1 - x0;
    if (delta_x == 0.0f)
        return 0;
    offset = fabsf((z1 - z0) * (x - x0) / delta_x);
    return fabsf(z1 - z) > offset;
}

static int cut_at_position(const RecvxRoomMesh *mesh, float x, float z, int floor)
{
    uint32_t cut;
    if (mesh == NULL || mesh->cuts == NULL || mesh->cut_areas == NULL)
        return -1;
    for (cut = 0; cut < mesh->cut_count; ++cut) {
        const RecvxRoomCut *room_cut = &mesh->cuts[cut];
        uint32_t i;
        if (!room_cut->active || room_cut->area_first > mesh->cut_area_count ||
            room_cut->area_count > mesh->cut_area_count - room_cut->area_first)
            continue;
        for (i = 0; i < room_cut->area_count; ++i)
            if (cut_area_contains(&mesh->cut_areas[room_cut->area_first + i],
                                  x, z, floor))
                return (int)cut;
    }
    return -1;
}

int recvxRoomCutForPosition(const RecvxRoomMesh *mesh, float x, float z,
                            int floor, int current_cut)
{
    int selected = cut_at_position(mesh, x, z, floor);
    if (mesh == NULL || mesh->cut_count == 0)
        return -1;
    if (selected < 0)
        return current_cut >= 0 && (uint32_t)current_cut < mesh->cut_count ? current_cut : 0;
    if (current_cut < 0 || (uint32_t)current_cut >= mesh->cut_count ||
        selected == current_cut)
        return selected;
    if (cut_at_position(mesh, x, z - 1.0f, floor) == current_cut ||
        cut_at_position(mesh, x, z + 1.0f, floor) == current_cut ||
        cut_at_position(mesh, x - 1.0f, z, floor) == current_cut ||
        cut_at_position(mesh, x + 1.0f, z, floor) == current_cut)
        return current_cut;
    return selected;
}

uint32_t recvxRoomCutAttributeForPosition(const RecvxRoomMesh *mesh, uint32_t cut,
                                         float x, float z, int floor)
{
    const RecvxRoomCut *room_cut;
    uint32_t i;
    if (mesh == NULL || mesh->cuts == NULL || mesh->cut_areas == NULL ||
        cut >= mesh->cut_count)
        return 0;
    room_cut = &mesh->cuts[cut];
    if (room_cut->area_first > mesh->cut_area_count ||
        room_cut->area_count > mesh->cut_area_count - room_cut->area_first)
        return 0;
    for (i = 0; i < room_cut->area_count; ++i) {
        const RecvxCutArea *area = &mesh->cut_areas[room_cut->area_first + i];
        if (cut_area_contains(area, x, z, floor))
            return area->attr;
    }
    return 0;
}

uint32_t recvxRoomCameraForPosition(const RecvxRoomMesh *mesh, uint32_t cut,
                                    float x, float z, int floor)
{
    return (recvxRoomCutAttributeForPosition(mesh, cut, x, z, floor) & 0x8000u) != 0;
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

static unsigned int byte_from_light(float value)
{
    if (value <= 0)
        return 0;
    if (value >= 1)
        return 255;
    return (unsigned int)(value * 255.0f + 0.5f);
}

static float diffuse_amount(float dot, uint32_t blend_state)
{
    return (blend_state & RECVX_BLEND_DOUBLE_SIDED) ? fabsf(dot) : fmaxf(0, dot);
}

int recvxApplyRoomLighting(RecvxRoomMesh *mesh, uint32_t cut, uint32_t camera)
{
    const RecvxRoomLight *directional = NULL;
    const RecvxRoomLight *points[3] = {NULL, NULL, NULL};
    const RecvxRoomCut *room_cut = NULL;
    unsigned int point_count = 0, i, axis, triangle, batch_index = 0;
    if (mesh == NULL || camera > 1 ||
        (mesh->vertex_count && (mesh->vertices == NULL || mesh->material_colors == NULL)))
        return -1;
    if (cut < mesh->cut_count && mesh->cuts != NULL)
        room_cut = &mesh->cuts[cut];
    if (mesh->light_count && mesh->lights == NULL)
        return -1;
    for (i = 4; i < mesh->light_count; ++i) {
        const RecvxRoomLight *light = &mesh->lights[i];
        int hidden = room_cut != NULL && i < 256 &&
            (room_cut->hide_lights[camera][i / 32] & (0x80000000u >> (i & 31)));
        if (hidden || (light->flags & 3) != 3)
            continue;
        if (light->source == 2 && directional == NULL)
            directional = light;
        else if (light->source == 4 && point_count < 3)
            points[point_count++] = light;
    }
    for (triangle = 0; triangle + 2 < mesh->vertex_count; triangle += 3) {
        RecvxMeshVertex *vertices = &mesh->vertices[triangle];
        float center[3] = {
            (vertices[0].x + vertices[1].x + vertices[2].x) / 3.0f,
            (vertices[0].y + vertices[1].y + vertices[2].y) / 3.0f,
            (vertices[0].z + vertices[1].z + vertices[2].z) / 3.0f
        };
        float normal[3] = {vertices[0].nx, vertices[0].ny, vertices[0].nz};
        float intensity[3] = {mesh->ambient[0], mesh->ambient[1], mesh->ambient[2]};
        uint32_t material = mesh->material_colors[triangle];
        uint32_t blend_state = 0;
        unsigned int channel;
        while (batch_index + 1 < mesh->batch_count &&
               triangle >= mesh->batches[batch_index].first_vertex +
                           mesh->batches[batch_index].vertex_count)
            ++batch_index;
        if (mesh->batch_count)
            blend_state = mesh->batches[batch_index].blend_state;
        if (directional != NULL) {
            float direction[3] = {-directional->direction[0], -directional->direction[1],
                                  -directional->direction[2]};
            float length = sqrtf(direction[0]*direction[0] + direction[1]*direction[1] +
                                 direction[2]*direction[2]);
            if (length > 0) {
                float amount;
                for (axis = 0; axis < 3; ++axis)
                    direction[axis] /= length;
                amount = diffuse_amount(normal[0]*direction[0] + normal[1]*direction[1] +
                                        normal[2]*direction[2], blend_state);
                for (channel = 0; channel < 3; ++channel)
                    intensity[channel] += directional->color[channel] * amount;
            }
        }
        for (i = 0; i < point_count; ++i) {
            const RecvxRoomLight *light = points[i];
            float direction[3] = {light->position[0] - center[0],
                                  light->position[1] - center[1],
                                  light->position[2] - center[2]};
            float length = sqrtf(direction[0]*direction[0] + direction[1]*direction[1] +
                                 direction[2]*direction[2]);
            float attenuation;
            if (length <= 0 || light->far_range <= 0)
                continue;
            if (length <= light->near_range)
                attenuation = 1;
            else if (length >= light->far_range)
                attenuation = 0;
            else if (light->far_range <= light->near_range)
                attenuation = 0;
            else
                attenuation = (light->far_range - length) /
                    (light->far_range - light->near_range);
            if (attenuation <= 0)
                continue;
            for (axis = 0; axis < 3; ++axis)
                direction[axis] /= length;
            float amount = diffuse_amount(normal[0]*direction[0] + normal[1]*direction[1] +
                                          normal[2]*direction[2], blend_state) * attenuation;
            for (channel = 0; channel < 3; ++channel)
                intensity[channel] += light->color[channel] * amount;
        }
        uint32_t color = material & 0xff000000u;
        for (channel = 0; channel < 3; ++channel) {
            unsigned int shift = channel * 8;
            unsigned int base = (material >> shift) & 255;
            color |= byte_from_light((base / 255.0f) * intensity[channel]) << shift;
        }
        vertices[0].color = color;
        vertices[1].color = color;
        vertices[2].color = color;
    }
    return 0;
}
