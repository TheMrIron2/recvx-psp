/* Call models for controller-only testing. These do not render pixels, load
 * inspected models, or implement item-use/file-view behavior. Unexercised
 * services abort so a fixture cannot silently skip a new dependency. */
#include <stdlib.h>
#include <stdio.h>
#define DRAW_MODEL(name) void name(void) { }
DRAW_MODEL(njClipZ)
DRAW_MODEL(njSetFogColor)
DRAW_MODEL(njControl3D)
DRAW_MODEL(njSetConstantAttr)
DRAW_MODEL(njSetBackColor)
DRAW_MODEL(njSetConstantMaterial)
DRAW_MODEL(njSetPaletteBankNum)
DRAW_MODEL(njDrawTextureH)
DRAW_MODEL(CallSystemSe)
#define UNEXERCISED(name) void name(void) { fprintf(stderr, "unexpected inventory dependency: " #name "\n"); abort(); }
UNEXERCISED(ControlFileView)
UNEXERCISED(FileFlagInit01)
UNEXERCISED(GetFile)
UNEXERCISED(CameraSet)
UNEXERCISED(DrawSubItem)
UNEXERCISED(ItemView)
UNEXERCISED(Ps2ClearOT)
UNEXERCISED(Ps2DrawOTag)
UNEXERCISED(Ps2DrawOTagI)
UNEXERCISED(SyncPath)
UNEXERCISED(bhGarbageTexture)
void bhSetMessage(int bank,int message) { if(bank!=1 || message<0) abort(); }
UNEXERCISED(njReleaseTexture)
UNEXERCISED(njSetScreen)
UNEXERCISED(njUserClipping)
DRAW_MODEL(njDrawLine2D)
