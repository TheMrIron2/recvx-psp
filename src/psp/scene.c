#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "psp/room.h"
#include "psp/scene.h"

_Static_assert(sizeof(RecvxSceneVertex) == 16, "GU vertex layout must be 16 bytes");
_Static_assert(offsetof(RecvxSceneVertex, x) == 4, "GU position follows color");

typedef struct SceneBuilder {
    RecvxScene scene;
    float minimum[3];
    float maximum[3];
} SceneBuilder;

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float read_float(const unsigned char *p)
{
    uint32_t bits = read_u32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int valid_coordinate(float value)
{
    return isfinite(value) && fabsf(value) <= 1000000.0f;
}

static void vertex(SceneBuilder *builder, uint32_t color, const float point[3])
{
    RecvxSceneVertex *v = &builder->scene.vertices[builder->scene.vertex_count++];
    unsigned int axis;
    v->color = color;
    v->x = point[0];
    v->y = point[1];
    v->z = point[2];
    for (axis = 0; axis < 3; ++axis) {
        builder->minimum[axis] = fminf(builder->minimum[axis], point[axis]);
        builder->maximum[axis] = fmaxf(builder->maximum[axis], point[axis]);
    }
}

static int box(SceneBuilder *builder, const unsigned char *record, uint32_t color)
{
    static const unsigned char edges[12][2] = {
        {0,1}, {1,3}, {3,2}, {2,0}, {4,5}, {5,7},
        {7,6}, {6,4}, {0,4}, {1,5}, {2,6}, {3,7}
    };
    float start[3], end[3], points[8][3];
    unsigned int axis, corner, edge;
    if (!(record[0] & 1))
        return 0;
    for (axis = 0; axis < 3; ++axis) {
        float extent = read_float(record + 20 + axis * 4);
        start[axis] = read_float(record + 8 + axis * 4);
        end[axis] = start[axis] + extent;
        if (!valid_coordinate(start[axis]) || !valid_coordinate(extent) ||
            !valid_coordinate(end[axis]))
            return -1;
    }
    for (corner = 0; corner < 8; ++corner)
        for (axis = 0; axis < 3; ++axis)
            points[corner][axis] = corner & (1u << axis) ? end[axis] : start[axis];
    for (edge = 0; edge < 12; ++edge) {
        vertex(builder, color, points[edges[edge][0]]);
        vertex(builder, color, points[edges[edge][1]]);
    }
    ++builder->scene.box_count;
    return 0;
}

static int marker(SceneBuilder *builder, const unsigned char *position, uint32_t color)
{
    float point[3];
    unsigned int axis;
    for (axis = 0; axis < 3; ++axis) {
        point[axis] = read_float(position + axis * 4);
        if (!valid_coordinate(point[axis]))
            return -1;
    }
    for (axis = 0; axis < 3; ++axis) {
        float original = point[axis];
        point[axis] = original - 1.25f;
        vertex(builder, color, point);
        point[axis] = original + 1.25f;
        vertex(builder, color, point);
        point[axis] = original;
    }
    ++builder->scene.marker_count;
    return 0;
}

void recvxSceneFree(RecvxScene *scene)
{
    if (scene != NULL) {
        free(scene->storage);
        memset(scene, 0, sizeof(*scene));
    }
}

int recvxBuildRoomScene(const void *room, size_t size, RecvxScene *scene)
{
    static const struct {
        unsigned int slot, count_index;
        uint32_t color;
    } boxes[] = {{6, 6, 0xffa0a0a0}, {7, 7, 0xffd0b060}, {8, 8, 0xff60d060}};
    static const struct {
        unsigned int slot, count_index, stride, position_offset, first;
        uint32_t color;
        int check_flags;
    } markers[] = {
        {0, 0, 0x2a8, 0x24, 0, 0xff60c0ff, 0}, /* Camera cuts: first camera */
        {1, 1, 0xe0,  0x14, 0, 0xff60ffff, 1}, /* Lights */
        {2, 2, 0x24,  0x0c, 0, 0xff6060ff, 1}, /* Enemy entries */
        {3, 3, 0x24,  0x0c, 4, 0xffffa060, 1}, /* Objects after reserved slots */
        {4, 4, 0x24,  0x0c, 0, 0xffffff60, 1}, /* Items */
        {9, 9, 0x10,  0,    0, 0xffffffff, 0}  /* Room positions */
    };
    RecvxRoomInfo info;
    const unsigned char *bytes = room;
    SceneBuilder builder = {0};
    uint64_t capacity = 90; /* Grid: 84 vertices; axes: 6 */
    unsigned int group, i, axis;
    float diagonal_squared = 0;

    if (scene == NULL || recvxInspectRoom(room, size, &info) != 0)
        return -1;
    for (group = 0; group < sizeof(boxes)/sizeof(boxes[0]); ++group)
        capacity += (uint64_t)info.counts[boxes[group].count_index] * 24;
    for (group = 0; group < sizeof(markers)/sizeof(markers[0]); ++group)
        capacity += (uint64_t)info.counts[markers[group].count_index] * 6;
    if (capacity > 32768)
        return -2;
    builder.scene.storage = malloc((size_t)capacity * sizeof(RecvxSceneVertex) + 15);
    if (builder.scene.storage == NULL)
        return -3;
    builder.scene.vertices = (RecvxSceneVertex *)
        (((uintptr_t)builder.scene.storage + 15) & ~(uintptr_t)15);
    for (axis = 0; axis < 3; ++axis) {
        builder.minimum[axis] = INFINITY;
        builder.maximum[axis] = -INFINITY;
    }

    for (group = 0; group < sizeof(boxes)/sizeof(boxes[0]); ++group) {
        uint32_t offset = read_u32(bytes + info.rom_offset + boxes[group].slot * 4);
        for (i = 0; i < info.counts[boxes[group].count_index]; ++i)
            if (box(&builder, bytes + offset + i * 0x24, boxes[group].color) != 0)
                goto invalid;
    }
    for (group = 0; group < sizeof(markers)/sizeof(markers[0]); ++group) {
        uint32_t offset = read_u32(bytes + info.rom_offset + markers[group].slot * 4);
        for (i = markers[group].first; i < info.counts[markers[group].count_index]; ++i) {
            const unsigned char *record = bytes + offset + i * markers[group].stride;
            if (markers[group].check_flags && read_u32(record) == 0)
                continue;
            if (marker(&builder, record + markers[group].position_offset, markers[group].color) != 0)
                goto invalid;
        }
    }
    if (builder.scene.vertex_count == 0) {
        builder.scene.radius = 10;
        for (axis = 0; axis < 3; ++axis)
            builder.minimum[axis] = builder.maximum[axis] = 0;
    } else {
        for (axis = 0; axis < 3; ++axis) {
            float difference = builder.maximum[axis] - builder.minimum[axis];
            builder.scene.center[axis] = (builder.maximum[axis] + builder.minimum[axis]) * 0.5f;
            diagonal_squared += difference * difference;
        }
        builder.scene.radius = fmaxf(1.0f, sqrtf(diagonal_squared) * 0.5f);
    }
    /* Add a ground grid and XYZ axes after fitting the view to actual data. */
    for (i = 0; i <= 20; ++i) {
        float radius = builder.scene.radius;
        float x = builder.scene.center[0], z = builder.scene.center[2];
        float coordinate = ((int)i - 10) * radius * 0.1f;
        float a[3] = {x + coordinate, builder.minimum[1], z - radius};
        float b[3] = {x + coordinate, builder.minimum[1], z + radius};
        float c[3] = {x - radius, builder.minimum[1], z + coordinate};
        float d[3] = {x + radius, builder.minimum[1], z + coordinate};
        vertex(&builder, 0xff383028, a);
        vertex(&builder, 0xff383028, b);
        vertex(&builder, 0xff383028, c);
        vertex(&builder, 0xff383028, d);
    }
    for (axis = 0; axis < 3; ++axis) {
        static const uint32_t colors[3] = {0xff6060ff, 0xff60ff60, 0xffff6060};
        float point[3];
        memcpy(point, builder.scene.center, sizeof(point));
        vertex(&builder, colors[axis], point);
        point[axis] += builder.scene.radius * 0.2f;
        vertex(&builder, colors[axis], point);
    }
    recvxSceneFree(scene);
    *scene = builder.scene;
    return 0;

invalid:
    recvxSceneFree(&builder.scene);
    return -1;
}
