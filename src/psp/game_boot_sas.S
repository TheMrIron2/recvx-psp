/* sceSasCore imports for toolchains predating PSPSDK's sascore library.
   NIDs/ABI: pspsdk/src/sascore/sceSasCore.S (BSD licensed PSPSDK). */
#include <pspstub.s>
STUB_START sceSasCore, 0x40090000, 0x00100005
STUB_FUNC 0x42778A9F, __sceSasInit
STUB_FUNC 0xA3589D81, __sceSasCore
STUB_FUNC 0x68A46B95, __sceSasGetEndFlag
STUB_FUNC 0x99944089, __sceSasSetVoice
STUB_FUNC 0xAD84D37F, __sceSasSetPitch
STUB_FUNC 0x440CA7D8, __sceSasSetVolume
STUB_FUNC 0x76F01ACA, __sceSasSetKeyOn
STUB_FUNC 0x787D04D5, __sceSasSetPause
STUB_FUNC 0x019B25EB, __sceSasSetADSR
STUB_FUNC 0x9EC3676A, __sceSasSetADSRmode
STUB_FUNC 0x5F9529F6, __sceSasSetSL
STUB_FUNC 0xCBCD4F79, __sceSasSetSimpleADSR
STUB_FUNC 0xA0CF2FA4, __sceSasSetKeyOff
STUB_FUNC 0x33D4AB37, __sceSasRevType
STUB_FUNC 0xF983B186, __sceSasRevVON
STUB_FUNC 0xD5A229C9, __sceSasRevEVOL
STUB_END
