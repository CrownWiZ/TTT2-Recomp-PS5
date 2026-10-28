// Manual PS5 system-module declarations (scene convention, as in the M3
// spike's main.c and the M4 vkinfo spike). The payload SDK ships stub .so
// files (imports) but no headers for these; every declaration here resolves
// at load time through the module imports the native-tool declares.
#pragma once
#include <cstddef>
#include <cstdint>

#ifdef __PROSPERO__

// Every entry point below lives in a system module and is exported unmangled:
// without this block the C++ declarations mangle and the imports never bind.
extern "C" {

// --- libkernel: notification + faults -------------------------------------------------

typedef struct notify_request {
    char dummy0[0x100];
    char message[0x1C0];
} notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

// --- libSceUserService ----------------------------------------------------------------

int sceUserServiceInitialize(int64_t);
int sceUserServiceGetInitialUser(int32_t *);

// --- libSceAudioOut -------------------------------------------------------------------

constexpr int SCE_AUDIO_OUT_PORT_TYPE_MAIN  = 0;
constexpr int SCE_AUDIO_OUT_PORT_TYPE_VOICE = 1;
constexpr int SCE_AUDIO_OUT_PORT_TYPE_BGM   = 2;
constexpr int SCE_AUDIO_OUT_MODE_STEREO     = 1;
constexpr int SCE_AUDIO_OUT_MODE_7_1CH      = 2;
constexpr int SCE_AUDIO_OUT_FMT_S16_NE      = 0;
constexpr int SCE_AUDIO_OUT_FMT_FLOAT_NE    = 11;
constexpr int SCE_USER_SERVICE_USER_ID_INVALID = -1;

int sceAudioOutInit();
int sceAudioOutOpen(int32_t userId, int32_t portType, int32_t index,
                    uint32_t sampleCount, uint32_t frequency, int32_t modeType);
int sceAudioOutOutput(int32_t handle, const void *ptr);
int sceAudioOutOutputStatus(int32_t handle);
int sceAudioOutClose(int32_t handle);

// --- libSceVideoOut -------------------------------------------------------------------

constexpr int SCE_VIDEO_OUT_BUS_MAIN          = 0;
constexpr int SCE_VIDEO_OUT_PIXEL_FORMAT_B8_G8_R8_X8 = 8; // BGRX little-endian
constexpr int SCE_VIDEO_OUT_BUFFER_INDEX_0    = 0;
constexpr int SCE_VIDEO_OUT_TIMESTAMP_119_88HZ = 1; // see kernel_video's vblank pacing
constexpr int SCE_VIDEO_OUT_EVENT_FLIP        = 1;

typedef struct SceVideoOutBufferAttributeV2 {
    uint32_t pixelFormat;
    uint32_t tilingMode;
    uint32_t aspectRatio;
    uint32_t width;
    uint32_t height;
    uint32_t pitchInPixel;
    uint32_t option;
    uint32_t field;
    void *mem;
} SceVideoOutBufferAttributeV2;

typedef struct SceVideoOutResolutionStatus {
    uint32_t resolutionPerStudioCount;
    uint32_t reserved[15];
} SceVideoOutResolutionStatus;

int sceVideoOutOpen(int32_t userId, int32_t busType, int32_t handle, const void *param);
int sceVideoOutGetResolutionStatus2(int32_t handle, int32_t resolutionOption,
                                    SceVideoOutResolutionStatus *status);
int sceVideoOutRegisterBuffers2(int32_t handle, int32_t startIndex, int32_t bufferNum,
                                const void *bufferMem[], const SceVideoOutBufferAttributeV2 *attribute);
// The SDK's import stub exports SubmitFlip (5.x titles keep it); SubmitFlip2 is
// not in the module's stub table, so the plain entry point is the one that binds.
int sceVideoOutSubmitFlip(int32_t handle, int32_t bufferIndex, int32_t flipMode, int64_t flipArg);
int sceVideoOutClose(int32_t handle);

// --- libkernel: direct memory (GPU/VideoOut buffers) ----------------------------------

constexpr int SCE_KERNEL_DMEM_ALIGNMENT = 0x100000; // 1 MiB
constexpr unsigned SCE_KERNEL_WB_ONION = 0;         // attribute: write-back onion
constexpr uint64_t SCE_KERNEL_MAIN_DMEM_SIZE = 0x100000000ull; // device-memory budget cap

int sceKernelAllocateDirectMemory(int64_t startAddress, int64_t endAddress, size_t size,
                                  size_t alignment, unsigned int memoryType, int64_t *physAddrOut);
int sceKernelMapDirectMemory(void **addrInProcess, size_t len, unsigned int prot,
                             unsigned int flags, int64_t physAddr, size_t maxPhysAddr);
int sceKernelCheckSegPermission(uint64_t addr, uint64_t size, unsigned int prot);

// --- libScePad ------------------------------------------------------------------------

constexpr int SCE_PAD_PORT_TYPE_STANDARD = 0;
constexpr unsigned SCE_PAD_BUTTON_L3        = 0x00002;
constexpr unsigned SCE_PAD_BUTTON_R3        = 0x00004;
constexpr unsigned SCE_PAD_BUTTON_OPTIONS   = 0x00008;
constexpr unsigned SCE_PAD_BUTTON_UP        = 0x00010;
constexpr unsigned SCE_PAD_BUTTON_RIGHT     = 0x00020;
constexpr unsigned SCE_PAD_BUTTON_DOWN      = 0x00040;
constexpr unsigned SCE_PAD_BUTTON_LEFT      = 0x00080;
constexpr unsigned SCE_PAD_BUTTON_L2        = 0x00100;
constexpr unsigned SCE_PAD_BUTTON_R2        = 0x00200;
constexpr unsigned SCE_PAD_BUTTON_L1        = 0x00400;
constexpr unsigned SCE_PAD_BUTTON_R1        = 0x00800;
constexpr unsigned SCE_PAD_BUTTON_TRIANGLE  = 0x01000;
constexpr unsigned SCE_PAD_BUTTON_CIRCLE    = 0x02000;
constexpr unsigned SCE_PAD_BUTTON_CROSS     = 0x04000;
constexpr unsigned SCE_PAD_BUTTON_SQUARE    = 0x08000;
constexpr unsigned SCE_PAD_BUTTON_TOUCH_PAD = 0x100000;

typedef struct SceFVector2 { float x, y; } SceFVector2;

typedef struct ScePadAnalogProb {
    uint8_t dummy[4];
    float lx, ly, rx, ry; // -1.0..1.0 pressed-positive up/right ordering per module docs
    uint8_t deadZoneLeft, deadZoneRight;
    uint8_t unused[6];
} ScePadAnalogProb;

typedef struct ScePadReadData {
    uint32_t min;
    SceFVector2 dummy16[6];
    uint8_t reserve;
    SceFVector2 analogLeft, analogRight;
    uint32_t buttons;
    ScePadAnalogProb probabilities;
} ScePadReadData;

// RetroArch-compatible minimal declarations: init/open/read/close.
typedef struct ScePadData {
    SceFVector2 analogLeft, analogRight;
    uint32_t buttons;
    uint8_t reserve1;
    uint8_t reserve2[0xA3];
} ScePadData;

int scePadInit();
int scePadOpen(int32_t userId, int32_t type, int32_t index, const void *param);
int scePadRead(int32_t handle, ScePadData *data, int32_t n);
int scePadClose(int32_t handle);

} // extern "C"

#endif // __PROSPERO__
