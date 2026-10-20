#include <math.h>
#include <limits.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_render.h"
#include "psp/game_boot_chunk.h"
#include "psp/game_boot_render_texture.h"
#include "psp/game_boot_fog.h"
#include "psp/game_boot_vector.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_lighting.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_view.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"
#include "ps2/veronica/prog/ps2_dummy.h"
#include "ps2/veronica/prog/ps2_NaView.h"

#define MAX_VERTICES 262144u
#define MAX_BATCHES 16384u
extern int recvxGameBootCaptureActive(void) __attribute__((weak));
extern void recvxGameBootDrawDiscard(void) __attribute__((weak));
extern unsigned int ulCnkCurrentDrawMode;
static RecvxBootRenderVertex *vertices;
static RecvxBootRenderBatch *batches;
static size_t vertex_capacity, batch_capacity, vertex_count, batch_count;
static int shadow_active;
static RecvxBootRenderStats render_stats;
RecvxBootRenderStats recvxGameBootRenderStats(void){return render_stats;}
static int user_scissor[4] = {0,0,480,272};

void njUserClipping(Int mode, NJS_POINT2 *points)
{
    if (mode == 0) {
        const int full[4] = {0,0,480,272};
        memcpy(user_scissor, full, sizeof(full));
        return;
    }
    if (mode != 2 || !points || !isfinite(points[0].x) || !isfinite(points[0].y) ||
        !isfinite(points[1].x) || !isfinite(points[1].y) || points[0].x < 0 || points[0].y < 0 ||
        points[1].x < points[0].x || points[1].y < points[0].y ||
        points[1].x > 640.0f/32 || points[1].y > 480.0f/32)
        recvxGameBootUnsupported("invalid original user clipping rectangle");
    /* Retail receives coordinates divided by32; the inclusive GS maximum
       expands by one32-pixel unit before conversion to PSP pixels. */
    user_scissor[0] = (int)(points[0].x*32*0.75f);
    user_scissor[1] = (int)(points[0].y*32*(272.0f/480));
    user_scissor[2] = (int)fminf(480, (points[1].x*32+33)*0.75f);
    user_scissor[3] = (int)fminf(272, (points[1].y*32+33)*(272.0f/480));
}
static RecvxBootTexture *texture_snapshots[256];
static size_t texture_snapshot_count;
static RecvxBootRenderFrame frame __attribute__((aligned(16)));

_Static_assert(sizeof(RecvxBootRenderVertex) == 24, "PSP GU vertex packing");

void recvxGameBootRenderBeginFrame(void)
{
    for (size_t i = 0; i < texture_snapshot_count; ++i) free(texture_snapshots[i]);
    texture_snapshot_count = 0;
    vertex_count = batch_count = 0; shadow_active = 0;
}
void recvxGameBootRenderConsume(void) { recvxGameBootRenderBeginFrame(); }
void Ps2ClearOT(void)
{
    /* Retail clears the ordering table and rewinds its primitive buffer.
       PSP equivalents are the pending 2D/3D queues, not loaded textures or
       render state. Present synchronizes GU before returning to the engine.
       Discard 2D users before freeing shared frame-owned palette snapshots. */
    if (recvxGameBootDrawDiscard) recvxGameBootDrawDiscard();
    recvxGameBootRenderBeginFrame();
}
int recvxGameBootRenderPending(void) { return vertex_count != 0; }
const RecvxBootTexture *recvxGameBootRenderSnapshotTexture(const RecvxBootTexture *texture)
{
    if (!texture || !texture->source || !texture->pixels)
        recvxGameBootUnsupported("invalid original palette snapshot texture");
    if (texture_snapshot_count == sizeof(texture_snapshots) / sizeof(texture_snapshots[0]))
        recvxGameBootUnsupported("original palette snapshot capacity exceeded");
    RecvxBootTexture *snapshot = memalign(16, sizeof(*snapshot));
    if (!snapshot) recvxGameBootUnsupported("original palette snapshot allocation failed");
    *snapshot = *texture;
    texture_snapshots[texture_snapshot_count++] = snapshot;
    for (size_t i = 0; i < batch_count; ++i)
        if (batches[i].texture == texture) batches[i].texture = snapshot;
    return snapshot;
}
void recvxGameBootRenderReset(void)
{
    /* A fault may leave 2D overlays pointing into the same palette views.
       Discard those draws before freeing their frame-owned snapshots. */
    if (recvxGameBootDrawDiscard) recvxGameBootDrawDiscard();
    memset(&render_stats,0,sizeof(render_stats));
    free(vertices); free(batches); vertices = NULL; batches = NULL;
    vertex_capacity = batch_capacity = 0;
    memset(&frame, 0, sizeof(frame));
    njUserClipping(0, NULL);
    recvxGameBootRenderBeginFrame();
    recvxGameBootRenderTextureReset(); recvxGameBootChunkReset();
}

static int owned(const void *pointer, size_t bytes)
{
    if (!sys || !freemem) return 0;
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    return pointer && !(p & 3u) && p >= first && p <= end && bytes <= end - p;
}
static void checked_math(void)
{
    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());
}
static void reserve(size_t count)
{
    if (count > MAX_VERTICES || vertex_count > MAX_VERTICES - count || batch_count >= MAX_BATCHES)
        recvxGameBootUnsupported("original 3D draw queue exceeds capacity");
    if (vertex_count + count > vertex_capacity) {
        size_t capacity = vertex_capacity ? vertex_capacity * 2 : 4096;
        void *memory;
        if (capacity > MAX_VERTICES) capacity = MAX_VERTICES;
        memory = realloc(vertices, capacity * sizeof(*vertices));
        if (!memory) recvxGameBootUnsupported("original 3D vertex allocation failed");
        vertices = memory; vertex_capacity = capacity;
    }
    if (batch_count + 1 > batch_capacity) {
        size_t capacity = batch_capacity ? batch_capacity * 2 : 128;
        void *memory = realloc(batches, capacity * sizeof(*batches));
        if (!memory) recvxGameBootUnsupported("original 3D batch allocation failed");
        batches = memory; batch_capacity = capacity;
    }
}

static void capture_projection(void)
{
    float near, far, ax, ay, distance = recvxEngineOriginalViewProjectionDistance();
    recvxEngineOriginalViewClipPlanes(&near, &far);
    recvxEngineOriginalViewAspect(&ax, &ay);
    near = -near; far = -far;
    if (!isfinite(distance) || !(distance > 0) || !(near > 0) || !(far > near))
        recvxGameBootUnsupported("invalid original 3D projection");
    memset(frame.projection, 0, sizeof(frame.projection));
    int capture=recvxGameBootCaptureActive&&recvxGameBootCaptureActive();
    float width=capture?512.0f:640.0f,height=capture?_nj_screen_.h:480.0f;
    frame.projection[0] = 2.0f * distance * ax / width;
    frame.projection[5] = 2.0f * distance * ay / height;
    frame.projection[8] = (1728.0f + width*0.5f - fNaViwOffsetX) * 2 / width;
    frame.projection[9] = (fNaViwOffsetY - 1808.0f - height*0.5f) * 2 / height;
    frame.projection[10] = -(far + near) / (far - near);
    frame.projection[11] = -1;
    frame.projection[14] = -(2.0f * far * near) / (far - near);
}

const RecvxBootRenderFrame *recvxGameBootRenderFrame(void)
{
    frame.vertices = vertices; frame.batches = batches;
    frame.vertex_count = vertex_count; frame.batch_count = batch_count;
    return &frame;
}

static void append(const RecvxBootRenderVertex *output, unsigned int count, RecvxBootRenderKind kind,
                   unsigned int attributes, unsigned int sampler, int source, int destination,
                   const RecvxBootTexture *texture, unsigned int fog_color)
{
    /* GE fog blends after texturing, preserving the texture and alpha. The
       original reciprocal-depth table represents this near/far ramp with
       quantized interpolation; GE evaluates the ramp directly. Snapshot its
       parameters because events may change them before this queue is shown. */
    unsigned int fog_enabled = ulNaFogState && (kind == RECVX_RENDER_TRIANGLES || kind == RECVX_RENDER_LINES);
    float fog_near = fog_enabled ? fNaFogNear : 0;
    float fog_far = fog_enabled ? fNaFogFar : 0;
    if (fog_enabled) {
        if (!isfinite(fog_near) || !isfinite(fog_far) || fog_near < 0 || fog_far <= fog_near)
            recvxGameBootUnsupported("invalid original geometry fog range");
        fog_color = ulNaFogR | (ulNaFogG << 8) | (ulNaFogB << 16);
    }
    /* Event scripts run after model submission and may set the next camera.
       Keep this frame's projection with the view-space vertices it produced. */
    if (count && !vertex_count) capture_projection();
    reserve(count);
    RecvxBootRenderBatch *batch = batch_count ? &batches[batch_count-1] : NULL;
    if (!count || kind == RECVX_RENDER_SHADOW_COMPOSITE || !batch || batch->count + count > 65535u || batch->kind != kind ||
        batch->fog_color != fog_color || batch->fog_enabled != fog_enabled ||
        batch->fog_near != fog_near || batch->fog_far != fog_far ||
        batch->texture != texture || batch->attributes != attributes || batch->sampler != sampler ||
        batch->blend_source != source || batch->blend_destination != destination ||
        memcmp(batch->scissor, user_scissor, sizeof(user_scissor))) {
        batch = &batches[batch_count++];
        *batch = (RecvxBootRenderBatch){.first=vertex_count, .attributes=attributes, .sampler=sampler,
            .blend_source=source, .blend_destination=destination, .texture=texture, .kind=kind, .fog_color=fog_color,
            .fog_enabled=fog_enabled, .fog_near=fog_near, .fog_far=fog_far};
        memcpy(batch->scissor, user_scissor, sizeof(user_scissor));
    }
    if (count) memcpy(vertices + vertex_count, output, count * sizeof(*output));
    vertex_count += count; batch->count += count;
}

static void local_vertices(const RecvxGameChunkVertex *input, RecvxBootRenderVertex *output, unsigned int count)
{
    NJS_MATRIX matrix;
    if (!input) recvxGameBootUnsupported("invalid original primitive vertices");
    njGetMatrix(&matrix); checked_math();
    for (unsigned int i = 0; i < count; ++i) {
        NJS_POINT3 local = {input[i].x, input[i].y, input[i].z}, view;
        njCalcPoint(&matrix, &local, &view); checked_math();
        if (!isfinite(view.x) || !isfinite(view.y) || !isfinite(view.z))
            recvxGameBootUnsupported("non-finite original primitive transform");
        unsigned int color = input[i].color;
        output[i] = (RecvxBootRenderVertex){0,0,
            (color & 0xff00ff00u) | ((color >> 16) & 255u) | ((color & 255u) << 16),
            view.x,-view.y,-view.z};
    }
}
static void screen_triangle(const RecvxGameChunkVertex input[3], int trans, int half, unsigned int offset)
{
    RecvxBootRenderVertex output[3];
    const RecvxGameBootRenderState *state = recvxGameBootRenderStateGet();
    const RecvxBootTexture *texture = state->selected_texture;
    float near, far;
    if (!input || (trans != 0 && trans != 1) || !texture || !texture->source ||
        !texture->pixels || texture->tile_count != 1)
        recvxGameBootUnsupported("invalid original screen sprite submission");
    recvxEngineOriginalViewClipPlanes(&near, &far);
    if (!isfinite(near) || !isfinite(far) || near >= 0 || far >= near)
        recvxGameBootUnsupported("invalid original screen sprite depth range");
    float a = (-65536.0f * near) / (far-near);
    float b = (65536.0f * near * far) / (far-near);
    for (unsigned int i=0; i<3; ++i) {
        if (!isfinite(input[i].x) || !isfinite(input[i].y) || !isfinite(input[i].z) ||
            !isfinite(input[i].u) || !isfinite(input[i].v) || input[i].z <= 0)
            recvxGameBootUnsupported("invalid original screen sprite vertex");
        float depth = a + b / (-1.0f / input[i].z);
        unsigned int color=input[i].color;
        output[i]=(RecvxBootRenderVertex){input[i].u*texture->width, input[i].v*texture->height,
            (color&0xff00ff00u)|((color>>16)&255u)|((color&255u)<<16),
            input[i].x*0.75f,input[i].y*(272.0f/480.0f),fmaxf(0,fminf(65534,depth))};
        if (!isfinite(output[i].x) || !isfinite(output[i].y) || !isfinite(output[i].u) || !isfinite(output[i].v))
            recvxGameBootUnsupported("screen sprite exceeds PSP coordinate range");
    }
    if (half) {
        RecvxBootRenderVertex constant[3];
        unsigned int rgb = ((offset & 0x00fefefeu) >> 1);
        rgb = ((rgb >> 16)&255u) | (rgb&0xff00u) | ((rgb&255u)<<16);
        for (unsigned int i=0;i<3;++i) {
            constant[i]=output[i];
            constant[i].color=(output[i].color&0xff000000u)|rgb;
            output[i].color=(output[i].color&0xff000000u)|((output[i].color&0x00fefefeu)>>1);
        }
        /* GE texture BLEND with equal vertex/environment RGB yields the
           authored offset while retaining texture*vertex alpha. Then add
           half the modulated base, weighted by the same source alpha. */
        append(constant,3,RECVX_RENDER_SCREEN_OFFSET,0x13u|(trans?8u:0),3u|(state->texture_filter?4u:0),8,6,texture,rgb);
        append(output,3,RECVX_RENDER_SCREEN_HALF_BASE,0x1bu,3u|(state->texture_filter?4u:0),trans?8:10,10,texture,0);
    } else append(output,3,RECVX_RENDER_SCREEN_TRIANGLES,0x13u|(trans?8u:0),3u|(state->texture_filter?4u:0),
        state->polygon_blend_source,state->polygon_blend_destination,texture,0);
}
void recvxGameBootRenderScreenTriangle(const RecvxGameChunkVertex input[3], int trans)
{ screen_triangle(input,trans,0,0); }
void recvxGameBootRenderHalfFogScreenTriangle(const RecvxGameChunkVertex input[3], unsigned int offset, int trans)
{ screen_triangle(input,trans,1,offset); }

void recvxGameBootRenderLine(const RecvxGameChunkVertex input[2], int trans)
{
    RecvxBootRenderVertex output[2];
    const RecvxGameBootRenderState *state = recvxGameBootRenderStateGet();
    if (trans != 0 && trans != 1) recvxGameBootUnsupported("invalid original line transparency");
    local_vertices(input, output, 2);
    append(output, 2, RECVX_RENDER_LINES, 0x13u | (trans ? 8u : 0), 0,
        state->polygon_blend_source, state->polygon_blend_destination, NULL, 0);
}
void recvxGameBootRenderShadowStart(void)
{
    if (shadow_active) recvxGameBootUnsupported("nested original modifier draw");
    shadow_active = 1;
    append(NULL,0,RECVX_RENDER_SHADOW_START,0,0,0,0,NULL,0);
}
void recvxGameBootRenderShadowTriangle(const RecvxGameChunkVertex input[3], unsigned int pass)
{
    RecvxBootRenderVertex output[3];
    if (!shadow_active || pass > 1) recvxGameBootUnsupported("invalid original modifier pass");
    local_vertices(input, output, 3);
    for (unsigned int i = 0; i < 3; ++i) output[i].color = 0xff202020u;
    append(output,3,pass ? RECVX_RENDER_SHADOW_SUBTRACT : RECVX_RENDER_SHADOW_ADD,0,0,0,0,NULL,0);
}
void recvxGameBootRenderShadowComposite(const float bounds[4], unsigned int fog)
{
    if (!shadow_active || !bounds || fog > 255)
        recvxGameBootUnsupported("invalid original modifier composite");
    for (unsigned int i = 0; i < 4; ++i)
        if (!isfinite(bounds[i]) || bounds[i] < 0 || bounds[i] > (i & 1 ? 480 : 640))
            recvxGameBootUnsupported("invalid original modifier bounds");
    if (bounds[2] <= bounds[0] || bounds[3] <= bounds[1]) return;
    unsigned int tint = 2u * ((fog * 3u) / 16u);
    unsigned int color = 0xff000000u | (tint * 0x10101u);
    RecvxBootRenderVertex output[2];
    for (unsigned int i = 0; i < 2; ++i) {
        float bx = bounds[2*i], by = bounds[2*i+1];
        float x = floorf(bx*16.0f)/16.0f * 0.75f;
        float y = floorf(by*16.0f)/16.0f * (272.0f/480.0f);
        output[i] = (RecvxBootRenderVertex){(floorf(bx)+0.5f)*0.75f,
            (floorf(by)+0.5f)*(272.0f/480.0f),color,x,y,0};
    }
    append(output,2,RECVX_RENDER_SHADOW_COMPOSITE,0,0,0,0,NULL,0);
}
void recvxGameBootRenderShadowEnd(void)
{
    if (!shadow_active) recvxGameBootUnsupported("unmatched original modifier end");
    append(NULL,0,RECVX_RENDER_SHADOW_END,0,0,0,0,NULL,0);
    shadow_active = 0;
}

static float dot(const NJS_VECTOR *a, const NJS_VECTOR *b)
{ return a->x*b->x + a->y*b->y + a->z*b->z; }
static int normalize(NJS_VECTOR *v)
{
    float length = sqrtf(dot(v, v));
    if (!isfinite(length) || length < 1e-20f) return 0;
    v->x /= length; v->y /= length; v->z /= length;
    return 1;
}
static float channel(unsigned int color, unsigned int shift)
{ return ((color >> shift) & 255u) / 255.0f; }
static unsigned int byte(float value)
{
    if (!isfinite(value)) recvxGameBootUnsupported("non-finite original model lighting");
    return (unsigned int)(fminf(1, fmaxf(0, value)) * 255.0f + 0.5f);
}

typedef struct VertexCache {
    unsigned int draw,index,material,color;
    NJS_POINT3 position;
    NJS_VECTOR normal;
    int normal_ready,normal_valid;
} VertexCache;
/* Bounded scratch space, invalidated per draw: animated/replaced streams never
   share results. Flat-shaded faces deliberately bypass the color cache. */
static VertexCache vertex_cache[256];
static unsigned int draw_serial;

typedef struct DrawContext {
    NJS_MATRIX matrix, normals;
    int sprite, textured, trans, half_fog;
    int custom_clip, reversed;
    unsigned int draw,material_serial;
    RecvxGameChunkMaterial material;
    int material_valid,lights_prepared;
    NJS_VECTOR light_direction;
    int light_direction_valid;
    NJS_POINT3 light_positions[8];
    float clip_planes[6][4];
    unsigned int fog_color;
    const RecvxGameBootRenderState *state;
    const RecvxEngineOriginalLightingState *lighting;
} DrawContext;

typedef struct ClipVertex {
    float u,v,x,y,z,color[4];
} ClipVertex;

static float clip_distance(const ClipVertex *vertex,const float plane[4])
{
    /* Queued GU vertices have the original Ninja Y/Z signs reversed. */
    float result=plane[0]*vertex->x-plane[1]*vertex->y-plane[2]*vertex->z+plane[3];
    if(!isfinite(result))recvxGameBootUnsupported("original custom clip distance exceeds range");
    return result;
}
static ClipVertex clip_intersection(const ClipVertex *a,const ClipVertex *b,float da,float db)
{
    float aa=fabsf(da),bb=fabsf(db),ratio,t;
    /* Opposite signs guarantee a crossing. Avoid overflowing da-db for large
       finite coordinates, and preserve UV/color interpolation in view space. */
    if(aa>bb){ratio=bb/aa;t=1/(1+ratio);}
    else{ratio=aa/bb;t=ratio/(1+ratio);}
    ClipVertex result={.u=(1-t)*a->u+t*b->u,.v=(1-t)*a->v+t*b->v,
        .x=(1-t)*a->x+t*b->x,.y=(1-t)*a->y+t*b->y,.z=(1-t)*a->z+t*b->z};
    for(unsigned int i=0;i<4;++i)result.color[i]=(1-t)*a->color[i]+t*b->color[i];
    if(!isfinite(result.u)||!isfinite(result.v)||!isfinite(result.x)||!isfinite(result.y)||!isfinite(result.z))
        recvxGameBootUnsupported("original custom clip interpolation exceeds range");
    return result;
}
static unsigned int clip_triangle(const RecvxBootRenderVertex input[3],const float planes[6][4],
                                  RecvxBootRenderVertex output[21])
{
    /* Intersecting a triangle with six convex half spaces produces at most
       nine polygon vertices, or seven fan triangles. Keep two spare slots. */
    ClipVertex polygon[2][12];
    unsigned int count=3,current=0;
    for(unsigned int i=0;i<3;++i){
        const RecvxBootRenderVertex *v=&input[i];
        polygon[0][i]=(ClipVertex){.u=v->u,.v=v->v,.x=v->x,.y=v->y,.z=v->z};
        for(unsigned int c=0;c<4;++c)polygon[0][i].color[c]=(v->color>>(c*8))&255u;
    }
    for(unsigned int plane=0;plane<6&&count;++plane){
        unsigned int next=current^1u,n=0;
        ClipVertex previous=polygon[current][count-1];
        float previous_distance=clip_distance(&previous,planes[plane]);
        for(unsigned int i=0;i<count;++i){
            ClipVertex vertex=polygon[current][i];
            float distance=clip_distance(&vertex,planes[plane]);
            if((previous_distance<0&&distance>0)||(previous_distance>0&&distance<0)){
                if(n>=12)recvxGameBootUnsupported("original custom clip polygon exceeds capacity");
                polygon[next][n++]=clip_intersection(&previous,&vertex,previous_distance,distance);
            }
            if(distance>=0){
                if(n>=12)recvxGameBootUnsupported("original custom clip polygon exceeds capacity");
                polygon[next][n++]=vertex;
            }
            previous=vertex;previous_distance=distance;
        }
        current=next;count=n;
    }
    if(count<3)return 0;
    if(count>9)recvxGameBootUnsupported("original custom clip fan exceeds capacity");
    unsigned int emitted=0;
    for(unsigned int i=2;i<count;++i){
        const ClipVertex *v[3]={&polygon[current][0],&polygon[current][i-1],&polygon[current][i]};
        NJS_VECTOR a={v[1]->x-v[0]->x,v[1]->y-v[0]->y,v[1]->z-v[0]->z};
        NJS_VECTOR b={v[2]->x-v[0]->x,v[2]->y-v[0]->y,v[2]->z-v[0]->z};
        NJS_VECTOR area={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        if(area.x==0&&area.y==0&&area.z==0)continue;
        for(unsigned int j=0;j<3;++j){
            unsigned int color=0;
            for(unsigned int c=0;c<4;++c)
                color|=(unsigned int)(fmaxf(0,fminf(255,v[j]->color[c]))+0.5f)<<(8*c);
            output[emitted++]=(RecvxBootRenderVertex){v[j]->u,v[j]->v,color,v[j]->x,v[j]->y,v[j]->z};
        }
    }
    return emitted;
}

static void prepare_lights(DrawContext *context)
{
    if(context->lights_prepared)return;
    int simple=context->state->current_draw_mode>=2;
    int multi=context->state->current_draw_mode&1;
    const RecvxEngineOriginalLightingState *lighting=context->lighting;
    const RecvxEngineOriginalLightValue *lights=simple?lighting->simple:lighting->easy;
    if(multi){
        int count=simple?lighting->simple_count:lighting->easy_count;
        if(count<0||count>7)recvxGameBootUnsupported("invalid original model light count");
        for(int i=1;i<=count;++i)if(lights[i].enabled){
            if(i==1){
                NJS_VECTOR direction=lights[0].direction;
                njCalcVector(cam.mtx,&direction,&context->light_direction);checked_math();
            }else{
                NJS_POINT3 point=lights[i].position;
                njCalcPoint(cam.mtx,&point,&context->light_positions[i]);checked_math();
                if(!isfinite(lights[i].near_range)||!isfinite(lights[i].far_range)||lights[i].near_range<0||lights[i].far_range<0)
                    recvxGameBootUnsupported("invalid original model point light range");
            }
        }
    }else context->light_direction=simple?lighting->simple_direction:lighting->easy_direction;
    context->light_direction.x=-context->light_direction.x;
    context->light_direction.y=-context->light_direction.y;
    context->light_direction.z=-context->light_direction.z;
    context->light_direction_valid=normalize(&context->light_direction);
    context->lights_prepared=1;
}

/* Retail textured color table: Easy uses an overbright ramp; Simple single
   uses power 17; Simple multi excludes ambient from the overbright ramp.
   ps2_Vu1Strip.c scales specular by 31 and diffuse by 128. */
static int specular_enabled(const DrawContext *context,const RecvxGameChunkMaterial *material,unsigned int attributes)
{
    return !context->sprite&&material->texture!=UINT_MAX&&!(attributes&3u)&&
        !(context->state->control_flags&0x10u)&&
        ((context->state->current_draw_mode&1u)||(material->specular&0xffffffu));
}
static float specular_weight(float illumination,int powered)
{
    float x=fmaxf(0,fminf(1,powered?illumination:illumination-1));
    if(!powered)return x;
    float x2=x*x,x4=x2*x2,x8=x4*x4,x16=x8*x8;
    return x16*x;
}

static unsigned int shade(DrawContext *context, const RecvxGameChunkMaterial *material,
                           const RecvxGameChunkVertex *vertex, const NJS_POINT3 *position,
                           const NJS_VECTOR *normal, unsigned int attributes)
{
    int simple = context->state->current_draw_mode >= 2;
    int multi = context->state->current_draw_mode & 1;
    const RecvxEngineOriginalLightingState *lighting = context->lighting;
    const RecvxEngineOriginalLightValue *lights = simple ? lighting->simple : lighting->easy;
    const float *ambient = simple ? context->state->simple_ambient : context->state->easy_ambient;
    float diffuse[3] = {channel(material->diffuse, 16), channel(material->diffuse, 8), channel(material->diffuse, 0)};
    if (material->texture != UINT_MAX && !simple)
        diffuse[0] = diffuse[1] = diffuse[2] = 1; /* Retail Easy textured path uses NaCnkDefaultOne. */
    float illumination[3] = {0, 0, 0},ambient_value[3]={0,0,0};
    int specular=specular_enabled(context,material,attributes);
    float alpha = channel(material->diffuse, 24) * channel(vertex->color, 24);
    if (context->state->control_flags & 0x10u) {
        diffuse[0] = context->state->constant_material.r;
        diffuse[1] = context->state->constant_material.g;
        diffuse[2] = context->state->constant_material.b;
        alpha = context->state->constant_material.a * channel(vertex->color, 24);
    }
    if (attributes & 1u) illumination[0] = illumination[1] = illumination[2] = 1;
    else {
        prepare_lights(context);
        if (!(attributes & 4u)) for (unsigned int c = 0; c < 3; ++c)
            ambient_value[c] = illumination[c] = multi ? ambient[c] * channel(material->ambient, 16 - 8*c) :
                (simple ? lighting->simple_ambient : lighting->easy_ambient);
        if (multi) {
            int count = simple ? lighting->simple_count : lighting->easy_count;
            if (count < 0 || count > 7) recvxGameBootUnsupported("invalid original model light count");
            for (int i = 1; i <= count; ++i) if (lights[i].enabled) {
                NJS_VECTOR direction;
                float attenuation = 1;
                if (i == 1) {
                    direction=context->light_direction;
                } else {
                    NJS_POINT3 point=context->light_positions[i];
                    direction = (NJS_VECTOR){point.x-position->x, point.y-position->y, point.z-position->z};
                    float near = lights[i].near_range, far = lights[i].far_range;
                    float distance_squared = dot(&direction, &direction);
                    if (!isfinite(far) || !isfinite(near) || far < 0 || near < 0)
                        recvxGameBootUnsupported("invalid original model point light range");
                    attenuation = distance_squared >= far*far ? 0 :
                        distance_squared <= near*near ? 1 : near*near / distance_squared;
                }
                checked_math();
                if (i==1?context->light_direction_valid:normalize(&direction)) {
                    float intensity = fmaxf(0, dot(normal, &direction)) * attenuation;
                    illumination[0] += lights[i].color.x * intensity;
                    illumination[1] += lights[i].color.y * intensity;
                    illumination[2] += lights[i].color.z * intensity;
                }
            }
        } else {
            NJS_VECTOR direction=context->light_direction;
            if(context->light_direction_valid){
                float intensity = fmaxf(0, dot(normal, &direction)) *
                    (simple ? lighting->simple_intensity : lighting->easy_intensity);
                NJS_POINT3 color = simple ? lighting->simple_color : lighting->easy_color;
                illumination[0] += color.x * intensity;
                illumination[1] += color.y * intensity;
                illumination[2] += color.z * intensity;
            }
        }
    }
    unsigned int result = byte(alpha) << 24;
    for (unsigned int c = 0; c < 3; ++c){
        float value=diffuse[c]*illumination[c];
        if(specular){
            float incident=illumination[c]-(simple&&multi?ambient_value[c]:0);
            float highlight=specular_weight(incident,simple&&!multi);
            float tint=multi?1:channel(material->specular,16-8*c);
            value=diffuse[c]*fmaxf(0,fminf(1,illumination[c]))+highlight*tint*(31.0f/128.0f);
            /* Preserve PS2 overbright modulation without another geometry pass.
               GE doubles RGB after texturing; alpha remains at its full scale. */
            value*=0.5f;
        }
        result |= byte(value*channel(vertex->color,16-8*c)) << (16-8*c);
    }
    return result;
}

static VertexCache *cached_vertex(DrawContext *context,const RecvxGameChunkVertex *input)
{
    VertexCache *cache=&vertex_cache[input->source_index&255u];
    if(cache->draw!=context->draw||cache->index!=input->source_index){
        *cache=(VertexCache){.draw=context->draw,.index=input->source_index};
        recvxFaceTransform(context->matrix,&input->x,&cache->position.x);
        ++render_stats.position_transforms;
    }else ++render_stats.cache_hits;
    return cache;
}

static int emit(void *user, const RecvxGameChunkMaterial *material, const RecvxGameChunkVertex input[3])
{
    DrawContext *context = user;
    RecvxBootRenderVertex output[3];
    NJS_POINT3 positions[3]; NJS_VECTOR face;
    ++render_stats.triangles;
    if(!context->sprite&&(!context->material_valid||memcmp(&context->material,material,sizeof(*material)))){
        context->material=*material;context->material_valid=1;++context->material_serial;
    }
    unsigned int attributes = context->sprite ? (0x13u | (context->trans ? 8u : 0)) :
        recvxGameBootRenderStripAttributes(material->flags);
    const RecvxBootTexture *texture = NULL;
    RecvxGameBootSampler sampler = {0};
    if (attributes & 0x40u) recvxGameBootUnsupported("unported original environment-mapped chunk material");
    if (context->sprite && context->textured) {
        texture = context->state->selected_texture;
        if (!texture || !texture->source || !texture->pixels || texture->tile_count != 1)
            recvxGameBootUnsupported("invalid original sprite texture selection");
        sampler.clamp_u = sampler.clamp_v = 1;
        sampler.filter = context->state->texture_filter;
    } else if (!context->sprite && material->texture != UINT_MAX) {
        texture = recvxGameBootRenderTextureSelect(NULL, material->texture);
        recvxGameBootRenderTextureSampler(material->texture_bits, material->texture_flags, &sampler);
        if (texture->tile_count != 1) recvxGameBootUnsupported("unported tiled 3D material texture");
        if (sampler.mirror_u || sampler.mirror_v) recvxGameBootUnsupported("unported mirrored 3D material sampler");
    }
    for (unsigned int i = 0; i < 3; ++i) {
        NJS_POINT3 local = {input[i].x, input[i].y, input[i].z};
        if(context->sprite)njCalcPoint(&context->matrix, &local, &positions[i]);
        else positions[i]=cached_vertex(context,&input[i])->position;
    }
    checked_math();
    NJS_VECTOR a = {positions[1].x-positions[0].x, positions[1].y-positions[0].y, positions[1].z-positions[0].z};
    NJS_VECTOR b = {positions[2].x-positions[0].x, positions[2].y-positions[0].y, positions[2].z-positions[0].z};
    face = (NJS_VECTOR){a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
    if (!normalize(&face)) return 0; /* Zero-area triangles cannot cover a sample. */
    for (unsigned int i = 0; i < 3; ++i) {
        NJS_VECTOR normal = {input[i].nx, input[i].ny, input[i].nz}, transformed;
        unsigned int color;
        if(context->sprite)color=input[i].color;
        else{
            VertexCache *cache=cached_vertex(context,&input[i]);
            int flat=attributes&0x20u;
            if(!flat&&cache->material==context->material_serial){color=cache->color;}
            else{
                transformed=face;
                if(!(attributes&1u)&&!flat){
                    if(!cache->normal_ready){
                        recvxSkinDirection(context->normals,&normal.x,&cache->normal.x);
                        ++render_stats.normal_transforms;
                        cache->normal_valid=normalize(&cache->normal);cache->normal_ready=1;
                    }
                    if(cache->normal_valid)transformed=cache->normal;
                }
                color=shade(context,material,&input[i],&positions[i],&transformed,attributes);
                ++render_stats.shaded_vertices;
                if(!flat&&((attributes&1u)||cache->normal_valid)){cache->color=color;cache->material=context->material_serial;}
            }
        }
        output[i] = (RecvxBootRenderVertex){input[i].u, input[i].v, color,
            positions[i].x, -positions[i].y, -positions[i].z};
        color = output[i].color;
        output[i].color = (color & 0xff00ff00u) | ((color >> 16) & 255u) | ((color & 255u) << 16);
        if (context->half_fog && fabsf(output[i].z) >= 0x1p39f)
            recvxGameBootUnsupported("authored sprite fog depth exceeds PSP constant range");
        if (!isfinite(output[i].x) || !isfinite(output[i].y) || !isfinite(output[i].z))
            recvxGameBootUnsupported("non-finite original model transform");
    }
    if(context->reversed){RecvxBootRenderVertex swap=output[1];output[1]=output[2];output[2]=swap;}
    unsigned int flags = sampler.clamp_u | (sampler.clamp_v << 1) | (!!sampler.filter << 2);
    if(specular_enabled(context,material,attributes))flags|=RECVX_RENDER_COLOR_DOUBLE;
    int source = context->sprite ? context->state->polygon_blend_source : context->state->control_flags & 0x8000u ? context->state->blend_source : material->blend_source;
    int destination = context->sprite ? context->state->polygon_blend_destination : context->state->control_flags & 0x8000u ? context->state->blend_destination : material->blend_destination;
    RecvxBootRenderVertex clipped[21];
    unsigned int count=context->custom_clip?clip_triangle(output,context->clip_planes,clipped):3;
    if(!count)return 0;
    append(context->custom_clip?clipped:output, count, context->half_fog ? RECVX_RENDER_HALF_FOG : RECVX_RENDER_TRIANGLES,
        attributes, flags, source, destination, texture, context->fog_color);
    return 0;
}

void recvxGameBootRenderSpriteTriangle(const RecvxGameChunkVertex input[3], int textured, int trans)
{
    DrawContext context = {.sprite = 1, .textured = textured, .trans = trans};
    RecvxGameChunkMaterial material = {.texture = UINT_MAX};
    if (!input || (textured != 0 && textured != 1) || (trans != 0 && trans != 1))
        recvxGameBootUnsupported("invalid original sprite submission");
    for (unsigned int i = 0; i < 3; ++i)
        if (!isfinite(input[i].x) || !isfinite(input[i].y) || !isfinite(input[i].z) ||
            !isfinite(input[i].u) || !isfinite(input[i].v))
            recvxGameBootUnsupported("non-finite original sprite vertex");
    njGetMatrix(&context.matrix); checked_math();
    memcpy(context.normals, context.matrix, sizeof(context.normals));
    context.state = recvxGameBootRenderStateGet();
    emit(&context, &material, input);
}

void recvxGameBootRenderHalfFogTriangle(const RecvxGameChunkVertex input[3], unsigned int offset_argb, int trans)
{
    DrawContext context = {.sprite=1, .textured=1, .trans=trans, .half_fog=1};
    RecvxGameChunkMaterial material = {.texture=UINT_MAX};
    if (!input || (trans != 0 && trans != 1))
        recvxGameBootUnsupported("invalid authored fog sprite submission");
    for (unsigned int i=0; i<3; ++i)
        if (!isfinite(input[i].x) || !isfinite(input[i].y) || !isfinite(input[i].z) ||
            !isfinite(input[i].u) || !isfinite(input[i].v))
            recvxGameBootUnsupported("non-finite authored fog sprite vertex");
    njGetMatrix(&context.matrix); checked_math();
    memcpy(context.normals, context.matrix, sizeof(context.normals));
    context.state = recvxGameBootRenderStateGet();
    context.fog_color = ((offset_argb >> 16) & 255u) | (offset_argb & 0xff00u) | ((offset_argb & 255u) << 16);
    emit(&context, &material, input);
}

static void normal_matrix(NJS_MATRIX *output, NJS_MATRIX *input)
{
    memcpy(*output, *input, sizeof(NJS_MATRIX));
    if (njInvertMatrix(output)) njTransposeMatrix(output);
    else checked_math();
    (*output)[12] = (*output)[13] = (*output)[14] = 0;
}

int njCnkDrawModelLocal(NJS_CNK_MODEL *model)
{
    DrawContext context = {0};
    if (!model) return 0;
    if (!sys || !owned(model, sizeof(*model))) recvxGameBootUnsupported("invalid original chunk model pointer");
    if (!isfinite(model->center.x) || !isfinite(model->center.y) || !isfinite(model->center.z) || !isfinite(model->r))
        recvxGameBootUnsupported("invalid original chunk model bounds");
    njGetMatrix(&context.matrix); checked_math();
    context.state = recvxGameBootRenderStateGet(); context.lighting = recvxEngineOriginalLightingGet();
    const float *m=context.matrix;
    context.reversed=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2])<0;
    if ((context.state->control_flags & 0x100u) && isfinite(model->r) && model->r > 0) {
        NJS_POINT3 center; float near, far;
        njCalcPoint(&context.matrix, &model->center, &center); checked_math();
        recvxEngineOriginalViewClipPlanes(&near, &far);
        float scale = 0;
        for (unsigned int c = 0; c < 3; ++c) {
            float length = sqrtf(context.matrix[c*4]*context.matrix[c*4] +
                context.matrix[c*4+1]*context.matrix[c*4+1] + context.matrix[c*4+2]*context.matrix[c*4+2]);
            scale = fmaxf(scale, length);
        }
        float radius = model->r * scale;
        if (center.z + radius < -near || center.z - radius > -far) return 0;
        if (center.z != 0) {
            float distance = fabsf(recvxEngineOriginalViewProjectionDistance() / center.z), ax, ay;
            recvxEngineOriginalViewAspect(&ax, &ay);
            if (center.x*distance + radius*distance*ax < -320 ||
                center.x*distance - radius*distance*ax > 320 ||
                center.y*distance + radius*distance*ay < -240 ||
                center.y*distance - radius*distance*ay > 240) return 0;
        }
    }
    normal_matrix(&context.normals, &context.matrix); checked_math();
    recvxFaceVectorPrepare();
    context.draw=++draw_serial;
    if(!context.draw){memset(vertex_cache,0,sizeof(vertex_cache));context.draw=++draw_serial;}
    ++render_stats.models;
    context.custom_clip=recvxEngineOriginalViewClipVolumePlanes(context.clip_planes);checked_math();
    if (recvxGameBootChunkParse(model, emit, &context)) recvxGameBootUnsupported(recvxGameBootChunkFault());
    return 1;
}
void njCnkEasyDrawModel(NJS_CNK_MODEL *model) { njCnkSetCurrentDrawMode(0); njCnkDrawModelLocal(model); }
void njCnkEasyMultiDrawModel(NJS_CNK_MODEL *model) { njCnkSetCurrentDrawMode(1); njCnkDrawModelLocal(model); }
void njCnkSimpleDrawModel(NJS_CNK_MODEL *model) { njCnkSetCurrentDrawMode(2); njCnkDrawModelLocal(model); }
void njCnkSimpleMultiDrawModel(NJS_CNK_MODEL *model) { njCnkSetCurrentDrawMode(3); njCnkDrawModelLocal(model); }

static void tree(NJS_CNK_OBJECT *object, unsigned int depth, unsigned int *visited)
{
    if (depth > 64) recvxGameBootUnsupported("original draw object tree exceeds depth");
    for (; object; object = object->sibling) {
        if (++*visited > 512 || !owned(object, sizeof(*object)))
            recvxGameBootUnsupported("invalid or cyclic original draw object tree");
        unsigned int flags = object->evalflags;
        for (unsigned int i = 0; i < 3; ++i)
            if ((!(flags & 1u) && !isfinite(object->pos[i])) ||
                (!(flags & 4u) && !isfinite(object->scl[i])))
                recvxGameBootUnsupported("invalid original draw object transform");
        if (!njPushMatrixEx()) checked_math();
        if (!(flags & 1u)) njTranslate(NULL, object->pos[0], object->pos[1], object->pos[2]);
        if (!(flags & 2u)) njRotateEx(object->ang, flags & 0x20u);
        if (!(flags & 4u)) njScale(NULL, object->scl[0], object->scl[1], object->scl[2]);
        checked_math();
        int visible = flags & 8u ? 0 : njCnkDrawModelLocal(object->model);
        if (!(flags & 0x10u) && (!(flags & 0x100u) || visible)) tree(object->child, depth+1, visited);
        if (!njPopMatrixEx()) checked_math();
    }
}
void njCnkEasyDrawObject(NJS_CNK_OBJECT *object)
{ unsigned int visited = 0; njCnkSetCurrentDrawMode(0); tree(object, 0, &visited); }
void njCnkEasyMultiDrawObject(NJS_CNK_OBJECT *object)
{ unsigned int visited = 0; njCnkSetCurrentDrawMode(1); tree(object, 0, &visited); }
void njCnkEasyMultiDrawObjectI(NJS_CNK_OBJECT *object)
{ unsigned int visited = 0; njCnkSetCurrentDrawMode(1); ulCnkCurrentDrawMode |= 4; tree(object, 0, &visited); }
