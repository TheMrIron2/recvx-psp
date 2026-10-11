#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "psp/player.h"
#include "psp/abi.h"
#include "psp/room.h"

#define PLAYER_RADIUS 3.5f
#define PLAYER_HEIGHT 16.5f
#define PLAYER_STEP_HEIGHT 0.45f
#define PLAYER_COLLISION_FLAGS 0x919u
#define PLAYER_COLLISION_STATE_FLAGS 0x40000000u
#define PLAYER_DIAGNOSTIC_WALK_SPEED 4.0f
#define PLAYER_DIAGNOSTIC_TURN_SPEED 2.4f
#define PLAYER_FLOOR_TARGET_DISTANCE 6.0f
#define PLAYER_GRAVITY 9.8f
#define PLAYER_SLOW_SURFACE_SCALE 0.8f
#define PLAYER_PI 3.141592654f
#define PLAYER_TAU 6.283185307f

static uint32_t read_u32(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static float read_f32(const unsigned char *data)
{
    uint32_t bits = read_u32(data);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int point_in_triangle(float x, float z, const float triangle[6])
{
    float c0 = (triangle[2] - triangle[0]) * (z - triangle[1]) -
               (triangle[3] - triangle[1]) * (x - triangle[0]);
    float c1 = (triangle[4] - triangle[2]) * (z - triangle[3]) -
               (triangle[5] - triangle[3]) * (x - triangle[2]);
    float c2 = (triangle[0] - triangle[4]) * (z - triangle[5]) -
               (triangle[1] - triangle[5]) * (x - triangle[4]);
    return !((c0 < 0 || c1 < 0 || c2 < 0) &&
             (c0 > 0 || c1 > 0 || c2 > 0));
}

static float segment_distance_squared(float x, float z, float ax, float az,
                                      float bx, float bz)
{
    float dx = bx - ax, dz = bz - az;
    float length = dx * dx + dz * dz;
    float t = length > 0.0f ? ((x - ax) * dx + (z - az) * dz) / length : 0.0f;
    float px, pz;
    t = fmaxf(0.0f, fminf(1.0f, t));
    px = ax + t * dx;
    pz = az + t * dz;
    dx = x - px;
    dz = z - pz;
    return dx * dx + dz * dz;
}

static void triangle_for_wall(const RecvxPlayerWall *wall, float triangle[6])
{
    float x0 = fminf(wall->px, wall->px + wall->w);
    float x1 = fmaxf(wall->px, wall->px + wall->w);
    float z0 = fminf(wall->pz, wall->pz + wall->d);
    float z1 = fmaxf(wall->pz, wall->pz + wall->d);
    switch (wall->id & 3) {
    case 1:
        triangle[0] = x0; triangle[1] = z0;
        triangle[2] = x1; triangle[3] = z0;
        triangle[4] = x1; triangle[5] = z1;
        break;
    case 2:
        triangle[0] = x1; triangle[1] = z0;
        triangle[2] = x1; triangle[3] = z1;
        triangle[4] = x0; triangle[5] = z1;
        break;
    case 3:
        triangle[0] = x0; triangle[1] = z0;
        triangle[2] = x0; triangle[3] = z1;
        triangle[4] = x1; triangle[5] = z1;
        break;
    default:
        triangle[0] = x0; triangle[1] = z0;
        triangle[2] = x1; triangle[3] = z0;
        triangle[4] = x0; triangle[5] = z1;
        break;
    }
}

static int circle_intersects_triangle(float x, float z, float radius,
                                      const float triangle[6])
{
    float radius_squared = radius * radius;
    if (point_in_triangle(x, z, triangle))
        return 1;
    return segment_distance_squared(x, z, triangle[0], triangle[1],
                                    triangle[2], triangle[3]) <= radius_squared ||
           segment_distance_squared(x, z, triangle[2], triangle[3],
                                    triangle[4], triangle[5]) <= radius_squared ||
           segment_distance_squared(x, z, triangle[4], triangle[5],
                                    triangle[0], triangle[1]) <= radius_squared;
}

static int circle_intersects_rectangle(float x, float z, float px, float pz,
                                       float width, float depth, float radius)
{
    float min_x = fminf(px, px + width);
    float max_x = fmaxf(px, px + width);
    float min_z = fminf(pz, pz + depth);
    float max_z = fmaxf(pz, pz + depth);
    float dx = x < min_x ? min_x - x : x > max_x ? x - max_x : 0.0f;
    float dz = z < min_z ? min_z - z : z > max_z ? z - max_z : 0.0f;
    return dx * dx + dz * dz <= radius * radius;
}

static int stair_contains(const RecvxPlayerRoom *room, int32_t floor,
                          float x, float z)
{
    uint32_t i;
    for (i = 0; i < room->stair_count; ++i) {
        const RecvxPlayerWall *stair = &room->stairs[i];
        if (stair->active && stair->floor == floor &&
            stair->parameters[0] != 0 &&
            x >= stair->px && x <= stair->px + stair->w &&
            z >= stair->pz && z <= stair->pz + stair->d)
            return (int)i;
    }
    return -1;
}

static void set_stair_limit(RecvxPlayerWall *wall,
                            const RecvxPlayerWall *stair,
                            float x, float z, float width, float depth,
                            int32_t floor)
{
    memset(wall, 0, sizeof(*wall));
    wall->active = 1;
    wall->type = 1;
    wall->floor = (int8_t)floor;
    wall->attr = 0x800000u;
    wall->px = x;
    wall->py = stair->py;
    wall->pz = z;
    wall->w = width;
    wall->h = 1.0f;
    wall->d = depth;
}

static uint32_t build_stair_limits(RecvxPlayer *player,
                                   const RecvxPlayerRoom *room,
                                   RecvxPlayerWall limits[4])
{
    const RecvxPlayerWall *stair;
    float radius = PLAYER_RADIUS;
    float margin = 0.5f * radius;
    int stair_index = stair_contains(room, player->floor, player->x, player->z);
    uint32_t count = 0;
    if (stair_index >= 0)
        player->stair_index = stair_index;
    if (player->stair_index < 0 ||
        (uint32_t)player->stair_index >= room->stair_count)
        return 0;
    stair = &room->stairs[player->stair_index];
    if (stair_contains(room, player->floor, player->x, player->z - radius) < 0) {
        set_stair_limit(&limits[count++], stair,
                        stair->px - 2.0f * radius,
                        stair->pz - (2.0f * radius + margin),
                        stair->w + 4.0f * radius, 2.0f * radius,
                        player->floor);
    }
    if (stair_contains(room, player->floor, player->x + radius, player->z) < 0) {
        set_stair_limit(&limits[count++], stair,
                        margin + stair->px + stair->w,
                        stair->pz - 2.0f * radius,
                        2.0f * radius, stair->d + 4.0f * radius,
                        player->floor);
    }
    if (stair_contains(room, player->floor, player->x, player->z + radius) < 0) {
        set_stair_limit(&limits[count++], stair,
                        stair->px - 2.0f * radius,
                        margin + stair->pz + stair->d,
                        stair->w + 4.0f * radius, 2.0f * radius,
                        player->floor);
    }
    if (stair_contains(room, player->floor, player->x - radius, player->z) < 0) {
        set_stair_limit(&limits[count++], stair,
                        stair->px - (2.0f * radius + margin),
                        stair->pz - 2.0f * radius,
                        2.0f * radius, stair->d + 4.0f * radius,
                        player->floor);
    }
    return count;
}

static int wall_applies_to_player(const RecvxPlayer *player,
                                  const RecvxPlayerWall *wall)
{
    uint32_t flags = PLAYER_COLLISION_FLAGS;
    uint32_t state_flags = PLAYER_COLLISION_STATE_FLAGS;
    if (wall->type == 7)
        return !(wall->attr & 1u) || wall->floor == player->floor;
    if (((wall->type & 1u) && (flags & 0x400u)) ||
        ((wall->attr & 1u) && wall->floor != player->floor) ||
        ((wall->attr & 2u) && (flags & 0x4000u)) ||
        ((wall->attr & 4u) && (state_flags & 0x40000000u)) ||
        ((wall->type == 2 || wall->type == 3) && (wall->attr & 0x40u) &&
         (state_flags & 0x4000u)))
        return 0;
    return 1;
}

static int wall_overlaps(const RecvxPlayer *player, const RecvxPlayerRoom *room,
                         const RecvxPlayerWall *wall, float x, float y, float z)
{
    float height = wall->h != 0.0f ? wall->h : room->wall_height;
    if (!wall->active || !wall_applies_to_player(player, wall))
        return 0;
    if (wall->type != 7) {
        if (y + PLAYER_HEIGHT < wall->py || y > wall->py + height)
            return 0;
    }

    switch (wall->type) {
    case 0:
    case 1:
        return circle_intersects_rectangle(x, z, wall->px, wall->pz,
                                           wall->w, wall->d, PLAYER_RADIUS);
    case 2:
    case 3: {
        float dx = x - wall->px, dz = z - wall->pz;
        float radius = PLAYER_RADIUS + wall->w;
        return dx * dx + dz * dz < radius * radius;
    }
    case 4:
    case 5: {
        float triangle[6];
        triangle_for_wall(wall, triangle);
        return circle_intersects_triangle(x, z, PLAYER_RADIUS, triangle);
    }
    case 6:
        return 0;
    case 7: {
        float edge_y = wall->py + height;
        int vertical_overlap = PLAYER_COLLISION_FLAGS & 0x100u ?
            y < edge_y && y + PLAYER_HEIGHT > edge_y :
            y + PLAYER_HEIGHT >= edge_y && y < wall->py;
        return vertical_overlap &&
               x >= wall->px - PLAYER_RADIUS && x < wall->px + wall->w + PLAYER_RADIUS &&
               z >= wall->pz - PLAYER_RADIUS && z < wall->pz + wall->d + PLAYER_RADIUS;
    }
    default:
        return 0;
    }
}

static int collides(const RecvxPlayer *player, const RecvxPlayerRoom *room,
                    const RecvxPlayerWall *stair_limits,
                    uint32_t stair_limit_count, float x, float y, float z)
{
    uint32_t i;
    for (i = 0; i < room->wall_count; ++i)
        if (wall_overlaps(player, room, &room->walls[i], x, y, z))
            return 1;
    for (i = 0; i < stair_limit_count; ++i)
        if (wall_overlaps(player, room, &stair_limits[i], x, y, z))
            return 1;
    return 0;
}

static int resolve_axis(const RecvxPlayer *player, const RecvxPlayerRoom *room,
                        const RecvxPlayerWall *stair_limits,
                        uint32_t stair_limit_count,
                        unsigned int axis, float candidate, float *resolved)
{
    float current = axis == 0 ? player->x : player->z;
    float clear = 0.0f, blocked = 1.0f;
    unsigned int iteration;
    for (iteration = 0; iteration < 8; ++iteration) {
        float fraction = (clear + blocked) * 0.5f;
        float value = current + (candidate - current) * fraction;
        float x = axis == 0 ? value : player->x;
        float z = axis == 0 ? player->z : value;
        if (collides(player, room, stair_limits, stair_limit_count,
                     x, player->y, z))
            blocked = fraction;
        else
            clear = fraction;
    }
    *resolved = current + (candidate - current) * clear;
    return 1;
}

static int ground_at(const RecvxPlayerRoom *room, float x, float z, float max_y,
                     float *ground_y)
{
    float nearest = -1000.0f;
    uint32_t i;
    int found = 0;
    for (i = 0; i < room->wall_count; ++i) {
        const RecvxPlayerWall *wall = &room->walls[i];
        float height, surface = -1000.0f;
        if (!wall->active)
            continue;
        height = wall->h != 0.0f ? wall->h : room->wall_height;
        switch (wall->type) {
        case 0:
            if (x >= wall->px && x < wall->px + wall->w &&
                z >= wall->pz && z < wall->pz + wall->d)
                surface = wall->py + height;
            break;
        case 2: {
            float dx = x - wall->px, dz = z - wall->pz;
            if (dx * dx + dz * dz < wall->w * wall->w)
                surface = wall->py + height;
            break;
        }
        case 4: {
            float triangle[6];
            triangle_for_wall(wall, triangle);
            if (point_in_triangle(x, z, triangle))
                surface = wall->py + height;
            break;
        }
        case 6:
            if (wall->w != 0.0f && wall->d != 0.0f &&
                x >= wall->px && x < wall->px + wall->w &&
                z >= wall->pz && z < wall->pz + wall->d) {
                switch (wall->id) {
                case 0:
                    surface = wall->py + (wall->h / wall->w) * (x - wall->px);
                    break;
                case 1:
                    surface = wall->h + wall->py +
                              (wall->h / wall->d) * (wall->pz - z);
                    break;
                case 2:
                    surface = wall->py + (wall->h / wall->d) * (z - wall->pz);
                    break;
                case 3:
                    surface = wall->h + wall->py +
                              (wall->h / wall->w) * (wall->px - x);
                    break;
                }
            }
            break;
        case 7:
            if (x >= wall->px && x < wall->px + wall->w &&
                z >= wall->pz && z < wall->pz + wall->d)
                surface = wall->py;
            break;
        }
        if (surface <= max_y && surface > nearest) {
            nearest = surface;
            found = 1;
        }
    }
    if (found)
        *ground_y = nearest;
    return found;
}

static int floor_number(const RecvxPlayerRoom *room, float y)
{
    int i, number = 2;
    for (i = 0; i < 31; ++i)
        if ((room->floor_heights[i] != 0.0f && y >= room->floor_heights[i]) ||
            (i == 2 && y >= room->floor_heights[i]))
            number = i;
    return number - 2;
}

static float player_floor_height(const RecvxPlayerRoom *room, int32_t floor)
{
    int32_t index = floor + 2;
    return index >= 0 && index < 32 ? room->floor_heights[index] : 0.0f;
}

static uint32_t player_cardinal_angle(const RecvxPlayer *player)
{
    int32_t angle = (int32_t)lrintf(player->yaw * (65536.0f / 6.283185307f));
    return ((uint32_t)angle + 8192u) & 0xc000u;
}

static int floor_target_direction_allowed(const RecvxPlayer *player,
                                          const RecvxPlayerFloor *floor)
{
    uint32_t angle = player_cardinal_angle(player);
    uint32_t direction_mask;
    switch (angle) {
    case 0x8000u:
        direction_mask = 0x400u;
        break;
    case 0x4000u:
        direction_mask = 0x800u;
        break;
    case 0:
        direction_mask = 0x1000u;
        break;
    default:
        direction_mask = 0x2000u;
        break;
    }
    return (floor->attr & direction_mask) == 0;
}

static int floor_target_contains(const RecvxPlayer *player,
                                 const RecvxPlayerFloor *floor)
{
    if (floor->attr & 1u) {
        float front_x = player->x - sinf(player->yaw) * PLAYER_FLOOR_TARGET_DISTANCE;
        float front_z = player->z - cosf(player->yaw) * PLAYER_FLOOR_TARGET_DISTANCE;
        return floor->floor == player->floor &&
               front_x >= floor->px && front_x <= floor->px + floor->w &&
               front_z >= floor->pz && front_z <= floor->pz + floor->d;
    }
    return player->x >= floor->px && player->x <= floor->px + floor->w &&
           player->y >= floor->py && player->y <= floor->py + floor->h &&
           player->z >= floor->pz && player->z <= floor->pz + floor->d;
}

static void update_floor_attributes(RecvxPlayer *player,
                                    const RecvxPlayerRoom *room)
{
    uint32_t i;
    player->surface_flags = 0;
    player->floor_sound = 0;
    player->foot_effect = 0;
    player->floor_target_index = -1;
    for (i = 0; i < room->floor_count; ++i) {
        const RecvxPlayerFloor *floor = &room->floors[i];
        float min_x, max_x, min_y, max_y, min_z, max_z;
        if (!floor->active)
            continue;
        if (floor->type == 0) {
            if (!(player->game_flags & 0x6u) &&
                floor_target_contains(player, floor) &&
                floor_target_direction_allowed(player, floor))
                player->floor_target_index = (int32_t)i;
            continue;
        }
        min_x = fminf(floor->px, floor->px + floor->w);
        max_x = fmaxf(floor->px, floor->px + floor->w);
        min_y = fminf(floor->py, floor->py + floor->h);
        max_y = fmaxf(floor->py, floor->py + floor->h);
        min_z = fminf(floor->pz, floor->pz + floor->d);
        max_z = fmaxf(floor->pz, floor->pz + floor->d);
        if (player->x < min_x || player->x > max_x ||
            player->y < min_y || player->y > max_y ||
            player->z < min_z || player->z > max_z)
            continue;
        if (floor->type == 1 && !(floor->attr & 1u))
            player->floor_sound = floor->parameters[0];
        else if (floor->type == 3) {
            switch (floor->parameters[0]) {
            case 1:
                player->surface_flags |= RECVX_PLAYER_SURFACE_SLOW;
                break;
            case 3:
                player->surface_flags |= RECVX_PLAYER_SURFACE_WATER;
                player->foot_effect = 2;
                break;
            case 4:
                player->surface_flags |= RECVX_PLAYER_SURFACE_HAZARD;
                break;
            }
        }
    }
}

static void clear_stair_transition(RecvxPlayer *player)
{
    player->stair_transition_index = -1;
    player->stair_target_floor = -1;
    player->stair_motion_slot = -1;
    player->stair_motion_frame = 0;
    player->stair_stop_frame = 0;
    player->stair_recovery_ticks = 0;
    player->stair_root_position_valid = 0;
    player->stair_phase = RECVX_PLAYER_STAIR_NONE;
}

static const RecvxPlayerMotionAsset *stair_motion(
    const RecvxPlayerAssets *assets, int32_t slot)
{
    const RecvxPlayerMotionAsset *motion;
    if (assets == NULL || slot < 0 || (uint32_t)slot >= assets->motion_count)
        return NULL;
    motion = &assets->motions[slot];
    return motion->present && motion->frame_count != 0 ? motion : NULL;
}

int recvxPlayerTryBeginStair(RecvxPlayer *player,
                             const RecvxPlayerRoom *room,
                             const RecvxPlayerAssets *assets)
{
    uint32_t i;
    float front_x, front_z;
    if (player == NULL || room == NULL || assets == NULL ||
        player->stair_phase != RECVX_PLAYER_STAIR_NONE)
        return 0;
    front_x = player->x - sinf(player->yaw) * 0.75f;
    front_z = player->z - cosf(player->yaw) * 0.75f;
    for (i = 0; i < room->stair_transition_count; ++i) {
        const RecvxPlayerWall *transition = &room->stair_transitions[i];
        unsigned int steps, motion_slot, stop_frame;
        int ascending;
        float target_yaw;
        int32_t target_floor;
        if (!transition->active || transition->type != 1 ||
            transition->floor != player->floor || (transition->attr & 1u))
            continue;
        if (front_x < fminf(transition->px, transition->px + transition->w) ||
            front_x > fmaxf(transition->px, transition->px + transition->w) ||
            front_z < fminf(transition->pz, transition->pz + transition->d) ||
            front_z > fmaxf(transition->pz, transition->pz + transition->d))
            continue;
        steps = transition->parameters[2];
        ascending = transition->parameters[0] == 0;
        if (steps == 0 || transition->parameters[1] > 3)
            continue;
        stop_frame = (steps % 4u) == 0 ? 19u : 6u;
        motion_slot = ascending ? ((steps % 4u) == 0 ? 35u : 31u) :
                                   ((steps % 4u) == 0 ? 36u : 32u);
        if (stair_motion(assets, motion_slot) == NULL ||
            stair_motion(assets, 39) == NULL ||
            stair_motion(assets, 0) == NULL ||
            stair_motion(assets, motion_slot)->frame_count <= stop_frame)
            continue;
        target_yaw = (ascending ? 0.0f : PLAYER_PI) -
                     (float)transition->parameters[1] * (PLAYER_PI * 0.5f);
        target_yaw = fmodf(target_yaw, PLAYER_TAU);
        if (target_yaw < 0.0f)
            target_yaw += PLAYER_TAU;
        target_floor = floor_number(room, player->y +
                                    (ascending ? 2.0f : -2.0f) * steps);
        if (target_floor == player->floor)
            continue;
        player->stair_transition_index = (int32_t)i;
        player->stair_target_floor = target_floor;
        player->stair_motion_slot = (int32_t)motion_slot;
        player->stair_motion_frame = 0;
        player->stair_stop_frame = stop_frame;
        player->stair_recovery_ticks = 10;
        player->stair_target_yaw = target_yaw;
        player->stair_target_y = player_floor_height(room, target_floor);
        player->stair_root_position_valid = 0;
        player->stair_phase = RECVX_PLAYER_STAIR_TURN;
        player->stair_index = -1;
        return 1;
    }
    return 0;
}

int recvxPlayerAdvanceStair(RecvxPlayer *player,
                            const RecvxPlayerRoom *room,
                            const RecvxPlayerAssets *assets)
{
    const RecvxPlayerMotionAsset *motion;
    float difference, step;
    if (player == NULL || room == NULL || assets == NULL ||
        player->stair_phase == RECVX_PLAYER_STAIR_NONE)
        return 0;
    if (player->stair_phase == RECVX_PLAYER_STAIR_TURN) {
        difference = fmodf(player->stair_target_yaw - player->yaw + PLAYER_PI,
                           PLAYER_TAU);
        if (difference < 0.0f)
            difference += PLAYER_TAU;
        difference -= PLAYER_PI;
        step = 0.125663706f;
        if (fabsf(difference) <= step) {
            player->yaw = player->stair_target_yaw;
            player->stair_phase = RECVX_PLAYER_STAIR_MOVE;
            player->stair_motion_frame = 0;
            motion = stair_motion(assets, player->stair_motion_slot);
            if (motion == NULL ||
                recvxPlayerMotionPositionAt(motion, 0, 0,
                                            player->stair_last_motion) != 1) {
                clear_stair_transition(player);
                return 1;
            }
            player->stair_root_position_valid = 1;
        } else {
            player->yaw += copysignf(step, difference);
            player->yaw = fmodf(player->yaw, PLAYER_TAU);
            if (player->yaw < 0.0f)
                player->yaw += PLAYER_TAU;
        }
        return 1;
    }
    if (player->stair_phase == RECVX_PLAYER_STAIR_MOVE) {
        float position[3], dx, dy, dz, sine, cosine;
        uint32_t next_frame = player->stair_motion_frame + 1;
        motion = stair_motion(assets, player->stair_motion_slot);
        if (motion == NULL) {
            clear_stair_transition(player);
            return 1;
        }
        if (next_frame > player->stair_stop_frame)
            next_frame = player->stair_stop_frame;
        if (recvxPlayerMotionPositionAt(motion, 0, next_frame, position) != 1) {
            clear_stair_transition(player);
            return 1;
        }
        if (!player->stair_root_position_valid) {
            if (recvxPlayerMotionPositionAt(motion, 0, player->stair_motion_frame,
                                            player->stair_last_motion) != 1) {
                clear_stair_transition(player);
                return 1;
            }
            player->stair_root_position_valid = 1;
        }
        dx = position[0] - player->stair_last_motion[0];
        dy = position[1] - player->stair_last_motion[1];
        dz = position[2] - player->stair_last_motion[2];
        sine = sinf(player->yaw);
        cosine = cosf(player->yaw);
        player->x += cosine * dx + sine * dz;
        player->y += dy;
        player->z += -sine * dx + cosine * dz;
        memcpy(player->stair_last_motion, position,
               sizeof(player->stair_last_motion));
        player->stair_motion_frame = next_frame;
        if (player->stair_motion_frame >= player->stair_stop_frame) {
            player->stair_phase = RECVX_PLAYER_STAIR_RECOVER;
            player->stair_recovery_ticks = 10;
            player->stair_motion_frame = 15;
        }
        return 1;
    }
    if (player->stair_phase == RECVX_PLAYER_STAIR_RECOVER) {
        if (player->stair_recovery_ticks != 0)
            --player->stair_recovery_ticks;
        if (player->stair_recovery_ticks == 0) {
            player->floor = player->stair_target_floor;
            player->y = player->stair_target_y;
            player->vertical_speed = 0.0f;
            player->grounded = 1;
            player->stair_index = -1;
            update_floor_attributes(player, room);
            clear_stair_transition(player);
        }
        return 1;
    }
    clear_stair_transition(player);
    return 1;
}

static void read_player_attribute(const unsigned char *source,
                                 RecvxPlayerWall *attribute)
{
    attribute->active = source[offsetof(RecvxRoomDiskAttribute, flags)] & 1;
    attribute->type = source[offsetof(RecvxRoomDiskAttribute, type)];
    attribute->id = source[offsetof(RecvxRoomDiskAttribute, id)];
    attribute->floor = (int8_t)source[offsetof(RecvxRoomDiskAttribute, floor)];
    attribute->attr = read_u32(source +
        offsetof(RecvxRoomDiskAttribute, attributes));
    attribute->px = read_f32(source + offsetof(RecvxRoomDiskAttribute, x));
    attribute->py = read_f32(source + offsetof(RecvxRoomDiskAttribute, y));
    attribute->pz = read_f32(source + offsetof(RecvxRoomDiskAttribute, z));
    attribute->w = read_f32(source + offsetof(RecvxRoomDiskAttribute, width));
    attribute->h = read_f32(source + offsetof(RecvxRoomDiskAttribute, height));
    attribute->d = read_f32(source + offsetof(RecvxRoomDiskAttribute, depth));
    memcpy(attribute->parameters,
           source + offsetof(RecvxRoomDiskAttribute, parameters),
           sizeof(attribute->parameters));
}

int recvxPlayerRoomLoad(const void *data, size_t size, RecvxPlayerRoom *room)
{
    const unsigned char *bytes = data;
    RecvxRoomInfo info;
    RecvxPlayerWall *walls = NULL, *floors = NULL, *stairs = NULL;
    RecvxPlayerWall *stair_transitions = NULL;
    RecvxPlayerDoor *doors = NULL;
    uint32_t wall_offset, floor_offset, etc_offset;
    uint32_t door_count = 0, door_capacity = 0, stair_count = 0;
    uint32_t stair_capacity = 0, stair_transition_count = 0;
    uint32_t stair_transition_capacity = 0, i;
    if (room == NULL || recvxInspectRoom(data, size, &info) != 0)
        return -1;
    memset(room, 0, sizeof(*room));
    if (info.counts[RECVX_ROOM_COUNT_WALL] > 4096 ||
        info.counts[RECVX_ROOM_COUNT_ETC] > 4096 ||
        info.counts[RECVX_ROOM_COUNT_FLOOR] > 4096)
        return -2;
    wall_offset = read_u32(bytes + info.rom_offset +
                           RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_WALL));
    if (info.counts[RECVX_ROOM_COUNT_WALL] != 0) {
        walls = calloc(info.counts[RECVX_ROOM_COUNT_WALL], sizeof(*walls));
        if (walls == NULL)
            return -3;
    }
    for (i = 0; i < info.counts[RECVX_ROOM_COUNT_WALL]; ++i) {
        const unsigned char *source = bytes + wall_offset +
                                      i * sizeof(RecvxRoomDiskAttribute);
        read_player_attribute(source, &walls[i]);
    }
    floor_offset = read_u32(bytes + info.rom_offset +
                            RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_FLOOR));
    if (info.counts[RECVX_ROOM_COUNT_FLOOR] != 0) {
        floors = calloc(info.counts[RECVX_ROOM_COUNT_FLOOR], sizeof(*floors));
        if (floors == NULL) {
            free(walls);
            return -3;
        }
    }
    for (i = 0; i < info.counts[RECVX_ROOM_COUNT_FLOOR]; ++i) {
        const unsigned char *source = bytes + floor_offset +
                                      i * sizeof(RecvxRoomDiskAttribute);
        read_player_attribute(source, &floors[i]);
    }
    etc_offset = read_u32(bytes + info.rom_offset +
                          RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_ETC));
    for (i = 0; i < info.counts[RECVX_ROOM_COUNT_ETC]; ++i) {
        const unsigned char *source = bytes + etc_offset +
                                      i * sizeof(RecvxRoomDiskAttribute);
        if (!(source[offsetof(RecvxRoomDiskAttribute, flags)] & 1))
            continue;
        if (source[offsetof(RecvxRoomDiskAttribute, type)] == 0)
            ++door_capacity;
        else if (source[offsetof(RecvxRoomDiskAttribute, type)] == 2 &&
                 source[offsetof(RecvxRoomDiskAttribute, parameters)] != 0)
            ++stair_capacity;
        else if (source[offsetof(RecvxRoomDiskAttribute, type)] == 1)
            ++stair_transition_capacity;
    }
    if (door_capacity != 0) {
        doors = calloc(door_capacity, sizeof(*doors));
        if (doors == NULL) {
            free(walls);
            free(floors);
            return -3;
        }
    }
    if (stair_capacity != 0) {
        stairs = calloc(stair_capacity, sizeof(*stairs));
        if (stairs == NULL) {
            free(walls);
            free(floors);
            free(doors);
            return -3;
        }
    }
    if (stair_transition_capacity != 0) {
        stair_transitions = calloc(stair_transition_capacity,
                                   sizeof(*stair_transitions));
        if (stair_transitions == NULL) {
            free(walls);
            free(floors);
            free(doors);
            free(stairs);
            return -3;
        }
    }
    for (i = 0; i < info.counts[RECVX_ROOM_COUNT_ETC]; ++i) {
        const unsigned char *source = bytes + etc_offset +
                                      i * sizeof(RecvxRoomDiskAttribute);
        RecvxPlayerDoor *door;
        if (!(source[offsetof(RecvxRoomDiskAttribute, flags)] & 1))
            continue;
        if (source[offsetof(RecvxRoomDiskAttribute, type)] == 2 &&
            source[offsetof(RecvxRoomDiskAttribute, parameters)] != 0) {
            read_player_attribute(source, &stairs[stair_count++]);
            continue;
        }
        if (source[offsetof(RecvxRoomDiskAttribute, type)] == 1) {
            read_player_attribute(source,
                                  &stair_transitions[stair_transition_count++]);
            continue;
        }
        if (source[offsetof(RecvxRoomDiskAttribute, type)] != 0)
            continue;
        door = &doors[door_count++];
        door->id = source[offsetof(RecvxRoomDiskAttribute, id)];
        door->floor = (int8_t)source[offsetof(RecvxRoomDiskAttribute, floor)];
        door->attr = read_u32(source + offsetof(RecvxRoomDiskAttribute, attributes));
        door->x = read_f32(source + offsetof(RecvxRoomDiskAttribute, x));
        door->y = read_f32(source + offsetof(RecvxRoomDiskAttribute, y));
        door->z = read_f32(source + offsetof(RecvxRoomDiskAttribute, z));
        door->width = read_f32(source + offsetof(RecvxRoomDiskAttribute, width));
        door->height = read_f32(source + offsetof(RecvxRoomDiskAttribute, height));
        door->depth = read_f32(source + offsetof(RecvxRoomDiskAttribute, depth));
        door->target_stage = source[offsetof(RecvxRoomDiskAttribute, parameters)];
        door->target_room = source[offsetof(RecvxRoomDiskAttribute, parameters) + 1];
        door->target_pos = source[offsetof(RecvxRoomDiskAttribute, parameters) + 2];
        door->door_type = source[offsetof(RecvxRoomDiskAttribute, parameters) + 3];
    }
    room->walls = walls;
    room->wall_count = info.counts[RECVX_ROOM_COUNT_WALL];
    room->doors = doors;
    room->door_count = door_count;
    room->wall_height = read_f32(bytes + info.rom_offset +
                                 offsetof(RecvxRoomDiskWork, wall_height));
    room->floors = floors;
    room->floor_count = info.counts[RECVX_ROOM_COUNT_FLOOR];
    room->stairs = stairs;
    room->stair_count = stair_count;
    room->stair_transitions = stair_transitions;
    room->stair_transition_count = stair_transition_count;
    for (i = 0; i < 32; ++i)
        room->floor_heights[i] = read_f32(bytes + info.rom_offset +
                                          offsetof(RecvxRoomDiskWork, floor_heights) +
                                          i * sizeof(float));
    return 0;
}

void recvxPlayerRoomFree(RecvxPlayerRoom *room)
{
    if (room == NULL)
        return;
    free(room->walls);
    free(room->doors);
    free(room->floors);
    free(room->stairs);
    free(room->stair_transitions);
    memset(room, 0, sizeof(*room));
}

int recvxPlayerSpawn(RecvxPlayer *player, const RecvxPlayerRoom *room,
                     const void *data, size_t size, uint32_t position_index)
{
    const unsigned char *bytes = data;
    RecvxRoomInfo info;
    uint32_t offset;
    if (player == NULL || room == NULL || recvxInspectRoom(data, size, &info) != 0 ||
        position_index >= info.counts[RECVX_ROOM_COUNT_POSITION])
        return -1;
    offset = read_u32(bytes + info.rom_offset +
                      RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_POSITION)) +
             position_index * sizeof(RecvxRoomDiskPosition);
    memset(player, 0, sizeof(*player));
    player->x = read_f32(bytes + offset + offsetof(RecvxRoomDiskPosition, x));
    player->y = read_f32(bytes + offset + offsetof(RecvxRoomDiskPosition, y));
    player->z = read_f32(bytes + offset + offsetof(RecvxRoomDiskPosition, z));
    player->yaw = (int32_t)read_u32(bytes + offset + offsetof(RecvxRoomDiskPosition, yaw)) *
                  (6.283185307f / 65536.0f);
    player->floor = floor_number(room, player->y);
    player->y = player_floor_height(room, player->floor);
    player->grounded = 1;
    player->stair_index = -1;
    player->stair_transition_index = -1;
    player->stair_target_floor = -1;
    player->stair_motion_slot = -1;
    update_floor_attributes(player, room);
    return 0;
}

int recvxPlayerDoorAt(const RecvxPlayer *player, const RecvxPlayerRoom *room,
                      RecvxPlayerDoor *door)
{
    uint32_t i;
    if (player == NULL || room == NULL || door == NULL)
        return 0;
    for (i = 0; i < room->door_count; ++i) {
        const RecvxPlayerDoor *candidate = &room->doors[i];
        float x0 = fminf(candidate->x, candidate->x + candidate->width);
        float x1 = fmaxf(candidate->x, candidate->x + candidate->width);
        float z0 = fminf(candidate->z, candidate->z + candidate->depth);
        float z1 = fmaxf(candidate->z, candidate->z + candidate->depth);
        if (candidate->floor != player->floor || player->x < x0 || player->x > x1 ||
            player->z < z0 || player->z > z1)
            continue;
        if (candidate->height != 0.0f &&
            (player->y + PLAYER_HEIGHT < candidate->y ||
             player->y > candidate->y + candidate->height))
            continue;
        *door = *candidate;
        return 1;
    }
    return 0;
}

void recvxPlayerUpdateAtSpeed(RecvxPlayer *player, const RecvxPlayerRoom *room,
                              float forward, float turn, float speed,
                              float turn_speed, float dt)
{
    float x, z, ground;
    RecvxPlayerWall stair_limits[4];
    uint32_t stair_limit_count;
    if (player == NULL || room == NULL)
        return;
    dt = fmaxf(0.0f, fminf(0.05f, dt));
    forward = fmaxf(-1.0f, fminf(1.0f, forward));
    turn = fmaxf(-1.0f, fminf(1.0f, turn));
    player->blocked = 0;
    update_floor_attributes(player, room);
    player->yaw = fmodf(player->yaw - turn * turn_speed * dt,
                        6.283185307f);
    if (player->yaw < 0.0f)
        player->yaw += 6.283185307f;
    if (player->surface_flags & RECVX_PLAYER_SURFACE_SLOW)
        speed *= PLAYER_SLOW_SURFACE_SCALE;
    stair_limit_count = build_stair_limits(player, room, stair_limits);
    x = player->x - sinf(player->yaw) * forward * speed * dt;
    z = player->z - cosf(player->yaw) * forward * speed * dt;
    if (forward != 0.0f)
        player->walk_phase += fabsf(forward) * dt * 7.0f;
    player->walk_phase = fmodf(player->walk_phase, 6.283185307f);
    if (!collides(player, room, stair_limits, stair_limit_count,
                  x, player->y, player->z))
        player->x = x;
    else {
        float resolved;
        player->blocked = 1;
        if (resolve_axis(player, room, stair_limits, stair_limit_count,
                         0, x, &resolved))
            player->x = resolved;
    }
    if (!collides(player, room, stair_limits, stair_limit_count,
                  player->x, player->y, z))
        player->z = z;
    else {
        float resolved;
        player->blocked = 1;
        if (resolve_axis(player, room, stair_limits, stair_limit_count,
                         1, z, &resolved))
            player->z = resolved;
    }

    if (player->grounded) {
        float floor_height = player_floor_height(room, player->floor);
        if (ground_at(room, player->x, player->z,
                      player->y + PLAYER_STEP_HEIGHT, &ground) &&
            ground >= floor_height - 0.02f &&
            ground >= player->y - PLAYER_STEP_HEIGHT)
            player->y = ground;
        else
            player->y = floor_height;
        player->vertical_speed = 0.0f;
    }
    if (!player->grounded) {
        float old_y = player->y;
        player->vertical_speed -= PLAYER_GRAVITY * dt;
        player->y += player->vertical_speed * dt;
        if (ground_at(room, player->x, player->z, old_y + 0.001f, &ground) &&
            player->y <= ground) {
            player->y = ground;
            player->vertical_speed = 0.0f;
            player->grounded = 1;
        }
    }
    player->floor = floor_number(room, player->y);
    update_floor_attributes(player, room);
}

void recvxPlayerUpdate(RecvxPlayer *player, const RecvxPlayerRoom *room,
                       float forward, float turn, float dt)
{
    recvxPlayerUpdateAtSpeed(player, room, forward, turn,
                            PLAYER_DIAGNOSTIC_WALK_SPEED,
                            PLAYER_DIAGNOSTIC_TURN_SPEED, dt);
}
