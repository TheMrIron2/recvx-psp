#include <string.h>

#include "ps2/veronica/prog/ps2_NinjaCnk.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_lighting.h"

static RecvxEngineOriginalLightingState lighting;

void recvxEngineOriginalLightingReset(void)
{
    memset(&lighting, 0, sizeof(lighting));
}

const RecvxEngineOriginalLightingState *recvxEngineOriginalLightingGet(void)
{
    return &lighting;
}

static RecvxEngineOriginalLightValue *get_light(
    RecvxEngineOriginalLightValue lights[8], int index)
{
    if (index < 0 || index >= 8) {
        recvxEngineOriginalFaultSet("original light index exceeds PSP adapter");
        return NULL;
    }
    lighting.api_calls++;
    return &lights[index];
}

void njCnkSetEasyLight(Float x, Float y, Float z)
{
    lighting.easy_direction = (NJS_VECTOR){x, y, z};
    lighting.api_calls++;
}

void njCnkSetEasyLightIntensity(Float intensity, Float ambient)
{
    lighting.easy_intensity = intensity;
    lighting.easy_ambient = ambient;
    lighting.api_calls++;
}

void njCnkSetEasyLightColor(Float r, Float g, Float b)
{
    lighting.easy_color = (NJS_POINT3){r, g, b};
    lighting.api_calls++;
}

void njCnkSetSimpleLight(Float x, Float y, Float z)
{
    lighting.simple_direction = (NJS_VECTOR){x, y, z};
    lighting.api_calls++;
}

void njCnkSetSimpleLightIntensity(Float intensity, Float ambient)
{
    lighting.simple_intensity = intensity;
    lighting.simple_ambient = ambient;
    lighting.api_calls++;
}

void njCnkSetSimpleLightColor(Float r, Float g, Float b)
{
    lighting.simple_color = (NJS_POINT3){r, g, b};
    lighting.api_calls++;
}

void njCnkSetEasyMultiLight(Int count)
{
    if (count < 0 || count > 8) {
        recvxEngineOriginalFaultSet("original easy-light count exceeds PSP adapter");
        return;
    }
    lighting.easy_count = count;
    lighting.api_calls++;
}

void njCnkSetSimpleMultiLight(Int count)
{
    if (count < 0 || count > 8) {
        recvxEngineOriginalFaultSet("original simple-light count exceeds PSP adapter");
        return;
    }
    lighting.simple_count = count;
    lighting.api_calls++;
}

void njCnkSetEasyMultiLightSwitch(Int light, Int enabled)
{
    RecvxEngineOriginalLightValue *value = get_light(lighting.easy, light);
    if (value != NULL)
        value->enabled = enabled != 0;
}

void njCnkSetSimpleMultiLightSwitch(Int light, Int enabled)
{
    RecvxEngineOriginalLightValue *value = get_light(lighting.simple, light);
    if (value != NULL)
        value->enabled = enabled != 0;
}

void njCnkSetEasyMultiLightColor(Int light, Float r, Float g, Float b)
{
    RecvxEngineOriginalLightValue *value = get_light(lighting.easy, light);
    if (value != NULL)
        value->color = (NJS_POINT3){r, g, b};
}

void njCnkSetSimpleMultiLightColor(Int light, Float r, Float g, Float b)
{
    RecvxEngineOriginalLightValue *value = get_light(lighting.simple, light);
    if (value != NULL)
        value->color = (NJS_POINT3){r, g, b};
}

void njCnkSetEasyMultiLightVector(Float x, Float y, Float z)
{
    lighting.easy[0].direction = (NJS_VECTOR){x, y, z};
    lighting.api_calls++;
}

void njCnkSetSimpleMultiLightVector(Float x, Float y, Float z)
{
    lighting.simple[0].direction = (NJS_VECTOR){x, y, z};
    lighting.api_calls++;
}

void njCnkSetEasyMultiLightPoint(Int light, Float x, Float y, Float z)
{
    RecvxEngineOriginalLightValue *value = get_light(lighting.easy, light);
    if (value != NULL)
        value->position = (NJS_POINT3){x, y, z};
}

void njCnkSetSimpleMultiLightPoint(Int light, Float x, Float y, Float z)
{
    RecvxEngineOriginalLightValue *value = get_light(lighting.simple, light);
    if (value != NULL)
        value->position = (NJS_POINT3){x, y, z};
}

void njCnkSetEasyMultiLightRange(Int light, Float near_range, Float far_range)
{
    RecvxEngineOriginalLightValue *value = get_light(lighting.easy, light);
    if (value != NULL) {
        value->near_range = near_range;
        value->far_range = far_range;
    }
}

void njCnkSetSimpleMultiLightRange(Int light, Float near_range, Float far_range)
{
    RecvxEngineOriginalLightValue *value = get_light(lighting.simple, light);
    if (value != NULL) {
        value->near_range = near_range;
        value->far_range = far_range;
    }
}

void njCnkSetEasyMultiLightMatrices(void)
{
    lighting.matrix_updates++;
    lighting.api_calls++;
}

void njCnkSetSimpleMultiLightMatrices(void)
{
    lighting.matrix_updates++;
    lighting.api_calls++;
}
