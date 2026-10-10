// PSP GU renderer for room geometry
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <pspmodulemgr.h>
#include <pspthreadman.h>
#include <pspsysmem.h>

#include "psp/renderer.h"

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define BUFFER_WIDTH 512
#define FRAME_BYTES (BUFFER_WIDTH * SCREEN_HEIGHT * 4)

static unsigned int command_list[16384] __attribute__((aligned(64)));
static void *draw_buffer;
static ScePspFVector3 center, focus;
static float yaw, pitch, distance, radius;
static uint64_t previous_time;
static uint64_t frame_elapsed_us, cpu_submit_us, ge_wait_us;
static size_t command_list_bytes, command_list_high_water;
static size_t module_text_bytes, module_data_bytes, module_bss_bytes;
static size_t player_source_bytes;
static int initialized;
static int display_mode;
static uint32_t camera_cut;
static uint32_t camera_variant;
static int player_mode;
static const RecvxRoomMesh *player_model_mesh;
static RecvxRoomMesh *player_actor_mesh;
static const RecvxPlayerAssets *player_actor_assets;
static unsigned int player_animation_slot;
static float player_animation_frame;
static int fixed_camera_active;
static ScePspFVector3 fixed_eye_offset, fixed_camera_forward, fixed_camera_up;
static float fixed_camera_fov;

typedef struct PlayerVertex {
    uint32_t color;
    float x, y, z;
} PlayerVertex;

static PlayerVertex player_vertices[256] __attribute__((aligned(16)));
static unsigned int player_vertex_count;
static float player_sine, player_cosine;

static void update_command_list_usage(void)
{
    int used = sceGuCheckList();
    if (used < 0)
        return;
    command_list_bytes = (size_t)used + 8;
    if (command_list_bytes > command_list_high_water)
        command_list_high_water = command_list_bytes;
}

static void read_module_footprint(void)
{
    SceKernelModuleInfo info;
    SceUID module_id = sceKernelGetModuleIdByAddress(command_list);
    if (module_id < 0)
        return;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (sceKernelQueryModuleInfo(module_id, &info) == 0) {
        module_text_bytes = info.text_size;
        module_data_bytes = info.data_size;
        module_bss_bytes = info.bss_size;
    }
}

static int set_wireframe(RecvxRoomMesh *mesh, int enabled)
{
    int result;
    if (mesh == NULL)
        return -1;
    result = recvxMeshSetWireframe(mesh, enabled);
    if (result == 0 && enabled)
        sceKernelDcacheWritebackRange(mesh->wire_vertices,
                                     mesh->triangle_count * 6 * sizeof(*mesh->wire_vertices));
    return result;
}

static void draw_memory_hud(const RecvxScene *scene, const RecvxRoomMesh *mesh,
                            const RecvxTextureSet *textures,
                            const RecvxPlayerRoom *player_room)
{
    SceSize free_bytes = sceKernelTotalFreeMemSize();
    SceSize largest_block = sceKernelMaxFreeMemSize();
    int stack_free = sceKernelGetThreadStackFreeSize(0);
    size_t mesh_bytes = mesh != NULL ? mesh->storage_bytes : 0;
    size_t texture_bytes = textures != NULL ? textures->storage_bytes : 0;
    size_t scene_bytes = scene != NULL ?
        (size_t)scene->vertex_count * sizeof(*scene->vertices) : 0;
    size_t wall_bytes = player_room != NULL ?
        (size_t)player_room->wall_count * sizeof(*player_room->walls) : 0;
    size_t door_bytes = player_room != NULL ?
        (size_t)player_room->door_count * sizeof(*player_room->doors) : 0;
    size_t wire_capacity = mesh != NULL ?
        (size_t)mesh->triangle_count * 6 * sizeof(RecvxSceneVertex) : 0;
    size_t wire_bytes = mesh != NULL && mesh->wire_vertices != NULL ? wire_capacity : 0;
    size_t wire_saved = wire_capacity - wire_bytes;
    size_t player_mesh_bytes = player_model_mesh != NULL ?
        player_model_mesh->storage_bytes : 0;

    pspDebugScreenSetXY(1, 4);
    pspDebugScreenPrintf("Heap %uK  Largest %uK  Stack %uK",
                         (unsigned int)(free_bytes / 1024),
                         (unsigned int)(largest_block / 1024),
                         stack_free > 0 ? (unsigned int)stack_free / 1024 : 0);
    pspDebugScreenSetXY(1, 5);
    pspDebugScreenPrintf("Scene %uK Mesh %uK Tex %uK Wire %uK Avoid %uK",
                         (unsigned int)(scene_bytes / 1024),
                         (unsigned int)(mesh_bytes / 1024),
                         (unsigned int)(texture_bytes / 1024),
                         (unsigned int)(wire_bytes / 1024),
                         (unsigned int)(wire_saved / 1024));
    pspDebugScreenSetXY(1, 6);
    pspDebugScreenPrintf("Player data %uK mesh %uK walls %uK doors %uK",
                         (unsigned int)(player_source_bytes / 1024),
                         (unsigned int)(player_mesh_bytes / 1024),
                         (unsigned int)(wall_bytes / 1024),
                         (unsigned int)(door_bytes / 1024));
    pspDebugScreenSetXY(1, 7);
    pspDebugScreenPrintf("Frame %u.%02u CPU+submit %u.%02u GE wait %u.%02u ms",
                         (unsigned int)(frame_elapsed_us / 1000),
                         (unsigned int)((frame_elapsed_us / 10) % 100),
                         (unsigned int)(cpu_submit_us / 1000),
                         (unsigned int)((cpu_submit_us / 10) % 100),
                         (unsigned int)(ge_wait_us / 1000),
                         (unsigned int)((ge_wait_us / 10) % 100));
    pspDebugScreenSetXY(1, 8);
    pspDebugScreenPrintf("ELF T/D/B %u/%u/%uK  GU %u/%uK max %uK",
                         (unsigned int)(module_text_bytes / 1024),
                         (unsigned int)(module_data_bytes / 1024),
                         (unsigned int)(module_bss_bytes / 1024),
                         (unsigned int)(command_list_bytes / 1024),
                         (unsigned int)(sizeof(command_list) / 1024),
                         (unsigned int)(command_list_high_water / 1024));
}

static int object_hidden(const RecvxRoomMesh *mesh, uint32_t object_index)
{
    const uint32_t *mask;
    if (mesh == NULL || mesh->cuts == NULL || camera_cut >= mesh->cut_count || object_index >= 512)
        return 0;
    mask = mesh->cuts[camera_cut].hide_objects[camera_variant];
    return (mask[object_index / 32] & (0x80000000u >> (object_index & 31))) != 0;
}

static unsigned int hidden_object_count(const RecvxRoomMesh *mesh)
{
    unsigned int count = 0, i;
    if (mesh != NULL)
        for (i = 0; i < mesh->object_count; ++i)
            count += object_hidden(mesh, i);
    return count;
}

static void apply_room_lighting(const RecvxRoomMesh *mesh)
{
    RecvxRoomMesh *mutable_mesh = (RecvxRoomMesh *)mesh;
    if (recvxApplyRoomLighting(mutable_mesh, camera_cut, camera_variant) == 0 &&
        mesh != NULL && mesh->vertices != NULL)
        sceKernelDcacheWritebackRange(mesh->vertices, mesh->storage_bytes);
}

static void rotate_camera_vector(ScePspFVector3 *vector, unsigned int axis,
                                 int32_t angle)
{
    float radians = ((uint32_t)angle & 0xffffu) *
                    (6.283185307179586f / 65536.0f);
    float sine = sinf(radians), cosine = cosf(radians);
    float x = vector->x, y = vector->y, z = vector->z;
    if (axis == 0) {
        vector->y = cosine * y - sine * z;
        vector->z = sine * y + cosine * z;
    } else if (axis == 1) {
        vector->x = cosine * x + sine * z;
        vector->z = -sine * x + cosine * z;
    } else {
        vector->x = cosine * x - sine * y;
        vector->y = sine * x + cosine * y;
    }
}

static int32_t signed_camera_angle(int32_t angle)
{
    return (int16_t)((uint32_t)angle & 0xffffu);
}

static int32_t camera_angle_delta(int32_t target, int32_t source)
{
    return (int16_t)((uint16_t)target - (uint16_t)source);
}

static int32_t limited_camera_angle(int32_t base, int32_t target, int32_t limit)
{
    int32_t delta = camera_angle_delta(target, base);
    if (limit < 0)
        limit = -limit;
    if (delta < -limit)
        delta = -limit;
    else if (delta > limit)
        delta = limit;
    return (uint16_t)(base + delta);
}

static void fixed_camera_base_position(const RecvxRoomMesh *mesh,
                                       const RecvxPlayer *player,
                                       uint32_t attr,
                                       ScePspFVector3 *position)
{
    const RecvxRoomCamera *camera =
        &mesh->cameras[camera_cut * 2 + camera_variant];
    float x_delta = player->x - camera->cut_center_x;
    float z_delta = player->z - camera->cut_center_z;
    position->x = camera->position[0];
    position->y = camera->position[1];
    position->z = camera->position[2];
    if (camera->width != 0.0f && camera->cut_width != 0.0f && !(attr & 0x20u))
        position->x += camera->width * 0.01f * (x_delta - camera->cut_width * 0.5f);
    if (camera->depth != 0.0f && camera->cut_depth != 0.0f && !(attr & 0x8u))
        position->z += camera->depth * 0.01f * (z_delta - camera->cut_depth * 0.5f);
    if ((camera->active_y[0] != 0.0f || camera->active_y[1] != 0.0f ||
         camera->active_y[2] != 0.0f || camera->active_y[3] != 0.0f) &&
        camera->cut_width != 0.0f && camera->cut_depth != 0.0f && !(attr & 0x10u)) {
        float y0 = camera->active_y[0] + z_delta *
                   ((camera->active_y[2] - camera->active_y[0]) / camera->cut_depth);
        float y1 = camera->active_y[1] + z_delta *
                   ((camera->active_y[3] - camera->active_y[1]) / camera->cut_depth);
        position->y += y0 + x_delta * (y1 - y0) / camera->cut_width;
    }
}

static void configure_fixed_camera(const RecvxRoomMesh *mesh,
                                   const RecvxPlayer *player)
{
    const RecvxRoomCamera *camera;
    ScePspFVector3 base_position, offset = {0.0f, 0.0f, 0.0f};
    ScePspFVector3 forward = {0.0f, 0.0f, -1.0f};
    ScePspFVector3 up = {0.0f, 1.0f, 0.0f};
    int32_t angles[3];
    uint32_t attr, perspective;
    float dx, dy, dz, horizontal;
    fixed_camera_active = 0;
    fixed_camera_fov = 50.0f;
    if (mesh == NULL || player == NULL || mesh->cameras == NULL ||
        camera_cut >= mesh->cut_count)
        return;
    camera = &mesh->cameras[camera_cut * 2 + camera_variant];
    attr = recvxRoomCutAttributeForPosition(mesh, camera_cut, player->x,
                                            player->z, player->floor);
    fixed_camera_base_position(mesh, player, attr, &base_position);
    for (unsigned int axis = 0; axis < 3; ++axis)
        angles[axis] = signed_camera_angle(camera->angles[axis]);
    dx = player->x - base_position.x;
    dz = player->z - base_position.z;
    dy = player->y + camera->height - base_position.y;
    horizontal = sqrtf(dx * dx + dz * dz);
    if (!(camera->flags & 1u)) {
        if (camera->angle_limits[0] != 0 && !(attr & 0x4u)) {
            int32_t desired = (int32_t)(-atan2f(dy, horizontal) * 10430.37835f);
            angles[0] = limited_camera_angle(angles[0], desired,
                                             signed_camera_angle(camera->angle_limits[0]));
        }
        if (camera->angle_limits[1] != 0 && !(attr & 0x2u)) {
            int32_t desired = (int32_t)(-atan2f(dx, dz) * 10430.37835f) + 32768;
            angles[1] = limited_camera_angle(angles[1], desired,
                                             signed_camera_angle(camera->angle_limits[1]));
        }
    }
    if ((camera->roll_limits[0] != 0 || camera->roll_limits[1] != 0 ||
         camera->roll_limits[2] != 0 || camera->roll_limits[3] != 0) &&
        camera->cut_width != 0.0f && camera->cut_depth != 0.0f && !(attr & 0x1u)) {
        float x_delta = player->x - camera->cut_center_x;
        float z_delta = player->z - camera->cut_center_z;
        float z0 = camera->roll_limits[0] + z_delta *
                   ((camera->roll_limits[2] - camera->roll_limits[0]) / camera->cut_depth);
        float z1 = camera->roll_limits[1] + z_delta *
                   ((camera->roll_limits[3] - camera->roll_limits[1]) / camera->cut_depth);
        angles[2] = (uint16_t)(angles[2] + z0 + x_delta * (z1 - z0) / camera->cut_width);
    }
    offset.z = -camera->lens;
    rotate_camera_vector(&offset, 2, angles[2]);
    rotate_camera_vector(&offset, 0, angles[0]);
    rotate_camera_vector(&offset, 1, angles[1]);
    rotate_camera_vector(&forward, 2, -angles[2]);
    rotate_camera_vector(&forward, 0, -angles[0]);
    rotate_camera_vector(&forward, 1, -angles[1]);
    rotate_camera_vector(&up, 2, -angles[2]);
    rotate_camera_vector(&up, 0, -angles[0]);
    rotate_camera_vector(&up, 1, -angles[1]);
    fixed_eye_offset = offset;
    fixed_camera_forward = forward;
    fixed_camera_up = up;
    perspective = camera->perspective;
    if (camera->flags & 0x20u && camera->cut_width != 0.0f &&
        camera->cut_depth != 0.0f) {
        float x_delta = player->x - camera->cut_center_x;
        float z_delta = player->z - camera->cut_center_z;
        float p0 = (perspective & 0xffu) * 182.04445f;
        float p1 = ((perspective >> 8) & 0xffu) * 182.04445f;
        float p2 = ((perspective >> 16) & 0xffu) * 182.04445f;
        float p3 = ((perspective >> 24) & 0xffu) * 182.04445f;
        float a0 = p0 + z_delta * ((p2 - p0) / camera->cut_depth);
        float a1 = p1 + z_delta * ((p3 - p1) / camera->cut_depth);
        fixed_camera_fov = (a0 + x_delta * (a1 - a0) / camera->cut_width) *
                           (360.0f / 65536.0f);
    } else if ((perspective & 0xffffu) != 0) {
        fixed_camera_fov = (perspective & 0xffffu) * (360.0f / 65536.0f);
    }
    if (fixed_camera_fov < 15.0f || fixed_camera_fov > 120.0f)
        fixed_camera_fov = 50.0f;
    fixed_camera_active = 1;
}

static void fixed_camera_position(const RecvxRoomMesh *mesh,
                                  const RecvxPlayer *player,
                                  ScePspFVector3 *position)
{
    uint32_t attr = recvxRoomCutAttributeForPosition(mesh, camera_cut,
                                                    player->x, player->z,
                                                    player->floor);
    fixed_camera_base_position(mesh, player, attr, position);
    position->x += fixed_eye_offset.x;
    position->y += fixed_eye_offset.y;
    position->z += fixed_eye_offset.z;
}

static int update_player_culling(const RecvxRoomMesh *mesh, const RecvxPlayer *player,
                                 int current_cut)
{
    int selected;
    uint32_t camera;
    if (mesh == NULL || player == NULL || mesh->cut_count == 0)
        return -1;
    selected = recvxRoomCutForPosition(mesh, player->x, player->z, player->floor,
                                       current_cut);
    if (selected < 0)
        return -1;
    camera = recvxRoomCameraForPosition(mesh, (uint32_t)selected,
                                        player->x, player->z, player->floor);
    if (camera_cut == (uint32_t)selected && camera_variant == camera)
        return 0;
    camera_cut = (uint32_t)selected;
    camera_variant = camera;
    return 1;
}

static void set_batch_blending(const RecvxMeshBatch *batch)
{
    unsigned int destination = batch->blend_state & 0xff;
    if (batch->blend_state & RECVX_BLEND_ALPHA) {
        if (destination == 10)
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffff);
        else
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuEnable(GU_BLEND);
    } else {
        sceGuDisable(GU_BLEND);
    }
}

static void draw_player_model(const RecvxPlayer *player,
                              const RecvxRoomMesh *model)
{
    ScePspFVector3 position = {player->x, player->y, player->z};
    uint32_t i;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    sceGumPushMatrix();
    sceGumTranslate(&position);
    sceGumRotateY(player->yaw);
    for (i = 0; i < model->batch_count; ++i) {
        const RecvxMeshBatch *batch = &model->batches[i];
        if (batch->vertex_count == 0)
            continue;
        set_batch_blending(batch);
        sceGumDrawArray(GU_TRIANGLES,
                        GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF |
                        GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        (int)batch->vertex_count, NULL,
                        model->vertices + batch->first_vertex);
    }
    sceGuDisable(GU_BLEND);
    sceGumPopMatrix();
}

static void add_player_vertex(uint32_t color, float x, float y, float z,
                              const RecvxPlayer *player)
{
    PlayerVertex *vertex = &player_vertices[player_vertex_count++];
    vertex->color = color;
    vertex->x = player->x + x * player_cosine - z * player_sine;
    vertex->y = player->y + y;
    vertex->z = player->z - x * player_sine - z * player_cosine;
}

static void add_player_box(const RecvxPlayer *player, float cx, float cy, float cz,
                           float hx, float hy, float hz, uint32_t color)
{
    static const unsigned char faces[6][6] = {
        {0, 1, 2, 0, 2, 3}, {4, 6, 5, 4, 7, 6},
        {0, 4, 5, 0, 5, 1}, {1, 5, 6, 1, 6, 2},
        {2, 6, 7, 2, 7, 3}, {3, 7, 4, 3, 4, 0}
    };
    float points[8][3] = {
        {cx - hx, cy - hy, cz - hz}, {cx + hx, cy - hy, cz - hz},
        {cx + hx, cy + hy, cz - hz}, {cx - hx, cy + hy, cz - hz},
        {cx - hx, cy - hy, cz + hz}, {cx + hx, cy - hy, cz + hz},
        {cx + hx, cy + hy, cz + hz}, {cx - hx, cy + hy, cz + hz}
    };
    unsigned int face, i;
    for (face = 0; face < 6; ++face)
        for (i = 0; i < 6; ++i) {
            unsigned int index = faces[face][i];
            add_player_vertex(color, points[index][0], points[index][1],
                              points[index][2], player);
        }
}

static void draw_player(const RecvxPlayer *player)
{
    static const unsigned char ring[4] = {2, 4, 5, 3};
    unsigned int i;
    float swing = sinf(player->walk_phase) * 0.12f;
    float head[6][3] = {
        {0.0f, 1.98f, 0.0f}, {0.0f, 1.52f, 0.0f},
        {-0.2f, 1.75f, 0.0f}, {0.0f, 1.75f, -0.2f},
        {0.2f, 1.75f, 0.0f}, {0.0f, 1.75f, 0.2f}
    };
    player_sine = sinf(player->yaw);
    player_cosine = cosf(player->yaw);
    player_vertex_count = 0;
    add_player_box(player, 0.0f, 1.02f, 0.0f, 0.27f, 0.48f, 0.17f,
                   0xff874027);
    add_player_box(player, -0.14f, 0.3f, swing, 0.12f, 0.3f, 0.14f,
                   0xff3c3a30);
    add_player_box(player, 0.14f, 0.3f, -swing, 0.12f, 0.3f, 0.14f,
                   0xff3c3a30);
    add_player_box(player, -0.35f, 1.1f, 0.0f, 0.1f, 0.38f, 0.13f,
                   0xff874027);
    add_player_box(player, 0.35f, 1.1f, 0.0f, 0.1f, 0.38f, 0.13f,
                   0xff874027);
    for (i = 0; i < 4; ++i) {
        unsigned int next = (i + 1) & 3;
        add_player_vertex(0xffad8064, head[0][0], head[0][1], head[0][2], player);
        add_player_vertex(0xffad8064, head[ring[i]][0], head[ring[i]][1],
                          head[ring[i]][2], player);
        add_player_vertex(0xffad8064, head[ring[next]][0], head[ring[next]][1],
                          head[ring[next]][2], player);
        add_player_vertex(0xff986e55, head[1][0], head[1][1], head[1][2], player);
        add_player_vertex(0xff986e55, head[ring[next]][0], head[ring[next]][1],
                          head[ring[next]][2], player);
        add_player_vertex(0xff986e55, head[ring[i]][0], head[ring[i]][1],
                          head[ring[i]][2], player);
    }
    sceKernelDcacheWritebackRange(player_vertices,
                                  player_vertex_count * sizeof(*player_vertices));
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                    (int)player_vertex_count, NULL, player_vertices);
}

static void reset_camera(const RecvxScene *scene, const RecvxRoomMesh *mesh,
                         const RecvxPlayer *player)
{
    const float *position = display_mode != 3 && mesh && mesh->vertices ? mesh->center : scene->center;
    radius = display_mode != 3 && mesh && mesh->vertices ? mesh->radius : scene->radius;
    configure_fixed_camera(mesh, player);
    if (player != NULL && fixed_camera_active) {
        fixed_camera_position(mesh, player, &center);
        center.x += fixed_camera_forward.x;
        center.y += fixed_camera_forward.y;
        center.z += fixed_camera_forward.z;
        focus = center;
        return;
    }
    focus.x = player ? player->x : position[0];
    focus.y = player ? player->y + 0.9f : position[1];
    focus.z = player ? player->z : position[2];
    center = focus;
    yaw = player ? player->yaw : 0.85f;
    pitch = player ? 0.28f : 0.6f;
    distance = player ? fmaxf(6.0f, fminf(22.0f, radius * 0.18f)) : radius * 3.0f;
}

int recvxRendererStart(const RecvxScene *scene, RecvxRoomMesh *mesh,
                       const RecvxTextureSet *textures, const RecvxPlayer *player,
                       const RecvxPlayerAssets *actor_assets,
                       RecvxRoomMesh *actor_mesh,
                       size_t actor_source_bytes)
{
    if (scene == NULL || scene->vertices == NULL || initialized)
        return -1;
    if (sceGuInit() < 0)
        return -1;
    initialized = 1;
    read_module_footprint();
    command_list_bytes = command_list_high_water = 0;
    player_source_bytes = actor_source_bytes;
    draw_buffer = NULL;
    sceGuStart(GU_DIRECT, command_list);
    sceGuDrawBuffer(GU_PSM_8888, draw_buffer, BUFFER_WIDTH);
    sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, (void *)FRAME_BYTES, BUFFER_WIDTH);
    sceGuDepthBuffer((void *)(FRAME_BYTES * 2), BUFFER_WIDTH);
    sceGuOffset(2048 - SCREEN_WIDTH / 2, 2048 - SCREEN_HEIGHT / 2);
    sceGuViewport(2048, 2048, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuShadeModel(GU_FLAT);
    update_command_list_usage();
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceKernelDcacheWritebackRange(scene->vertices,
                                 scene->vertex_count * sizeof(*scene->vertices));
    if (mesh && mesh->vertices)
        sceKernelDcacheWritebackRange(mesh->vertices, mesh->storage_bytes);
    if (actor_mesh && actor_mesh->vertices)
        sceKernelDcacheWritebackRange(actor_mesh->vertices, actor_mesh->storage_bytes);
    if (textures)
        for (uint32_t i = 0; i < textures->count; ++i) {
            const RecvxTexture *texture = &textures->textures[i];
            sceKernelDcacheWritebackRange(texture->pixels, texture->image_bytes + 1024);
        }
    sceDisplayWaitVblankStartCB();
    sceGuDisplay(GU_TRUE);
    pspDebugScreenSetColorMode(GU_PSM_8888);
    pspDebugScreenEnableBackColor(0);
    display_mode = mesh && mesh->vertices ? (textures && textures->count ? 4 : 0) : 3;
    player_mode = player != NULL;
    player_model_mesh = actor_mesh;
    player_actor_mesh = actor_mesh;
    player_actor_assets = actor_assets;
    player_animation_slot = UINT_MAX;
    player_animation_frame = 0.0f;
    camera_cut = 0;
    camera_variant = 0;
    if (player_mode)
        update_player_culling(mesh, player, -1);
    apply_room_lighting(mesh);
    reset_camera(scene, mesh, player);
    previous_time = sceKernelGetSystemTimeWide();
    frame_elapsed_us = cpu_submit_us = ge_wait_us = 0;
    return 0;
}

void recvxRendererFrame(const RecvxScene *scene, RecvxRoomMesh *mesh, const RecvxTextureSet *textures,
                        RecvxPlayer *player, const RecvxPlayerRoom *player_room,
                        const char *room_name,
                        const SceCtrlData *pad, unsigned int pressed)
{
    uint64_t now = sceKernelGetSystemTimeWide();
    float dt = fminf(0.05f, (float)(now - previous_time) * 0.000001f);
    float analog_x = ((int)pad->Lx - 128) / 128.0f;
    float analog_y = ((int)pad->Ly - 128) / 128.0f;
    float near_plane, far_plane;
    ScePspFVector3 eye, up = {0, 1, 0};
    int lighting_changed = 0;
    frame_elapsed_us = now - previous_time;
    previous_time = now;

    if ((pressed & PSP_CTRL_SELECT) && mesh && mesh->vertices) {
        display_mode = (display_mode + 1) % (textures && textures->count ? 5 : 4);
        if (display_mode == 1) {
            if (set_wireframe(mesh, 1) != 0)
                display_mode = 0;
        } else {
            set_wireframe(mesh, 0);
        }
        reset_camera(scene, mesh, player_mode ? player : NULL);
    }
    if (!player_mode && (pressed & PSP_CTRL_CIRCLE) && mesh && mesh->cut_count) {
        camera_cut = (camera_cut + 1) % mesh->cut_count;
        lighting_changed = 1;
    }
    if (!player_mode && (pressed & PSP_CTRL_CROSS) && mesh && mesh->cut_count) {
        camera_variant ^= 1;
        lighting_changed = 1;
    }
    if (pressed & PSP_CTRL_TRIANGLE)
        reset_camera(scene, mesh, player_mode ? player : NULL);
    if (!player_mode && (pad->Buttons & PSP_CTRL_LTRIGGER))
        distance *= 1.0f + dt;
    if (!player_mode && (pad->Buttons & PSP_CTRL_RTRIGGER))
        distance *= 1.0f - dt;
    if (player_mode && player != NULL && player_room != NULL) {
        float forward = -analog_y, turn = analog_x;
        if (fabsf(forward) < 0.16f)
            forward = 0.0f;
        if (fabsf(turn) < 0.16f)
            turn = 0.0f;
        if (pad->Buttons & PSP_CTRL_UP)
            forward = 1.0f;
        else if (pad->Buttons & PSP_CTRL_DOWN)
            forward = -1.0f;
        if (pad->Buttons & PSP_CTRL_LEFT)
            turn = -1.0f;
        else if (pad->Buttons & PSP_CTRL_RIGHT)
            turn = 1.0f;
        recvxPlayerUpdate(player, player_room, forward, turn, dt);
        if (player_actor_assets != NULL && player_actor_mesh != NULL &&
            player_actor_assets->model_count != 0) {
            unsigned int motion_slot = forward != 0.0f ? 39 : 42;
            if (motion_slot < player_actor_assets->motion_count &&
                player_actor_assets->motions[motion_slot].present) {
                const RecvxPlayerMotionAsset *motion =
                    &player_actor_assets->motions[motion_slot];
                if (player_animation_slot != motion_slot) {
                    player_animation_slot = motion_slot;
                    player_animation_frame = 0.0f;
                } else {
                    player_animation_frame += dt * 30.0f;
                }
                if (player_animation_frame >= motion->frame_count)
                    player_animation_frame = fmodf(player_animation_frame,
                                                   (float)motion->frame_count);
                recvxPosePlayerMesh(&player_actor_assets->models[0], motion,
                                    (uint32_t)player_animation_frame,
                                    player_actor_mesh);
            }
        }
        if (update_player_culling(mesh, player, (int)camera_cut) > 0) {
            lighting_changed = 1;
        }
        configure_fixed_camera(mesh, player);
        if (fixed_camera_active) {
            fixed_camera_position(mesh, player, &eye);
            center.x = eye.x + fixed_camera_forward.x;
            center.y = eye.y + fixed_camera_forward.y;
            center.z = eye.z + fixed_camera_forward.z;
        } else {
            center.x = player->x;
            center.y = player->y + 0.9f;
            center.z = player->z;
            yaw = player->yaw;
        }
        focus = center;
        pitch = fmaxf(0.16f, fminf(0.58f, pitch));
    } else {
        if (pad->Buttons & PSP_CTRL_LEFT)
            yaw -= dt * 1.7f;
        if (pad->Buttons & PSP_CTRL_RIGHT)
            yaw += dt * 1.7f;
        if (pad->Buttons & PSP_CTRL_UP)
            pitch += dt;
        if (pad->Buttons & PSP_CTRL_DOWN)
            pitch -= dt;
        pitch = fmaxf(0.12f, fminf(1.4f, pitch));
        yaw = fmodf(yaw, 6.283185307f);
        if (fabsf(analog_x) < 0.16f)
            analog_x = 0;
        if (fabsf(analog_y) < 0.16f)
            analog_y = 0;
        center.x += (analog_x * cosf(yaw) + analog_y * sinf(yaw)) * radius * dt;
        center.z += (-analog_x * sinf(yaw) + analog_y * cosf(yaw)) * radius * dt;
        center.x = fmaxf(focus.x - radius * 4, fminf(focus.x + radius * 4, center.x));
        center.z = fmaxf(focus.z - radius * 4, fminf(focus.z + radius * 4, center.z));
    }
    if (lighting_changed)
        apply_room_lighting(mesh);
    if (player_mode)
        distance = fmaxf(fmaxf(4.0f, radius * 0.07f),
                         fminf(fmaxf(10.0f, radius * 0.5f), distance));
    else
        distance = fmaxf(radius * 0.2f, fminf(radius * 12, distance));
    if (player_mode && fixed_camera_active) {
        up = fixed_camera_up;
    } else {
        eye.x = center.x + sinf(yaw) * cosf(pitch) * distance;
        eye.y = center.y + sinf(pitch) * distance;
        eye.z = center.z + cosf(yaw) * cosf(pitch) * distance;
    }
    if (player_mode && fixed_camera_active) {
        near_plane = fmaxf(0.05f, radius * 0.001f);
        far_plane = fmaxf(radius * 4.0f, near_plane * 3200.0f);
    } else {
        near_plane = radius * 0.01f;
        far_plane = radius * 32.0f;
    }

    sceGuStart(GU_DIRECT, command_list);
    sceGuClearColor(0xff18120e);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(player_mode && fixed_camera_active ? fixed_camera_fov : 50.0f,
                      (float)SCREEN_WIDTH / SCREEN_HEIGHT,
                      near_plane, far_plane);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumLookAt(&eye, &center, &up);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    if (display_mode == 4 && mesh && textures) {
        sceGuTexMode(GU_PSM_T8, 0, 0, 1);
        sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuTexScale(1, 1);
        sceGuTexOffset(0, 0);
        sceGuAlphaFunc(GU_GREATER, 0, 0xff);
        sceGuEnable(GU_ALPHA_TEST);
        for (uint32_t i = 0; i < mesh->batch_count; ++i) {
            const RecvxMeshBatch *batch = &mesh->batches[i];
            if (object_hidden(mesh, batch->object_index))
                continue;
            set_batch_blending(batch);
            if (batch->texture_index == UINT32_MAX) {
                sceGuDisable(GU_TEXTURE_2D);
                sceGumDrawArray(GU_TRIANGLES,
                                GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF |
                                GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                                (int)batch->vertex_count, NULL, mesh->vertices + batch->first_vertex);
            } else {
                const RecvxTexture *texture = &textures->textures[batch->texture_index];
                sceGuEnable(GU_TEXTURE_2D);
                sceGuClutLoad(32, texture->palette);
                sceGuTexImage(0, texture->width, texture->height, texture->width, texture->pixels);
                sceGuTexWrap(batch->flags & 1 ? GU_CLAMP : GU_REPEAT,
                             batch->flags & 2 ? GU_CLAMP : GU_REPEAT);
                sceGuTexFilter(batch->flags & 4 ? GU_LINEAR : GU_NEAREST,
                               batch->flags & 4 ? GU_LINEAR : GU_NEAREST);
                sceGuTexFlush();
                sceGumDrawArray(GU_TRIANGLES,
                                GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF |
                                GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                                (int)batch->vertex_count, NULL, mesh->textured_vertices + batch->first_vertex);
            }
        }
        sceGuDisable(GU_BLEND);
        sceGuDisable(GU_ALPHA_TEST);
        sceGuDisable(GU_TEXTURE_2D);
    }
    if ((display_mode == 0 || display_mode == 2) && mesh && mesh->vertices) {
        sceGuDisable(GU_TEXTURE_2D);
        for (uint32_t i = 0; i < mesh->batch_count; ++i) {
            const RecvxMeshBatch *batch = &mesh->batches[i];
            if (!object_hidden(mesh, batch->object_index)) {
                set_batch_blending(batch);
                sceGumDrawArray(GU_TRIANGLES,
                                GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF |
                                GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                                (int)batch->vertex_count, NULL,
                                mesh->vertices + batch->first_vertex);
            }
        }
        sceGuDisable(GU_BLEND);
    }
    if (display_mode == 1 && mesh && mesh->wire_vertices) {
        sceGuDisable(GU_BLEND);
        sceGuDisable(GU_TEXTURE_2D);
        sceGuDisable(GU_DEPTH_TEST);
        for (uint32_t i = 0; i < mesh->batch_count; ++i) {
            const RecvxMeshBatch *batch = &mesh->batches[i];
            unsigned int offset, remaining;
            if (object_hidden(mesh, batch->object_index))
                continue;
            offset = batch->first_vertex * 2;
            remaining = batch->vertex_count * 2;
            while (remaining != 0) {
                unsigned int count = remaining > 32768 ? 32768 : remaining;
                sceGumDrawArray(GU_LINES,
                                GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                                (int)count, NULL, mesh->wire_vertices + offset);
                offset += count;
                remaining -= count;
            }
        }
        sceGuEnable(GU_DEPTH_TEST);
    }
    if (display_mode == 2 || display_mode == 3) {
        if (display_mode == 2)
            sceGuDisable(GU_DEPTH_TEST);
        sceGumDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        (int)scene->vertex_count, NULL, scene->vertices);
        sceGuEnable(GU_DEPTH_TEST);
    }
    if (player_mode && player != NULL) {
        if (player_model_mesh != NULL && player_model_mesh->vertices != NULL) {
            if (player_actor_mesh != NULL)
                sceKernelDcacheWritebackRange(player_actor_mesh->vertices,
                                              player_actor_mesh->storage_bytes);
            draw_player_model(player, player_model_mesh);
        } else
            draw_player(player);
    }
        update_command_list_usage();
        sceGuFinish();
    {
        uint64_t submit_done = sceKernelGetSystemTimeWide();
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        ge_wait_us = sceKernelGetSystemTimeWide() - submit_done;
        cpu_submit_us = submit_done - now;
    }

    /* Write the HUD into the finished draw buffer before swapping it. */
    pspDebugScreenSetOffset((int)(uintptr_t)draw_buffer);
    pspDebugScreenSetTextColor(0xffffffff);
    pspDebugScreenSetXY(1, 1);
    pspDebugScreenPrintf("%s   %s", room_name,
                         player_mode ? "Walking prototype" :
                         display_mode == 4 ? "Room model textured" : display_mode == 3 ? "Spatial viewer" :
                         display_mode == 1 ? "Room model wireframe" : "Room model solid");
    pspDebugScreenSetXY(1, 2);
    if (display_mode != 3 && mesh)
        pspDebugScreenPrintf("%u models  %u triangles%s", (unsigned int)mesh->model_count,
                             (unsigned int)mesh->triangle_count, display_mode == 2 ? " + spatial overlay" : "");
    else
        pspDebugScreenPrintf("%u bounds  %u markers  %u lines",
                         (unsigned int)scene->box_count,
                         (unsigned int)scene->marker_count,
                         (unsigned int)(scene->vertex_count / 2));
    pspDebugScreenSetXY(1, 3);
    if (player_mode && player != NULL && mesh != NULL && mesh->cut_count != 0)
        pspDebugScreenPrintf("Cut %u/%u  Cam %c  Player %.1f,%.1f F%d %s M%u:%02u",
                             (unsigned int)(camera_cut + 1), (unsigned int)mesh->cut_count,
                             camera_variant ? 'B' : 'A', player->x, player->z,
                             (int)player->floor, player->blocked ? "Wall" : "",
                             player_animation_slot == UINT_MAX ? 0 : player_animation_slot,
                             (unsigned int)player_animation_frame);
    else if (player_mode && player != NULL)
        pspDebugScreenPrintf("Player %.1f,%.1f F%d %s M%u:%02u", player->x, player->z,
                             (int)player->floor, player->blocked ? "Wall" : "",
                             player_animation_slot == UINT_MAX ? 0 : player_animation_slot,
                             (unsigned int)player_animation_frame);
    else if (mesh && mesh->cut_count)
        pspDebugScreenPrintf("Cut %u/%u  Camera %c  Hidden objects %u",
                             (unsigned int)(camera_cut + 1), (unsigned int)mesh->cut_count,
                             camera_variant ? 'B' : 'A', hidden_object_count(mesh));
    else
        pspDebugScreenPrintf("No room camera cuts");
    draw_memory_hud(scene, mesh, textures, player_room);
    pspDebugScreenSetXY(1, 29);
    pspDebugScreenPrintf("SELECT: solid / wire / bounds / spatial / textured");
    pspDebugScreenSetXY(1, 31);
    pspDebugScreenPrintf(player_mode ? "Stick move/turn  D-pad tank controls  Fixed room camera" :
                         "D-pad orbit  Stick pan  L/R zoom  TRIANGLE reset");
    pspDebugScreenSetXY(1, 32);
    pspDebugScreenPrintf(player_mode ? "CROSS door  SQUARE return  TRIANGLE reset  START exit" :
                         "CROSS camera  CIRCLE cut  SQUARE rooms  START exit");
    sceDisplayWaitVblankStartCB();
    draw_buffer = sceGuSwapBuffers();
}

void recvxRendererStop(void)
{
    if (!initialized)
        return;
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceGuDisplay(GU_FALSE);
    sceGuTerm();
    initialized = 0;
    player_mode = 0;
    player_model_mesh = NULL;
    player_actor_mesh = NULL;
    player_actor_assets = NULL;
    player_animation_slot = UINT_MAX;
    player_animation_frame = 0.0f;
    pspDebugScreenSetOffset(0);
    pspDebugScreenEnableBackColor(1);
    pspDebugScreenInit();
}
