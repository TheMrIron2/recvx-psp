#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "psp/player.h"
#include "psp/abi.h"
#include "psp/room.h"

#define PLAYER_RADIUS 0.28f
#define PLAYER_HEIGHT 1.8f
#define PLAYER_STEP_HEIGHT 0.45f
#define PLAYER_WALK_SPEED 4.0f
#define PLAYER_TURN_SPEED 2.4f
#define PLAYER_GRAVITY 9.8f

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

static int inside_rectangle(float x, float z, float px, float pz,
                            float width, float depth, float radius)
{
    float min_x = fminf(px, px + width);
    float max_x = fmaxf(px, px + width);
    float min_z = fminf(pz, pz + depth);
    float max_z = fmaxf(pz, pz + depth);
    float nearest_x = fmaxf(min_x, fminf(max_x, x));
    float nearest_z = fmaxf(min_z, fminf(max_z, z));
    float dx = x - nearest_x, dz = z - nearest_z;
    return dx * dx + dz * dz <= radius * radius;
}

static int wall_overlaps(const RecvxPlayer *player, const RecvxPlayerRoom *room,
                         const RecvxPlayerWall *wall, float x, float y, float z)
{
    float height = wall->h != 0.0f ? wall->h : room->wall_height;
    if (!wall->active || ((wall->attr & 1u) && wall->floor != player->floor) ||
        y + PLAYER_HEIGHT < wall->py || y > wall->py + height)
        return 0;

    switch (wall->type) {
    case 0:
    case 1:
        return inside_rectangle(x, z, wall->px, wall->pz, wall->w, wall->d,
                                PLAYER_RADIUS);
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
        return x >= wall->px && x < wall->px + wall->w &&
               z >= wall->pz && z < wall->pz + wall->d;
    case 7: {
        float edge_y = wall->py + wall->h;
        int vertical_overlap = y + PLAYER_HEIGHT >= edge_y && y < wall->py;
        return vertical_overlap &&
               x >= wall->px - PLAYER_RADIUS && x < wall->px + wall->w + PLAYER_RADIUS &&
               z >= wall->pz - PLAYER_RADIUS && z < wall->pz + wall->d + PLAYER_RADIUS;
    }
    default:
        return 0;
    }
}

static int collides(const RecvxPlayer *player, const RecvxPlayerRoom *room,
                    float x, float y, float z)
{
    uint32_t i;
    for (i = 0; i < room->wall_count; ++i)
        if (wall_overlaps(player, room, &room->walls[i], x, y, z))
            return 1;
    return 0;
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
        case 4:
        case 5: {
            float triangle[6];
            triangle_for_wall(wall, triangle);
            if (point_in_triangle(x, z, triangle))
                surface = wall->py + height;
            break;
        }
        case 6:
            if (x >= wall->px && x < wall->px + wall->w &&
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

int recvxPlayerRoomLoad(const void *data, size_t size, RecvxPlayerRoom *room)
{
    const unsigned char *bytes = data;
    RecvxRoomInfo info;
    RecvxPlayerWall *walls = NULL;
    RecvxPlayerDoor *doors = NULL;
    uint32_t wall_offset, etc_offset, door_count = 0, door_capacity = 0, i;
    if (room == NULL || recvxInspectRoom(data, size, &info) != 0)
        return -1;
    memset(room, 0, sizeof(*room));
    if (info.counts[RECVX_ROOM_COUNT_WALL] > 4096 ||
        info.counts[RECVX_ROOM_COUNT_ETC] > 4096)
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
        RecvxPlayerWall *wall = &walls[i];
        wall->active = source[offsetof(RecvxRoomDiskAttribute, flags)] & 1;
        wall->type = source[offsetof(RecvxRoomDiskAttribute, type)];
        wall->id = source[offsetof(RecvxRoomDiskAttribute, id)];
        wall->floor = (uint8_t)source[offsetof(RecvxRoomDiskAttribute, floor)];
        wall->attr = read_u32(source + offsetof(RecvxRoomDiskAttribute, attributes));
        wall->px = read_f32(source + offsetof(RecvxRoomDiskAttribute, x));
        wall->py = read_f32(source + offsetof(RecvxRoomDiskAttribute, y));
        wall->pz = read_f32(source + offsetof(RecvxRoomDiskAttribute, z));
        wall->w = read_f32(source + offsetof(RecvxRoomDiskAttribute, width));
        wall->h = read_f32(source + offsetof(RecvxRoomDiskAttribute, height));
        wall->d = read_f32(source + offsetof(RecvxRoomDiskAttribute, depth));
    }
    etc_offset = read_u32(bytes + info.rom_offset +
                          RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_ETC));
    for (i = 0; i < info.counts[RECVX_ROOM_COUNT_ETC]; ++i) {
        const unsigned char *source = bytes + etc_offset +
                                      i * sizeof(RecvxRoomDiskAttribute);
        if ((source[offsetof(RecvxRoomDiskAttribute, flags)] & 1) &&
            source[offsetof(RecvxRoomDiskAttribute, type)] == 0)
            ++door_capacity;
    }
    if (door_capacity != 0) {
        doors = calloc(door_capacity, sizeof(*doors));
        if (doors == NULL) {
            free(walls);
            return -3;
        }
    }
    for (i = 0; i < info.counts[RECVX_ROOM_COUNT_ETC]; ++i) {
        const unsigned char *source = bytes + etc_offset +
                                      i * sizeof(RecvxRoomDiskAttribute);
        RecvxPlayerDoor *door;
        if (!(source[offsetof(RecvxRoomDiskAttribute, flags)] & 1) ||
            source[offsetof(RecvxRoomDiskAttribute, type)] != 0)
            continue;
        door = &doors[door_count++];
        door->id = source[offsetof(RecvxRoomDiskAttribute, id)];
        door->floor = (uint8_t)source[offsetof(RecvxRoomDiskAttribute, floor)];
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
    player->grounded = 1;
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

void recvxPlayerUpdate(RecvxPlayer *player, const RecvxPlayerRoom *room,
                       float forward, float turn, float dt)
{
    float x, z, ground;
    if (player == NULL || room == NULL)
        return;
    dt = fmaxf(0.0f, fminf(0.05f, dt));
    forward = fmaxf(-1.0f, fminf(1.0f, forward));
    turn = fmaxf(-1.0f, fminf(1.0f, turn));
    player->blocked = 0;
    player->yaw = fmodf(player->yaw - turn * PLAYER_TURN_SPEED * dt,
                        6.283185307f);
    if (player->yaw < 0.0f)
        player->yaw += 6.283185307f;
    x = player->x - sinf(player->yaw) * forward * PLAYER_WALK_SPEED * dt;
    z = player->z - cosf(player->yaw) * forward * PLAYER_WALK_SPEED * dt;
    if (forward != 0.0f)
        player->walk_phase += fabsf(forward) * dt * 7.0f;
    player->walk_phase = fmodf(player->walk_phase, 6.283185307f);
    if (!collides(player, room, x, player->y, player->z))
        player->x = x;
    else
        player->blocked = 1;
    if (!collides(player, room, player->x, player->y, z))
        player->z = z;
    else
        player->blocked = 1;

    if (player->grounded && ground_at(room, player->x, player->z,
                                      player->y + PLAYER_STEP_HEIGHT, &ground)) {
        if (ground >= player->y - 0.02f) {
            player->y = ground;
            player->vertical_speed = 0.0f;
        } else {
            player->grounded = 0;
        }
    } else {
        player->grounded = 0;
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
}
