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
#include "psp/ninja_math.h"
#include "psp/timing.h"

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define BUFFER_WIDTH 512
#define FRAME_BYTES (BUFFER_WIDTH * SCREEN_HEIGHT * 4)
#define PLAYER_HOKAN_BLEND_PER_TICK ((65536.0f - 42598.0f) / 65536.0f)
#define PLAYER_REVERSE_HOKAN_BLEND_PER_TICK ((65536.0f - 49152.0f) / 65536.0f)

static unsigned int command_list[16384] __attribute__((aligned(64)));
static void *draw_buffer;
static ScePspFVector3 center, focus;
static float yaw, pitch, distance, radius;
static uint64_t previous_time;
static uint64_t frame_elapsed_us, cpu_submit_us, ge_wait_us;
static RecvxGameClock simulation_clock;
static size_t command_list_bytes, command_list_high_water;
static size_t module_text_bytes, module_data_bytes, module_bss_bytes;
static size_t player_source_bytes;
static int initialized;
static int display_mode;
static uint32_t camera_cut;
static uint32_t camera_variant;
static uint32_t fixed_camera_attribute;
static int camera_cut_initialized;
static int fixed_camera_attribute_initialized;
static int player_mode;
static const RecvxRoomMesh *player_model_mesh;
static RecvxRoomMesh *player_actor_mesh;
static const RecvxPlayerAssets *player_actor_assets;
static const RecvxTextureSet *player_actor_textures;
static unsigned int player_animation_slot;
static float player_animation_frame;
static unsigned int player_transition_slot;
static uint32_t player_transition_frame;
static unsigned int player_transition_remaining;
static unsigned int player_transition_duration;
static float player_transition_rate;
static float player_speed_per_tick;
static int fixed_camera_active;
static ScePspFVector3 fixed_eye_offset, fixed_camera_forward, fixed_camera_up;
static ScePspFVector3 fixed_camera_base;
static uint16_t fixed_camera_angles[3];
static float fixed_camera_lens;
static float fixed_camera_fov;
static ScePspFVector3 fixed_camera_player_focus;
static uint32_t fixed_camera_pose_cut;
static uint32_t fixed_camera_pose_variant;
static uint32_t fixed_camera_player_focus_cut;
static uint32_t fixed_camera_player_focus_variant;
static uint16_t fixed_camera_pose_flags;
static uint32_t fixed_camera_transition_frame;
static uint32_t fixed_camera_transition_duration;
static int fixed_camera_pose_initialized;
static int fixed_camera_player_focus_initialized;
static int fixed_camera_transition_active;

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
                            const RecvxPlayerRoom *player_room,
                            const RecvxPlayer *player)
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
    size_t floor_bytes = player_room != NULL ?
        (size_t)player_room->floor_count * sizeof(*player_room->floors) : 0;
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
    pspDebugScreenPrintf("Player data %uK mesh %uK wall %uK floor %uK door %uK",
                         (unsigned int)(player_source_bytes / 1024),
                         (unsigned int)(player_mesh_bytes / 1024),
                         (unsigned int)(wall_bytes / 1024),
                         (unsigned int)(floor_bytes / 1024),
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
    pspDebugScreenSetXY(1, 9);
    pspDebugScreenPrintf("Floor target %d",
                         player != NULL ? (int)player->floor_target_index : -1);
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
    float sine, cosine;
    float x = vector->x, y = vector->y, z = vector->z;
    recvxNinjaSinCos(angle, &sine, &cosine);
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

static int32_t tracking_camera_angle(int32_t base, int32_t current,
                                    int32_t desired, int32_t limit)
{
    int32_t relative = camera_angle_delta(desired, base);
    int32_t target = current;
    if (limit < 0)
        limit = -limit;
    if (relative < -limit) {
        target = (uint16_t)(base + relative + limit);
        if (camera_angle_delta(target, current) > 0)
            target = current;
    } else if (limit < relative) {
        target = (uint16_t)(base + relative - limit);
        if (camera_angle_delta(target, current) < 0)
            target = current;
    }
    return target;
}

static float camera_rate(float rate)
{
    if (rate <= 0.0f)
        return 0.0f;
    return rate >= 1.0f ? 1.0f : rate;
}

static int32_t camera_angle_step(uint16_t current, int32_t target, float rate)
{
    int32_t delta = camera_angle_delta(target, current);
    return (uint16_t)(current + (int32_t)(delta * rate));
}

static int fixed_camera_transition_required(const RecvxRoomCamera *camera)
{
    return fixed_camera_pose_initialized &&
           (fixed_camera_pose_cut != camera_cut ||
            fixed_camera_pose_variant != camera_variant) &&
           ((camera->flags & 0x2u) || (fixed_camera_pose_flags & 0x2u));
}

static void advance_fixed_camera_pose(const RecvxRoomCamera *camera,
                                      const ScePspFVector3 *target_position,
                                      const int32_t target_angles[3],
                                      float target_lens, float target_fov,
                                      uint32_t ticks)
{
    int pose_changed;
    uint32_t tick;
    if (!fixed_camera_pose_initialized) {
        fixed_camera_base = *target_position;
        fixed_camera_lens = target_lens;
        fixed_camera_fov = target_fov;
        for (unsigned int axis = 0; axis < 3; ++axis)
            fixed_camera_angles[axis] = (uint16_t)target_angles[axis];
        fixed_camera_pose_cut = camera_cut;
        fixed_camera_pose_variant = camera_variant;
        fixed_camera_pose_flags = camera->flags;
        fixed_camera_transition_active = 0;
        fixed_camera_pose_initialized = 1;
    } else {
        pose_changed = fixed_camera_pose_cut != camera_cut ||
                       fixed_camera_pose_variant != camera_variant;
        if (pose_changed) {
            int32_t duration;
            fixed_camera_transition_active =
                (camera->flags & 0x2u) || (fixed_camera_pose_flags & 0x2u);
            fixed_camera_transition_frame = 0;
            duration = (int8_t)camera->speed + 1;
            fixed_camera_transition_duration = duration > 0 ?
                (uint32_t)duration : 1;
            fixed_camera_pose_cut = camera_cut;
            fixed_camera_pose_variant = camera_variant;
            fixed_camera_pose_flags = camera->flags;
            if (!fixed_camera_transition_active) {
                fixed_camera_base = *target_position;
                fixed_camera_lens = target_lens;
                fixed_camera_fov = target_fov;
                for (unsigned int axis = 0; axis < 3; ++axis)
                    fixed_camera_angles[axis] = (uint16_t)target_angles[axis];
            }
        }
        for (tick = 0; tick < ticks; ++tick) {
            float position_rate = camera_rate(camera->angle_speed);
            float angle_rate = camera_rate(camera->angle_speed8 * 0.01f);
            float perspective_rate;
            perspective_rate = angle_rate;
            if (fixed_camera_transition_active) {
                position_rate = fixed_camera_transition_duration != 0 ?
                    (float)fixed_camera_transition_frame /
                    (float)fixed_camera_transition_duration : 1.0f;
                perspective_rate = position_rate;
                if (!(camera->flags & 0x4u))
                    angle_rate = position_rate;
            }
            fixed_camera_base.x += position_rate *
                (target_position->x - fixed_camera_base.x);
            fixed_camera_base.y += position_rate *
                (target_position->y - fixed_camera_base.y);
            fixed_camera_base.z += position_rate *
                (target_position->z - fixed_camera_base.z);
            fixed_camera_lens += position_rate *
                (target_lens - fixed_camera_lens);
            fixed_camera_fov += perspective_rate *
                (target_fov - fixed_camera_fov);
            for (unsigned int axis = 0; axis < 3; ++axis)
                fixed_camera_angles[axis] = (uint16_t)camera_angle_step(
                    fixed_camera_angles[axis], target_angles[axis], angle_rate);
            if (fixed_camera_transition_active) {
                ++fixed_camera_transition_frame;
                if (fixed_camera_transition_frame >= fixed_camera_transition_duration)
                    fixed_camera_transition_active = 0;
            }
        }
    }
}

static void fixed_camera_base_position(const RecvxRoomMesh *mesh,
                                       const ScePspFVector3 *player_focus,
                                       uint32_t attr,
                                       ScePspFVector3 *position)
{
    const RecvxRoomCamera *camera =
        &mesh->cameras[camera_cut * 2 + camera_variant];
    float x_delta = player_focus->x - camera->cut_center_x;
    float z_delta = player_focus->z - camera->cut_center_z;
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
                                   const RecvxPlayer *player,
                                   uint32_t simulation_ticks)
{
    const RecvxRoomCamera *camera;
    ScePspFVector3 base_position, aim_position, offset = {0.0f, 0.0f, 0.0f};
    ScePspFVector3 forward = {0.0f, 0.0f, -1.0f};
    ScePspFVector3 up = {0.0f, 1.0f, 0.0f};
    int32_t angles[3];
    uint32_t attr, perspective, room_attribute;
    float dx, dy, dz, horizontal, target_fov = 50.0f;
    fixed_camera_active = 0;
    if (mesh == NULL || player == NULL || mesh->cameras == NULL ||
        camera_cut >= mesh->cut_count)
        return;
    camera = &mesh->cameras[camera_cut * 2 + camera_variant];
    if (!fixed_camera_player_focus_initialized ||
        fixed_camera_player_focus_cut != camera_cut ||
        fixed_camera_player_focus_variant != camera_variant) {
        fixed_camera_player_focus.x = player->x;
        fixed_camera_player_focus.y = player->y + camera->height;
        fixed_camera_player_focus.z = player->z;
        if (recvxRoomCutAttributeAtPosition(mesh, camera_cut, player->x,
                                            player->z, player->floor,
                                            &room_attribute)) {
            fixed_camera_attribute = room_attribute;
            fixed_camera_attribute_initialized = 1;
        }
        fixed_camera_player_focus_cut = camera_cut;
        fixed_camera_player_focus_variant = camera_variant;
        fixed_camera_player_focus_initialized = 1;
    } else {
        for (uint32_t tick = 0; tick < simulation_ticks; ++tick) {
            fixed_camera_player_focus.x +=
                0.5f * (player->x - fixed_camera_player_focus.x);
            fixed_camera_player_focus.z +=
                0.5f * (player->z - fixed_camera_player_focus.z);
            if (recvxRoomCutAttributeAtPosition(mesh, camera_cut,
                                                fixed_camera_player_focus.x,
                                                fixed_camera_player_focus.z,
                                                player->floor,
                                                &room_attribute)) {
                fixed_camera_attribute = room_attribute;
                fixed_camera_attribute_initialized = 1;
            }
        }
        fixed_camera_player_focus.y = player->y + camera->height;
    }
    if (recvxRoomCutAttributeAtPosition(mesh, camera_cut,
                                        fixed_camera_player_focus.x,
                                        fixed_camera_player_focus.z,
                                        player->floor, &room_attribute)) {
        fixed_camera_attribute = room_attribute;
        fixed_camera_attribute_initialized = 1;
    }
    attr = fixed_camera_attribute_initialized ? fixed_camera_attribute : 0;
    attr = camera_variant != 0 ? attr >> 16 : attr & 0xffffu;
    fixed_camera_base_position(mesh, &fixed_camera_player_focus, attr,
                               &base_position);
    aim_position = base_position;
    if (fixed_camera_pose_initialized &&
        ((fixed_camera_pose_cut == camera_cut &&
          fixed_camera_pose_variant == camera_variant) ||
         fixed_camera_transition_required(camera)))
        aim_position = fixed_camera_base;
    for (unsigned int axis = 0; axis < 3; ++axis)
        angles[axis] = signed_camera_angle(camera->angles[axis]);
    dx = fixed_camera_player_focus.x - aim_position.x;
    dz = fixed_camera_player_focus.z - aim_position.z;
    dy = fixed_camera_player_focus.y - aim_position.y;
    horizontal = sqrtf(dx * dx + dz * dz);
    if (camera->flags & 0x4u) {
        if (!(attr & 0x4u))
            angles[0] = (int32_t)(-atan2f(dy, horizontal) * 10430.37835f);
        if (!(attr & 0x2u))
            angles[1] = (int32_t)(-atan2f(dx, dz) * 10430.37835f) + 32768;
    } else if (!(camera->flags & 1u)) {
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
    } else {
        int32_t current_pitch = fixed_camera_pose_initialized ?
            signed_camera_angle(fixed_camera_angles[0]) : angles[0];
        int32_t current_yaw = fixed_camera_pose_initialized ?
            signed_camera_angle(fixed_camera_angles[1]) : angles[1];
        int32_t pitch_limit = signed_camera_angle(camera->angle_limits[0]);
        int32_t yaw_limit = signed_camera_angle(camera->angle_limits[1]);
        int32_t desired_pitch =
            (int32_t)(-atan2f(dy, horizontal) * 10430.37835f);
        int32_t desired_yaw =
            (int32_t)(-atan2f(dx, dz) * 10430.37835f) + 32768;
        if (camera->angle_limits[0] != 0 && !(attr & 0x4u))
            angles[0] = tracking_camera_angle(angles[0], current_pitch,
                                              desired_pitch, pitch_limit);
        if (camera->angle_limits[1] != 0 && !(attr & 0x2u))
            angles[1] = tracking_camera_angle(angles[1], current_yaw,
                                              desired_yaw, yaw_limit);
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
        target_fov = (a0 + x_delta * (a1 - a0) / camera->cut_width) *
                     (360.0f / 65536.0f);
    } else if ((perspective & 0xffffu) != 0) {
        target_fov = (perspective & 0xffffu) * (360.0f / 65536.0f);
    }
    if (target_fov < 15.0f || target_fov > 120.0f)
        target_fov = 50.0f;
    advance_fixed_camera_pose(camera, &base_position, angles, camera->lens,
                              target_fov, simulation_ticks);
    for (unsigned int axis = 0; axis < 3; ++axis)
        angles[axis] = signed_camera_angle(fixed_camera_angles[axis]);
    offset.z = -fixed_camera_lens;
    rotate_camera_vector(&offset, 2, angles[2]);
    rotate_camera_vector(&offset, 0, angles[0]);
    rotate_camera_vector(&offset, 1, angles[1]);
    forward = (ScePspFVector3){0.0f, 0.0f, -1.0f};
    up = (ScePspFVector3){0.0f, 1.0f, 0.0f};
    rotate_camera_vector(&forward, 2, -angles[2]);
    rotate_camera_vector(&forward, 0, -angles[0]);
    rotate_camera_vector(&forward, 1, -angles[1]);
    rotate_camera_vector(&up, 2, -angles[2]);
    rotate_camera_vector(&up, 0, -angles[0]);
    rotate_camera_vector(&up, 1, -angles[1]);
    fixed_eye_offset = offset;
    fixed_camera_forward = forward;
    fixed_camera_up = up;
    fixed_camera_active = 1;
}

static void fixed_camera_position(const RecvxRoomMesh *mesh,
                                  const RecvxPlayer *player,
                                  ScePspFVector3 *position)
{
    (void)mesh;
    (void)player;
    *position = fixed_camera_base;
    position->x += fixed_eye_offset.x;
    position->y += fixed_eye_offset.y;
    position->z += fixed_eye_offset.z;
}

static int update_player_culling(const RecvxRoomMesh *mesh, const RecvxPlayer *player,
                                 int current_cut)
{
    int selected;
    int changed;
    uint32_t camera;
    uint32_t room_attribute;
    if (mesh == NULL || player == NULL || mesh->cut_count == 0)
        return -1;
    selected = recvxRoomCutForPosition(mesh, player->x, player->z, player->floor,
                                       current_cut);
    if (selected < 0)
        return -1;
    if (!camera_cut_initialized || camera_cut != (uint32_t)selected) {
        if (recvxRoomCutAttributeAtPosition(mesh, (uint32_t)selected,
                                            player->x, player->z, player->floor,
                                            &room_attribute)) {
            fixed_camera_attribute = room_attribute;
            fixed_camera_attribute_initialized = 1;
        }
        camera = fixed_camera_attribute_initialized ?
            ((fixed_camera_attribute & 0x8000u) != 0) :
            recvxRoomCameraForPosition(mesh, (uint32_t)selected,
                                       player->x, player->z, player->floor);
    } else {
        camera = camera_variant;
    }
    changed = !camera_cut_initialized || camera_cut != (uint32_t)selected ||
              camera_variant != camera;
    camera_cut = (uint32_t)selected;
    camera_variant = camera;
    camera_cut_initialized = 1;
    return changed;
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
                              const RecvxRoomMesh *model,
                              const RecvxTextureSet *textures)
{
    ScePspFVector3 position = {player->x, player->y, player->z};
    uint32_t i;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    if (textures != NULL && textures->textures != NULL && textures->count != 0) {
        sceGuTexMode(GU_PSM_T8, 0, 0, 1);
        sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuTexScale(1, 1);
        sceGuTexOffset(0, 0);
        sceGuAlphaFunc(GU_GREATER, 0, 0xff);
        sceGuEnable(GU_ALPHA_TEST);
    }
    sceGumPushMatrix();
    sceGumTranslate(&position);
    sceGumRotateY(player->yaw);
    for (i = 0; i < model->batch_count; ++i) {
        const RecvxMeshBatch *batch = &model->batches[i];
        const RecvxTexture *texture = NULL;
        if (batch->vertex_count == 0)
            continue;
        if (textures != NULL && textures->textures != NULL &&
            batch->texture_index < textures->count)
            texture = &textures->textures[batch->texture_index];
        set_batch_blending(batch);
        if (texture != NULL) {
            sceGuEnable(GU_TEXTURE_2D);
            sceGuClutLoad(32, texture->palette);
            sceGuTexImage(0, texture->width, texture->height,
                          texture->width, texture->pixels);
            sceGuTexWrap(batch->flags & 1 ? GU_CLAMP : GU_REPEAT,
                         batch->flags & 2 ? GU_CLAMP : GU_REPEAT);
            sceGuTexFilter(batch->flags & 4 ? GU_LINEAR : GU_NEAREST,
                           batch->flags & 4 ? GU_LINEAR : GU_NEAREST);
            sceGuTexFlush();
        } else
            sceGuDisable(GU_TEXTURE_2D);
        sceGumDrawArray(GU_TRIANGLES,
                        GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF |
                        GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        (int)batch->vertex_count, NULL,
                        model->vertices + batch->first_vertex);
    }
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_TEXTURE_2D);
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
    configure_fixed_camera(mesh, player, 0);
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
                       const RecvxTextureSet *actor_textures,
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
    if (actor_textures)
        for (uint32_t i = 0; i < actor_textures->count; ++i) {
            const RecvxTexture *texture = &actor_textures->textures[i];
            sceKernelDcacheWritebackRange(texture->pixels,
                                          texture->image_bytes + 1024);
        }
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
    player_actor_textures = actor_textures;
    player_animation_slot = UINT_MAX;
    player_animation_frame = 0.0f;
    player_transition_slot = UINT_MAX;
    player_transition_frame = 0;
    player_transition_remaining = 0;
    player_transition_duration = 0;
    player_transition_rate = PLAYER_HOKAN_BLEND_PER_TICK;
    camera_cut = 0;
    camera_variant = 0;
    fixed_camera_attribute = 0;
    camera_cut_initialized = 0;
    fixed_camera_attribute_initialized = 0;
    fixed_camera_pose_initialized = 0;
    fixed_camera_player_focus_initialized = 0;
    fixed_camera_transition_active = 0;
    fixed_camera_transition_frame = 0;
    fixed_camera_transition_duration = 0;
    if (player_mode)
        update_player_culling(mesh, player, -1);
    apply_room_lighting(mesh);
    reset_camera(scene, mesh, player);
    previous_time = recvxPlatformTimeUs();
    recvxGameClockReset(&simulation_clock, previous_time);
    frame_elapsed_us = cpu_submit_us = ge_wait_us = 0;
    return 0;
}

static unsigned int mapped_player_motion_frame(unsigned int source_slot,
                                               float source_frame,
                                               const RecvxPlayerMotionAsset *target)
{
    const RecvxPlayerMotionAsset *source;
    if (player_actor_assets == NULL || target == NULL ||
        source_slot >= player_actor_assets->motion_count)
        return 0;
    source = &player_actor_assets->motions[source_slot];
    if (!source->present || source->frame_count == 0)
        return 0;
    return (unsigned int)(source_frame * target->frame_count / source->frame_count);
}

static const RecvxPlayerMotionAsset *player_motion(unsigned int slot)
{
    const RecvxPlayerMotionAsset *motion;
    if (player_actor_assets == NULL || slot >= player_actor_assets->motion_count)
        return NULL;
    motion = &player_actor_assets->motions[slot];
    return motion->present && motion->frame_count != 0 ? motion : NULL;
}

static void select_player_motion(unsigned int motion_slot,
                                 unsigned int stair_phase)
{
    const RecvxPlayerMotionAsset *motion = player_motion(motion_slot);
    unsigned int initial_frame = 0;
    if (player_animation_slot == motion_slot)
        return;
    player_transition_slot = player_animation_slot;
    player_transition_frame = (uint32_t)player_animation_frame;
    player_animation_slot = motion_slot;
    if (stair_phase != RECVX_PLAYER_STAIR_NONE) {
        player_transition_duration = 4;
        player_transition_rate = PLAYER_HOKAN_BLEND_PER_TICK;
        if (stair_phase == RECVX_PLAYER_STAIR_RECOVER)
            initial_frame = 15;
    } else {
        switch (motion_slot) {
        case 0:
            player_transition_duration = 8;
            player_transition_rate = PLAYER_HOKAN_BLEND_PER_TICK;
            initial_frame = player_transition_slot == 4 ?
                mapped_player_motion_frame(player_transition_slot,
                                           player_animation_frame, motion) : 5;
            break;
        case 4:
            player_transition_duration = 4;
            player_transition_rate = PLAYER_HOKAN_BLEND_PER_TICK;
            initial_frame = mapped_player_motion_frame(player_transition_slot,
                                                       player_animation_frame, motion);
            break;
        case 16:
            player_transition_duration = 8;
            player_transition_rate = PLAYER_REVERSE_HOKAN_BLEND_PER_TICK;
            if (player_transition_slot == 0 || player_transition_slot == 4)
                initial_frame = mapped_player_motion_frame(player_transition_slot,
                                                           player_animation_frame, motion);
            else
                initial_frame = 25;
            break;
        case 39:
            player_transition_duration = 8;
            player_transition_rate = PLAYER_HOKAN_BLEND_PER_TICK;
            break;
        case 42:
        default:
            player_transition_duration = 5;
            player_transition_rate = PLAYER_HOKAN_BLEND_PER_TICK;
            break;
        }
    }
    player_transition_remaining = player_transition_slot == UINT_MAX ? 0 :
                                 player_transition_duration;
    player_animation_frame = motion != NULL &&
        initial_frame < motion->frame_count ? (float)initial_frame : 0.0f;
}

static void update_player_simulation(RecvxPlayer *player,
                                    const RecvxPlayerRoom *player_room,
                                    float forward, float turn,
                                    int run, uint32_t ticks)
{
    const RecvxPlayerMotionAsset *motion = NULL;
    unsigned int normal_motion_slot, speed_track = 0;
    float turn_speed;
    uint32_t tick;

    if (player == NULL || player_room == NULL)
        return;
    if (forward < 0.0f) {
        normal_motion_slot = 16;
        speed_track = 6;
    } else if (forward > 0.0f && run) {
        normal_motion_slot = 4;
        speed_track = 3;
    } else if (forward > 0.0f)
        normal_motion_slot = 0;
    else if (turn != 0.0f)
        normal_motion_slot = 39;
    else
        normal_motion_slot = 42;
    turn_speed = forward == 0.0f ? 3.769911184f :
                 run ? 2.356194490f : 2.094395102f;
    for (tick = 0; tick < ticks; ++tick) {
        unsigned int stair_phase = player->stair_phase;
        unsigned int motion_slot = normal_motion_slot;
        float speed_per_tick = 4.0f / RECVX_GAME_TICKS_PER_SECOND;
        if (stair_phase != RECVX_PLAYER_STAIR_NONE) {
            motion_slot = stair_phase == RECVX_PLAYER_STAIR_TURN ? 39u :
                stair_phase == RECVX_PLAYER_STAIR_MOVE ?
                    (unsigned int)player->stair_motion_slot : 0u;
            select_player_motion(motion_slot, stair_phase);
            if (stair_phase == RECVX_PLAYER_STAIR_MOVE)
                player_animation_frame = (float)player->stair_motion_frame;
            else if (stair_phase == RECVX_PLAYER_STAIR_RECOVER)
                player_animation_frame = 15.0f +
                    (float)(10u - player->stair_recovery_ticks);
            player_speed_per_tick = 0.0f;
            recvxPlayerAdvanceStair(player, player_room, player_actor_assets);
            if (stair_phase == RECVX_PLAYER_STAIR_MOVE &&
                player->stair_phase == RECVX_PLAYER_STAIR_MOVE)
                player_animation_frame = (float)player->stair_motion_frame;
            else if (stair_phase == RECVX_PLAYER_STAIR_MOVE &&
                     player->stair_phase == RECVX_PLAYER_STAIR_RECOVER) {
                select_player_motion(0, RECVX_PLAYER_STAIR_RECOVER);
                player_animation_frame = 15.0f;
            } else if (stair_phase == RECVX_PLAYER_STAIR_RECOVER &&
                       player->stair_phase == RECVX_PLAYER_STAIR_NONE) {
                select_player_motion(normal_motion_slot,
                                     RECVX_PLAYER_STAIR_NONE);
            }
        } else {
            select_player_motion(normal_motion_slot, RECVX_PLAYER_STAIR_NONE);
            if (forward != 0.0f && player_actor_assets != NULL) {
                float authored_speed;
                if (recvxPlayerSpeedAt(player_actor_assets, speed_track,
                                       (uint32_t)player_animation_frame,
                                       &authored_speed) == 1)
                    speed_per_tick = fabsf(authored_speed);
            }
            player_speed_per_tick = forward * speed_per_tick;
            recvxPlayerUpdateAtSpeed(player, player_room, forward, turn,
                                     speed_per_tick * RECVX_GAME_TICKS_PER_SECOND,
                                     turn_speed, 1.0f / RECVX_GAME_TICKS_PER_SECOND);
            if (player_actor_assets != NULL)
                recvxPlayerTryBeginStair(player, player_room,
                                         player_actor_assets);
        }
        motion = player_motion(player_animation_slot);
        if (motion != NULL && stair_phase != RECVX_PLAYER_STAIR_MOVE) {
            float frame_step = motion_slot == 39u &&
                               stair_phase == RECVX_PLAYER_STAIR_NONE &&
                               turn > 0.0f ? -1.0f : 1.0f;
            player_animation_frame += frame_step;
            if (player_animation_frame < 0.0f)
                player_animation_frame += (float)motion->frame_count;
            else if (player_animation_frame >= motion->frame_count)
                player_animation_frame = fmodf(player_animation_frame,
                                               (float)motion->frame_count);
        }
        if (player_transition_remaining != 0)
            --player_transition_remaining;
    }
    if (ticks == 0 && forward == 0.0f)
        player_speed_per_tick = 0.0f;
    motion = player_motion(player_animation_slot);
    if (player_actor_assets != NULL && player_actor_mesh != NULL &&
        player_actor_assets->model_count != 0 && motion != NULL) {
        const RecvxPlayerMotionAsset *from_motion = NULL;
        float blend = 1.0f;
        if (player_transition_remaining != 0 &&
            player_transition_slot < player_actor_assets->motion_count) {
            const RecvxPlayerMotionAsset *candidate =
                &player_actor_assets->motions[player_transition_slot];
            if (candidate->present && candidate->frame_count != 0) {
                from_motion = candidate;
                blend = 1.0f - powf(1.0f - player_transition_rate,
                                    (float)(player_transition_duration -
                                            player_transition_remaining));
            }
        }
        recvxBlendPlayerMeshPose(&player_actor_assets->models[0], from_motion,
                                 player_transition_frame, motion,
                                 (uint32_t)player_animation_frame, blend,
                                 player_actor_mesh);
    }
}

void recvxRendererFrame(const RecvxScene *scene, RecvxRoomMesh *mesh, const RecvxTextureSet *textures,
                        RecvxPlayer *player, const RecvxPlayerRoom *player_room,
                        const char *room_name,
                        const RecvxInputState *input)
{
    uint64_t now = recvxPlatformTimeUs();
    float frame_dt = fminf(0.05f, (float)(now - previous_time) * 0.000001f);
    float analog_x = ((int)input->analog_x - 128) / 128.0f;
    float analog_y = ((int)input->analog_y - 128) / 128.0f;
    uint32_t pressed = input->pressed;
    uint32_t simulation_ticks = recvxGameClockAdvance(&simulation_clock, now);
    float near_plane, far_plane;
    ScePspFVector3 eye, up = {0, 1, 0};
    int lighting_changed = 0;
    frame_elapsed_us = now - previous_time;
    previous_time = now;

    if ((pressed & RECVX_BUTTON_SELECT) && mesh && mesh->vertices) {
        display_mode = (display_mode + 1) % (textures && textures->count ? 5 : 4);
        if (display_mode == 1) {
            if (set_wireframe(mesh, 1) != 0)
                display_mode = 0;
        } else {
            set_wireframe(mesh, 0);
        }
        reset_camera(scene, mesh, player_mode ? player : NULL);
    }
    if (!player_mode && (pressed & RECVX_BUTTON_CIRCLE) && mesh && mesh->cut_count) {
        camera_cut = (camera_cut + 1) % mesh->cut_count;
        lighting_changed = 1;
    }
    if (!player_mode && (pressed & RECVX_BUTTON_CROSS) && mesh && mesh->cut_count) {
        camera_variant ^= 1;
        lighting_changed = 1;
    }
    if (pressed & RECVX_BUTTON_TRIANGLE)
        reset_camera(scene, mesh, player_mode ? player : NULL);
    if (!player_mode && (input->buttons & RECVX_BUTTON_LTRIGGER))
        distance *= 1.0f + frame_dt;
    if (!player_mode && (input->buttons & RECVX_BUTTON_RTRIGGER))
        distance *= 1.0f - frame_dt;
    if (player_mode && player != NULL && player_room != NULL) {
        float forward = -analog_y, turn = analog_x;
        if (fabsf(forward) < 0.16f)
            forward = 0.0f;
        if (fabsf(turn) < 0.16f)
            turn = 0.0f;
        if (input->buttons & RECVX_BUTTON_UP)
            forward = 1.0f;
        else if (input->buttons & RECVX_BUTTON_DOWN)
            forward = -1.0f;
        if (input->buttons & RECVX_BUTTON_LEFT)
            turn = -1.0f;
        else if (input->buttons & RECVX_BUTTON_RIGHT)
            turn = 1.0f;
        update_player_simulation(player, player_room, forward, turn,
                                 (input->buttons & RECVX_BUTTON_RTRIGGER) != 0,
                                 simulation_ticks);
        if (update_player_culling(mesh, player, (int)camera_cut) > 0) {
            lighting_changed = 1;
        }
        configure_fixed_camera(mesh, player, simulation_ticks);
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
        if (input->buttons & RECVX_BUTTON_LEFT)
            yaw -= frame_dt * 1.7f;
        if (input->buttons & RECVX_BUTTON_RIGHT)
            yaw += frame_dt * 1.7f;
        if (input->buttons & RECVX_BUTTON_UP)
            pitch += frame_dt;
        if (input->buttons & RECVX_BUTTON_DOWN)
            pitch -= frame_dt;
        pitch = fmaxf(0.12f, fminf(1.4f, pitch));
        yaw = fmodf(yaw, 6.283185307f);
        if (fabsf(analog_x) < 0.16f)
            analog_x = 0;
        if (fabsf(analog_y) < 0.16f)
            analog_y = 0;
        center.x += (analog_x * cosf(yaw) + analog_y * sinf(yaw)) * radius * frame_dt;
        center.z += (-analog_x * sinf(yaw) + analog_y * cosf(yaw)) * radius * frame_dt;
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
            draw_player_model(player, player_model_mesh, player_actor_textures);
        } else
            draw_player(player);
    }
        update_command_list_usage();
        sceGuFinish();
    {
        uint64_t submit_done = recvxPlatformTimeUs();
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        ge_wait_us = recvxPlatformTimeUs() - submit_done;
        cpu_submit_us = submit_done - now;
    }

    /* Write the HUD into the finished draw buffer before swapping it. */
    pspDebugScreenSetOffset((int)(uintptr_t)draw_buffer);
    pspDebugScreenSetTextColor(0xffffffff);
    pspDebugScreenSetXY(1, 1);
    pspDebugScreenPrintf("%s   %s", room_name,
                         player_mode ? "Player prototype" :
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
        pspDebugScreenPrintf("Cut %u/%u  Cam %c  Player %.1f,%.1f F%d %s M%u:%02u v%+.3f",
                             (unsigned int)(camera_cut + 1), (unsigned int)mesh->cut_count,
                             camera_variant ? 'B' : 'A', player->x, player->z,
                             (int)player->floor, player->blocked ? "Wall" : "",
                             player_animation_slot == UINT_MAX ? 0 : player_animation_slot,
                             (unsigned int)player_animation_frame,
                             player_speed_per_tick);
    else if (player_mode && player != NULL)
        pspDebugScreenPrintf("Player %.1f,%.1f F%d %s M%u:%02u v%+.3f", player->x, player->z,
                             (int)player->floor, player->blocked ? "Wall" : "",
                             player_animation_slot == UINT_MAX ? 0 : player_animation_slot,
                             (unsigned int)player_animation_frame,
                             player_speed_per_tick);
    else if (mesh && mesh->cut_count)
        pspDebugScreenPrintf("Cut %u/%u  Camera %c  Hidden objects %u",
                             (unsigned int)(camera_cut + 1), (unsigned int)mesh->cut_count,
                             camera_variant ? 'B' : 'A', hidden_object_count(mesh));
    else
        pspDebugScreenPrintf("No room camera cuts");
    draw_memory_hud(scene, mesh, textures, player_room, player);
    pspDebugScreenSetXY(1, 29);
    pspDebugScreenPrintf("SELECT: solid / wire / bounds / spatial / textured");
    pspDebugScreenSetXY(1, 31);
    pspDebugScreenPrintf(player_mode ? "Stick move/turn  D-pad tank controls  R run  Fixed room camera" :
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
    player_actor_textures = NULL;
    player_animation_slot = UINT_MAX;
    player_animation_frame = 0.0f;
    pspDebugScreenSetOffset(0);
    pspDebugScreenEnableBackColor(1);
    pspDebugScreenInit();
}
