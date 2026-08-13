// Helicopter Distribution Office - TesmioLoader bridge
// v1.2.0, target SOVIET64.exe v1.1.1.9, TesmioLoader API 4
//
// Extends a native Distribution Office so it can own and dispatch cargo
// helicopters while retaining normal road-distribution behaviour. The Workshop
// asset supplies two native HELIPORT_STATION records and can attach additional
// heliports through the game's normal heliport-area system.
//
// Runtime changes are deliberately narrow:
//  1) helicopter purchase compatibility for the HDO is routed through the
//     vanilla helicopter-pad compatibility helper;
//  2) the native task rebuild retains records reachable through either the
//     road graph or the helicopter graph. Every per-vehicle assignment
//     transaction is then partitioned by route class: road vehicles see only
//     currently road-reachable non-heliport rows, while helicopters see only
//     fully air-reachable rows. The complete mixed vector is restored before
//     the hook returns, so the UI and saved HDO assignments remain untouched.
//     Native cargo, threshold, priority and reservation checks remain
//     authoritative for the rows each vehicle can reach;
//  3) the HDO's two helipad records are masked only while the native bottom-menu
//     classifier groups Workshop buildings, allowing the asset to appear with
//     ordinary Distribution Offices without removing its functional pads;
//  4) Soviet and Western overseas pseudo-buildings can be added as normal native
//     Distribution Office targets from the office panel;
//  5) an HDO-scoped queue engine snapshots the native task, assignment and
//     resident-vehicle lists. It rebuilds and refreshes WRSR's own 0x80-byte
//     Distribution Office task rows before dispatch, then rechecks parked
//     helicopters after task or fleet edits, helicopter state transitions,
//     HDO-panel close and the native three-second threshold cadence. WRSR parks
//     returned helicopters with both a non-operational flight state and a null
//     current-building field, so its stock Distribution Office candidate scan
//     cannot re-enter assignment. For the exact idle-on-own-HDO-pad case, the
//     engine rebuilds derived task rows only when WRSR's own structural-dirty
//     byte is set or a saved HDO has no derived rows, and otherwise refreshes
//     the existing rows in place. It synchronously presents a load-like
//     current-building/state view
//     only to WRSR's full readiness predicate, restores both fields, then calls
//     WRSR's own per-vehicle assignment routine once. A native outbound route
//     is the durable latch, preventing repeated reservation/route replacement
//     while the helicopter is waiting for its helipad take-off update;
//  6) at WRSR's route-advance seam, after a helicopter has completed its
//     destination stop but before the engine advances to the HDO return leg,
//     the same native task rows are refreshed and the same native per-vehicle
//     assignment routine is offered one atomic chance to replace that return
//     leg. If no eligible air task exists, the original route advance runs
//     unchanged and the helicopter returns home. Cargo transfer, task
//     eligibility, route construction, reservations and take-off remain native;
//  7) while an existing helicopter is being rehomed into the HDO, the
//     synchronous native arrival update temporarily presents that one HDO
//     instance as AIRPLANE_PARKING. The real descriptor is restored before the
//     hook returns;
//  8) every required hook target and directly-called native entrypoint is
//     preflighted before the first hook is installed. Only detour targets need
//     instruction-relocation decoding; callable-only entrypoints are checked as
//     readable native addresses. If an unexpected install failure happens
//     after that point, the DLL remains loaded so no installed detour can become
//     a stale pointer.
//  9) the native Distribution Office building selector keeps its complete
//     duplicate, capacity, construction and ownership validation. While (and
//     only while) an HDO is selecting a genuine cargo heliport, a synchronous
//     one-record connection projection crosses the selector's mandatory
//     road-station existence gate and its cached connectivity result is
//     promoted. The same native selector then draws the valid overlay and
//     creates the ordinary 0x198-byte assignment plus its building
//     back-reference. A discovery pass suppresses the click byte so a
//     newly-hovered no-road heliport cannot emit the stock failure before the
//     promoted native pass handles that same click.
//
// The rehome compatibility shim does not write vehicle home, route, resident
// roster, parking reservation or helipad occupancy fields. The real HDO type
// descriptor is restored immediately after the synchronous native arrival call.
// A preflight mismatch fails before any HDO hook is applied. An unexpected
// failure after installation begins keeps the module resident and suppresses
// Start, preventing an installed detour from pointing into an unloaded DLL.

#define TSM_API_VERSION 4u
#define EXPORT extern "C" __declspec(dllexport)

typedef __SIZE_TYPE__ usize;
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;
extern "C" int _fltused = 0;

// Conventional Windows DLL surface with a normal PE import table and DLL entrypoint.
typedef int BOOL;
typedef unsigned long DWORD;
typedef void* HINSTANCE;
typedef void* LPVOID;
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int virtualKey);
extern "C" BOOL __stdcall DllMain(HINSTANCE, DWORD, LPVOID) { return 1; }
extern "C" void* volatile g_hdoRelocationAnchor = (void*)&DllMain;

struct TsmHost
{
    unsigned apiVersion;
    unsigned structSize;
    void* exeModule;
    u8* exeBase;
    usize exeSize;
    void* engineModule;
    const char* baseDir;
    const char* pluginDir;
    void (*log)(const char* fmt, ...);
    void** (*findIatSlot)(void* module, const char* dll, const char* fn);
    int (*patchIat)(void* module, const char* dll, const char* fn,
                    void* detour, void** original, const char* label);
    int (*installInlineHook)(void* target, void* detour, void** trampoline,
                             const u8* expect, usize stolen,
                             const char* label);
    u8* (*allocNear)(u8* anchor, usize size);
    int (*readablePtr)(const void* p, usize n);
    long (*faultFilter)(const char* what, void* exceptionPointers);
    int (*configInt)(const char* iniName, const char* section,
                     const char* key, int fallback);
    int (*configString)(const char* iniName, const char* section,
                        const char* key, char* out, int outSize,
                        const char* fallback);
    int (*provide)(const char* service, unsigned version, const void* iface);
    const void* (*consume)(const char* service, unsigned version);
};

struct TsmPluginInfo
{
    const char* name;
    const char* version;
};

static const TsmHost* H = 0;
static u8* EXE = 0;

// --------------------------------------------------------------------- v1.1.1.9 layout
static const usize RVA_VEHICLE_BUILDING_COMPAT = 0x003E2900u; // FUN_1403e2900
static const usize RVA_HELICOPTER_PAD_COMPAT    = 0x003E3B80u; // FUN_1403e3b80
static const usize RVA_ROAD_DO_PANEL            = 0x00741170u; // FUN_140741170
static const usize RVA_DO_TASK_ELIGIBILITY       = 0x001DE2B0u; // FUN_1401de2b0
static const usize RVA_BOTTOM_MENU_REBUILD        = 0x000797E0u; // FUN_1400797e0
static const usize RVA_ASSIGN_PUSH              = 0x0008E050u;
static const usize RVA_ASSIGN_INIT              = 0x001512A0u;
static const usize RVA_VEHICLE_RESIDENCE_UPDATE         = 0x006CD1E0u; // FUN_1406cd1e0
static const usize RVA_DISTRIBUTION_OFFICE_SCAN          = 0x001C6050u; // FUN_1401c6050
static const usize RVA_DISTRIBUTION_ASSIGN_VEHICLE       = 0x001E5350u; // FUN_1401e5350
static const usize RVA_DISTRIBUTION_TASK_REBUILD         = 0x001E2380u; // FUN_1401e2380
static const usize RVA_DISTRIBUTION_TASK_REFRESH         = 0x001E3BD0u; // FUN_1401e3bd0
static const usize RVA_VEHICLE_READY                     = 0x006BD6B0u; // FUN_1406bd6b0
static const usize RVA_ROUTE_ADVANCE                     = 0x0067DB00u; // FUN_14067db00
static const usize RVA_DISTRIBUTION_TARGET_SELECTOR      = 0x002B78B0u; // FUN_1402b78b0

static const usize W_BUILDING      = 0x0240u;
static const usize W_POS_X         = 0x0004u;
static const usize W_POS_Y         = 0x0008u;
static const usize W_OFF_X         = 0x0028u;
static const usize W_OFF_Y         = 0x002Cu;

static const usize B_TYPEDESC      = 0x0318u;
static const usize B_ASSIGN_BEGIN  = 0x0D38u;
static const usize B_ASSIGN_END    = 0x0D40u;
static const usize B_ASSIGN_DIRTY  = 0x0D50u;
static const usize B_ASSIGN_TIMER  = 0x0FC0u;
static const usize B_VEHICLE_BEGIN = 0x0C70u;
static const usize B_VEHICLE_END   = 0x0C78u;
static const usize B_TASK_BEGIN    = 0x0D58u;
static const usize B_TASK_END      = 0x0D60u;
static const usize B_TASK_CAP      = 0x0D68u;

// Vehicle state used by the native rehome/arrival path.
static const usize V_CURRENT_BUILDING = 0x04F0u;
static const usize V_HOME          = 0x04F8u; // param_1[0x9f]
static const usize V_ROUTE_ACTIVE  = 0x05FDu;
static const usize V_ROUTE_BEGIN   = 0x0680u;
static const usize V_ROUTE_END     = 0x0688u;
static const usize V_ROUTE_INDEX   = 0x0698u;
static const usize V_ARRIVAL       = 0x0528u; // param_1[0xa5]
static const usize V_APPROACH      = 0x0530u; // param_1[0xa6]
static const usize V_READINESS_BLOCKER = 0x05E8u;
static const usize V_HELICOPTER_STATE = 0x1A04u;
static const usize V_PARKING_OCCUPANCY = 0x0C80u; // param_1[400]
static const usize GAME_DISTRIBUTION_ACTIVE = 0x05A4u;
static const usize GAME_ACTIVE_TOOL         = 0xD428u;
static const usize GAME_SELECTOR_OFFICE     = 0xD298u;
static const usize GAME_SELECTOR_WINDOW     = 0xD2A0u;
static const usize GAME_SELECTOR_VALID      = 0xF5B0u;
static const usize GAME_SELECTOR_TARGET     = 0xF5B8u;

static const usize B_UNDER_CONSTRUCTION = 0x0EA8u;
static const usize B_BUILD_PROGRESS      = 0x0604u;

static const usize TD_TYPE         = 0x0360u;
static const usize TD_SUBTYPE      = 0x0364u;
static const usize TD_WORKSHOP_A    = 0x09D5u;
static const usize TD_WORKSHOP_B    = 0x09D6u;
static const usize TD_STATION_BEGIN = 0x0298u;
static const usize TD_STATION_END   = 0x02A0u;
static const usize TD_STATION_CAP   = 0x02A8u;

// Global building-type descriptor vector scanned by the native bottom-menu
// classifier. Entries are 0xBE8 bytes and begin with the internal asset name.
static const usize G_TYPEDESC_BEGIN = 0x11B20u;
static const usize G_TYPEDESC_END   = 0x11B28u;
static const usize TYPEDESC_STRIDE  = 0x0BE8u;
static const usize V_TYPE          = 0x0294u;
static const usize V_OBJECT_DEF    = 0x1708u;

// Native building station vector used by the helicopter compatibility helper.
// Records are 0x20 bytes; the connection/station kind lives at +0x1C.
static const usize B_STATION_BEGIN = 0x0A40u;
static const usize B_STATION_END   = 0x0A48u;
static const usize STATION_KIND    = 0x001Cu;
static const usize STATION_SIZE    = 0x0020u;

// Distribution-target selection checks a separate 0x60-byte building
// connection vector for at least one road record before consulting its cached
// graph result.
static const usize B_CONNECTION_BEGIN = 0x0A10u;
static const usize B_CONNECTION_END   = 0x0A18u;
static const usize B_CONNECTION_CAP   = 0x0A20u;
static const usize CONNECTION_SIZE    = 0x0060u;
static const usize CONNECTION_LINK    = 0x0030u;

// Distribution Office task record (0x80 bytes).
static const usize TASK_SOURCE     = 0x0010u;
static const usize TASK_DEST       = 0x0048u;
static const usize TASK_SOURCE_ASSIGN_INDEX = 0x0018u;
static const usize TASK_DEST_ASSIGN_INDEX   = 0x0050u;

static const usize G_WESTERN_NODE       = 0x11AF8u;
static const usize G_SOVIET_NODE        = 0x11B00u;
static const usize GAME_SOVIET_ICON     = 0x0B2C0u;
static const usize GAME_WESTERN_ICON    = 0x0B2C8u;
static const usize G_MOUSE_OBJECT_RVA   = 0x00A54B90u;
static const usize G_MOUSE_CLICK_RVA    = 0x00A54E91u;
static const usize G_PANEL_RVA          = 0x009BE060u;
static const usize G_PANEL_POS_RVA      = 0x009BE2F0u;
static const usize G_PANEL_SIZE_RVA     = 0x009BE2E8u;
static const usize G_PANEL_PAD_RVA      = 0x009BE2F8u;
static const usize G_PANEL_COLOR_RVA    = 0x009BE30Cu;
static const usize G_TECHNIQUE_RVA      = 0x009EAD08u;
static const usize G_DPI_RVA            = 0x00992088u;

static const int TYPE_EXTERNAL               = 0x14;
static const int BUILDING_DISTRIBUTION_OFFICE = 0x2B;
static const int BUILDING_AIRPLANE_PARKING      = 0x2F;
static const int SUBTYPE_AIRPLANE             = 0x21;
static const int VEHICLE_HELICOPTER            = 10;

// Keep these at the proven Dock Distribution Office layout values.
// Keep the DDO horizontal placement but align the controls with the native
// "Vehicles/containers:" header. The normal DO panel draws that header around
// baseY + 102*dpi; a 42px icon top at +94 centres the buttons on the same row.
static const int BUTTON_X_OFFSET = 394;
static const int BUTTON_Y_OFFSET = 94;
static const int BUTTON_SIZE     = 42;
static const int BUTTON_GAP      = 4;

// --------------------------------------------------------------------- native call types
typedef u64 (*FnVehicleBuildingCompat)(u8* game, u64 building,
                                       i64 vehicleDef, char mode,
                                       unsigned int* errorOut);
typedef u64 (*FnHelicopterPadCompat)(u8* game, u64 building, i64 vehicleDef);
typedef void (*FnRoadDoPanel)(void* ui, void* window);
typedef u64 (*FnDoTaskEligibility)(i64 game, i64* task, i64 vehicle, i64 office);
typedef void (*FnBottomMenuRebuild)(i64 game);
typedef void (*FnAssignPush)(u64* vector, void** value);
typedef void* (*FnAssignInit)(void* config);
typedef void (*FnVehicleResidenceUpdate)(void* vehicle, void* context);
typedef void (*FnDistributionOfficeScan)(void* game, void* office);
typedef u64 (*FnDistributionAssignVehicle)(void* game, void* vehicle, void* office);
typedef void (*FnDistributionTaskRebuild)(void* game, void* office);
typedef void (*FnDistributionTaskRefresh)(void* game, void* task);
typedef bool (*FnVehicleReady)(void* vehicle, char distributionMode);
typedef void (*FnRouteAdvance)(void* vehicle, char skipDisabledStops);
typedef void (*FnDistributionTargetSelector)(void* game, u64 arg2,
                                              u64 arg3, float scale);
typedef void* (*FnMalloc)(usize size);
typedef void* (*FnGetModuleHandleA)(const char* moduleName);
typedef void* (*FnGetProcAddress)(void* module, const char* procName);
typedef void (*FnPanelDraw)(void* panel, float u0, float v0, float u1, float v1, float angle, int alpha);
typedef void* (*FnGetMouseSolid)(void* input, void* returnBuffer);
typedef int (*FnPanelCollisionRect)(void* panel, void* mouseVector,
                                    float left, float right, float top, float bottom);

static FnVehicleBuildingCompat o_VehicleBuildingCompat = 0;
static FnHelicopterPadCompat    f_HelicopterPadCompat = 0;
static FnRoadDoPanel             o_RoadDoPanel = 0;
static FnDoTaskEligibility       o_DoTaskEligibility = 0;
static FnBottomMenuRebuild       o_BottomMenuRebuild = 0;
static FnAssignPush              f_AssignPush = 0;
static FnAssignInit              f_AssignInit = 0;
static FnVehicleResidenceUpdate         o_VehicleResidenceUpdate = 0;
static FnDistributionOfficeScan         o_DistributionOfficeScan = 0;
static FnDistributionAssignVehicle      f_DistributionAssignVehicle = 0;
static FnDistributionAssignVehicle      o_DistributionAssignVehicle = 0;
static FnDistributionTaskRebuild        f_DistributionTaskRebuild = 0;
static FnDistributionTaskRebuild        o_DistributionTaskRebuild = 0;
static FnDistributionTaskRefresh        f_DistributionTaskRefresh = 0;
static FnVehicleReady                   f_VehicleReady = 0;
static FnRouteAdvance                   o_RouteAdvance = 0;
static FnDistributionTargetSelector     o_DistributionTargetSelector = 0;
static FnMalloc                  f_Malloc = 0;
static FnPanelDraw               f_PanelDraw = 0;
static FnGetMouseSolid           f_GetMouseSolid = 0;
static FnPanelCollisionRect      f_PanelCollisionRect = 0;

static void* g_buttonHome = 0;
static int g_buttonCapture = 0; // 0 none, 1 Soviet, 2 Western
static int g_lastLeftButtonDown = 0;
static int g_buttonSuppressFrames = 0;
static int g_loggedHelicopterTaskSkip = 0;
static u32 g_helicopterTaskAdmitCount = 0;
static u32 g_queuePassLogCount = 0;
static u32 g_queueCandidateLogCount = 0;
static u32 g_queueSafetyRejectLogCount = 0;
static u32 g_queueNoAssignmentLogCount = 0;
static u32 g_savedBootstrapLogCount = 0;
static u32 g_routeAdvanceProbeCount = 0;
static u32 g_airTaskViewLogCount = 0;
static u32 g_airTaskViewRejectCount = 0;
static u32 g_structuralRebuildLogCount = 0;
static u32 g_mixedTaskRebuildLogCount = 0;
static u32 g_queueAssignmentCount = 0;
static u32 g_queuePendingRouteSkipCount = 0;
static u32 g_chainAssignmentCount = 0;
static u32 g_chainNoAssignmentCount = 0;
static u32 g_selectorOverrideLogCount = 0;
static void* g_lastSelectorOverrideTarget = 0;
static int g_chainDispatchActive = 0;
static int g_initReady = 0;

// Private descriptor scratch used only during one synchronous native
// residence-update call. WRSR's simulation update is single-threaded here; the
// actual building descriptor and every building/vehicle state field remain
// game-owned.
__declspec(align(16)) static u8 g_residenceTypeDesc[TYPEDESC_STRIDE];
static void* g_lastArrivalVehicle = 0;

// Synchronous per-vehicle view of the HDO's derived native task rows. The
// complete office vector is restored before the assignment hook returns, so
// UI code and the next vehicle always retain the complete mixed task list.
static const usize AIR_TASK_VIEW_MAX_ROWS = 256u;
__declspec(align(16)) static u8 g_airTaskView[AIR_TASK_VIEW_MAX_ROWS * 0x80u];
static int g_airTaskViewActive = 0;

static const usize SELECTOR_CONNECTION_MAX_ROWS = 128u;
__declspec(align(16)) static u8
    g_selectorConnectionView[(SELECTOR_CONNECTION_MAX_ROWS + 1u) * CONNECTION_SIZE];

// --------------------------------------------------------------------- exact v1.1.1.9 byte guards
static const u8 EXPECT_COMPAT[] = {
    0x48,0x89,0x5C,0x24,0x10,
    0x48,0x89,0x6C,0x24,0x18,
    0x44,0x88,0x4C,0x24,0x20
};
static const u8 EXPECT_HELI_HELPER[] = {
    0x48,0x89,0x5C,0x24,0x08,
    0x57,
    0x48,0x83,0xEC,0x30,
    0x49,0x8B,0xD8,
    0x48,0x8B,0xFA
};
static const u8 EXPECT_ROAD_DO_PANEL[] = {
    0x48,0x8B,0xC4,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x8D,0xA8,0xB8,0xF1,0xFF,0xFF
};
static const u8 EXPECT_ASSIGN_PUSH[] = {
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x48,0x8B,0xD9
};
static const u8 EXPECT_ASSIGN_INIT[] = {
    0x33,0xC0,0x48,0x89,0x41,0x08,0x48,0x89,0x41,0x10,0x48,0x89,0x41,0x18
};

// --------------------------------------------------------------------- minimal memory helpers
static int BytesEqual(const u8* a, const u8* b, usize n)
{
    if (!a || !b) return 0;
    for (usize i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static int IsCanonicalUserRange(const void* p, usize n)
{
    const u64 first = (u64)p;
    const u64 userMax = 0x00007FFFFFFFFFFFull;
    if (first < 0x10000ull || first > userMax) return 0;
    if (n == 0) return 1;
    const u64 span = (u64)n - 1ull;
    if (span > userMax - first) return 0;
    return 1;
}

static int IsReadable(const void* p, usize n)
{
    return H && H->readablePtr && IsCanonicalUserRange(p, n) && H->readablePtr(p, n);
}

static void ZeroBytes(void* memory, usize count)
{
    u8* p = (u8*)memory;
    if (!p) return;
    for (usize i = 0; i < count; ++i) p[i] = 0;
}

static void* ReadPointer(void* base, usize offset)
{
    u8* p = (u8*)base;
    if (!p || !IsReadable(p + offset, sizeof(void*))) return 0;
    return *(void**)(p + offset);
}

static int ReadInt(void* base, usize offset, int* out)
{
    u8* p = (u8*)base;
    if (!p || !out || !IsReadable(p + offset, sizeof(int))) return 0;
    *out = *(int*)(p + offset);
    return 1;
}

static float ReadFloatOr(void* base, usize offset, float fallback)
{
    u8* p = (u8*)base;
    if (!p || !IsReadable(p + offset, sizeof(float))) return fallback;
    return *(float*)(p + offset);
}

static float GlobalFloat(usize rva, float fallback)
{
    if (!EXE || !IsReadable(EXE + rva, sizeof(float))) return fallback;
    return *(float*)(EXE + rva);
}

static int IsHdoBuilding(void* building)
{
    if (!building) return 0;
    void* td = ReadPointer(building, B_TYPEDESC);
    int type = -1, subtype = -1;
    if (!td || !ReadInt(td, TD_TYPE, &type) || !ReadInt(td, TD_SUBTYPE, &subtype)) return 0;
    return type == BUILDING_DISTRIBUTION_OFFICE && subtype == SUBTYPE_AIRPLANE;
}

// --------------------------------------------------------------------- helicopter compatibility
static int IsOurHelicopterCase(u64 building, i64 vehicleDef)
{
    if (!building || !vehicleDef || !IsHdoBuilding((void*)building)) return 0;
    int vehicleType = -1;
    if (!ReadInt((void*)vehicleDef, V_TYPE, &vehicleType)) return 0;
    return vehicleType == VEHICLE_HELICOPTER;
}

static u64 h_VehicleBuildingCompat(u8* game, u64 building,
                                   i64 vehicleDef, char mode,
                                   unsigned int* errorOut)
{
    if (IsOurHelicopterCase(building, vehicleDef))
    {
        u64 ok = f_HelicopterPadCompat(game, building, vehicleDef);
        return ok;
    }
    return o_VehicleBuildingCompat(game, building, vehicleDef, mode, errorOut);
}

// --------------------------------------------------------------------- mixed-fleet task filtering
static int IsHelicopterVehicleObject(void* vehicle)
{
    void* vehicleDef = ReadPointer(vehicle, V_OBJECT_DEF);
    int vehicleType = -1;
    return vehicleDef && ReadInt(vehicleDef, V_TYPE, &vehicleType) &&
           vehicleType == VEHICLE_HELICOPTER;
}

// This mirrors the native helicopter-station representation used by
// FUN_1403e3b50. A cargo heliport is AIRPLANE-subtype and contains at least one
// air/heli station record in the building's own 0xA40 vector. Ordinary airport
// cargo terminals do not expose HELIPORT_STATION records here, so trucks remain
// valid for those facilities.
static int HasNativeHeliportStation(void* building)
{
    if (!building) return 0;

    void* td = ReadPointer(building, B_TYPEDESC);
    int subtype = -1;
    if (!td || !ReadInt(td, TD_SUBTYPE, &subtype) || subtype != SUBTYPE_AIRPLANE)
        return 0;

    u8* b = (u8*)building;
    if (!IsReadable(b + B_STATION_BEGIN, 16)) return 0;
    u8* begin = *(u8**)(b + B_STATION_BEGIN);
    u8* end   = *(u8**)(b + B_STATION_END);
    if (!begin || !end || end < begin) return 0;

    usize bytes = (usize)(end - begin);
    if ((bytes % STATION_SIZE) != 0) return 0;
    usize count = bytes / STATION_SIZE;
    if (count > 128 || (count && !IsReadable(begin, bytes))) return 0;

    for (usize i = 0; i < count; ++i)
    {
        int kind = *(int*)(begin + i * STATION_SIZE + STATION_KIND);
        // Native helicopter/air-station family checked by FUN_1403e3b50.
        if ((unsigned)(kind - 0x1F) < 5u) return 1;
    }
    return 0;
}

static int TaskTouchesCargoHeliport(i64* task)
{
    if (!task || !IsReadable(task, 0x80)) return 0;
    void* source = *(void**)((u8*)task + TASK_SOURCE);
    void* dest   = *(void**)((u8*)task + TASK_DEST);
    return HasNativeHeliportStation(source) || HasNativeHeliportStation(dest);
}

static int IsExactOverseasNode(void* game, void* building)
{
    if (!game || !building) return 0;
    void* soviet = ReadPointer(game, G_SOVIET_NODE);
    void* western = ReadPointer(game, G_WESTERN_NODE);
    return building == soviet || building == western;
}

static int IsAirReachableTaskEndpoint(void* game, void* building)
{
    if (!building) return 0;
    return IsHdoBuilding(building) || HasNativeHeliportStation(building) ||
           IsExactOverseasNode(game, building);
}

// Returns 1 for a fully air-reachable row, 0 for a row containing at least one
// road-only endpoint, and -1 when the record is malformed. Malformed records are
// left to the native validator instead of being reinterpreted by the plugin.
static int TaskAirReachability(void* game, i64* task)
{
    if (!task || !IsReadable(task, 0x80)) return -1;
    void* source = *(void**)((u8*)task + TASK_SOURCE);
    void* dest   = *(void**)((u8*)task + TASK_DEST);
    if (!source || !dest) return -1;
    return IsAirReachableTaskEndpoint(game, source) &&
           IsAirReachableTaskEndpoint(game, dest);
}

static int ReadHdoAssignmentRecords(void* office, void*** beginOut,
                                    void*** endOut)
{
    if (!office || !beginOut || !endOut ||
        !IsReadable((u8*)office + B_ASSIGN_BEGIN, 16))
        return 0;

    void** begin = *(void***)((u8*)office + B_ASSIGN_BEGIN);
    void** end = *(void***)((u8*)office + B_ASSIGN_END);
    if ((!begin && end) || (begin && !end) || (begin && end < begin)) return 0;
    usize count = begin ? (usize)(end - begin) : 0;
    if (count > 64u || (count && !IsReadable(begin, count * sizeof(void*))))
        return 0;
    *beginOut = begin;
    *endOut = end;
    return 1;
}

// The native office stores road-network reachability in record[0]. A task row
// retains the source/destination assignment indices at +0x18/+0x50, allowing
// the mixed dispatcher to project the same shared task index back to the road
// view without asking a truck to interpret an air-only endpoint.
static int TaskRoadReachability(void* office, i64* task)
{
    if (!office || !task || !IsReadable(task, 0x80)) return -1;
    void** begin = 0;
    void** end = 0;
    if (!ReadHdoAssignmentRecords(office, &begin, &end) || !begin) return -1;

    int sourceIndex = *(int*)((u8*)task + TASK_SOURCE_ASSIGN_INDEX);
    int destIndex = *(int*)((u8*)task + TASK_DEST_ASSIGN_INDEX);
    usize count = (usize)(end - begin);
    if (sourceIndex < 0 || destIndex < 0 ||
        (usize)sourceIndex >= count || (usize)destIndex >= count)
        return -1;

    u8* sourceRecord = (u8*)begin[sourceIndex];
    u8* destRecord = (u8*)begin[destIndex];
    if (!sourceRecord || !destRecord ||
        !IsReadable(sourceRecord, 1) || !IsReadable(destRecord, 1))
        return -1;
    return sourceRecord[0] != 0 && destRecord[0] != 0;
}

static int TaskUsesDedicatedAirEndpoint(void* game, i64* task)
{
    if (!game || !task || !IsReadable(task, 0x80)) return 0;
    void* source = *(void**)((u8*)task + TASK_SOURCE);
    void* dest = *(void**)((u8*)task + TASK_DEST);
    return HasNativeHeliportStation(source) || HasNativeHeliportStation(dest) ||
           IsExactOverseasNode(game, source) || IsExactOverseasNode(game, dest);
}

// FUN_1401c6050 refreshes record[0] solely from the road graph. Its following
// FUN_1401e2380 rebuild therefore deletes heliport/customs pairs even though
// they remain reachable to an HDO helicopter. At the rebuild boundary, expose
// air-reachable records in addition to the native road-reachable records. The
// original bytes are restored synchronously; only the derived 0x80-byte task
// index remains mixed, and assignment transactions project that index by
// vehicle type below.
static void h_DistributionTaskRebuild(void* game, void* office)
{
    if (!o_DistributionTaskRebuild) return;
    if (!game || !office || !IsHdoBuilding(office))
    {
        o_DistributionTaskRebuild(game, office);
        return;
    }

    void** begin = 0;
    void** end = 0;
    if (!ReadHdoAssignmentRecords(office, &begin, &end) || !begin)
    {
        o_DistributionTaskRebuild(game, office);
        return;
    }

    usize count = (usize)(end - begin);
    u8* records[64] = {};
    u8 savedReachability[64] = {};
    usize promoted = 0;
    for (usize i = 0; i < count; ++i)
    {
        u8* record = (u8*)begin[i];
        if (!record || !IsReadable(record, 0x10)) continue;
        records[i] = record;
        savedReachability[i] = record[0];
        void* target = *(void**)(record + 8);
        if (record[0] == 0 && IsAirReachableTaskEndpoint(game, target))
        {
            record[0] = 1;
            ++promoted;
        }
    }

    o_DistributionTaskRebuild(game, office);

    for (usize i = 0; i < count; ++i)
        if (records[i] && IsReadable(records[i], 1))
            records[i][0] = savedReachability[i];

    if (promoted && g_mixedTaskRebuildLogCount < 24 && H && H->log)
    {
        usize rows = 0;
        if (IsReadable((u8*)office + B_TASK_BEGIN, 16))
        {
            u8* taskBegin = *(u8**)((u8*)office + B_TASK_BEGIN);
            u8* taskEnd = *(u8**)((u8*)office + B_TASK_END);
            if (taskBegin && taskEnd && taskEnd >= taskBegin &&
                ((usize)(taskEnd - taskBegin) % 0x80u) == 0)
                rows = (usize)(taskEnd - taskBegin) / 0x80u;
        }
        ++g_mixedTaskRebuildLogCount;
        H->log("helido    mixed HDO task-index rebuild #%u: air_records_restored=%llu indexed_rows=%llu",
               g_mixedTaskRebuildLogCount, (u64)promoted, (u64)rows);
    }
}

static u64 h_DoTaskEligibility(i64 game, i64* task, i64 vehicle, i64 office)
{
    // Only the mixed Helicopter Distribution Office gets this split. A cargo
    // heliport remains a perfectly normal road-accessible building everywhere
    // else in the game.
    if (!office || !vehicle || !IsHdoBuilding((void*)office))
        return o_DoTaskEligibility(game, task, vehicle, office);

    if (!IsHelicopterVehicleObject((void*)vehicle))
    {
        if (TaskTouchesCargoHeliport(task)) return 0;
        return o_DoTaskEligibility(game, task, vehicle, office);
    }

    // FUN_1401e5480 scans every 0x80-byte row and continues after a false
    // eligibility result. Rejecting only the rows this helicopter cannot reach
    // is therefore the middle dispatch layer: the same native scan can continue
    // to later heliport/overseas rows without constructing a road route first.
    int airReachability = TaskAirReachability((void*)game, task);
    if (airReachability == 0)
    {
        if (!g_loggedHelicopterTaskSkip && H && H->log)
        {
            g_loggedHelicopterTaskSkip = 1;
            H->log("helido    mixed-fleet dispatcher skipped a road-only/mixed-endpoint task for an HDO helicopter");
        }
        return 0;
    }

    u64 result = o_DoTaskEligibility(game, task, vehicle, office);
    if (airReachability == 1 && (char)result != 0 &&
        g_helicopterTaskAdmitCount < 16 && H && H->log)
    {
        ++g_helicopterTaskAdmitCount;
        H->log("helido    mixed-fleet dispatcher admitted air-reachable task #%u through native eligibility",
               g_helicopterTaskAdmitCount);
    }
    return result;
}

// Intercept every native FUN_1401e5350 call, including calls made inside the
// stock office monitor before the plugin's post-scan queue pass. The native
// function sorts and selects against the whole vector before/around its
// per-row eligibility callback, and its loaded-vehicle path can bypass that
// callback. During one transaction, replace the office's vector header with a
// private type-specific projection: fully air-reachable rows for helicopters,
// and currently road-reachable non-heliport rows for road vehicles. Restore
// all three vector pointers synchronously before returning.
static u64 h_DistributionAssignVehicle(void* game, void* vehicle, void* office)
{
    if (!game || !vehicle || !office || !IsHdoBuilding(office) ||
        !o_DistributionAssignVehicle)
        return o_DistributionAssignVehicle
                   ? o_DistributionAssignVehicle(game, vehicle, office) : 0;

    int helicopter = IsHelicopterVehicleObject(vehicle);

    // A recursive call already sees the filtered header owned by the outer
    // transaction, so it can safely continue through the original trampoline.
    if (g_airTaskViewActive)
        return o_DistributionAssignVehicle(game, vehicle, office);

    if (!IsReadable((u8*)office + B_TASK_BEGIN, 24))
        return o_DistributionAssignVehicle(game, vehicle, office);

    u8** beginField = (u8**)((u8*)office + B_TASK_BEGIN);
    u8** endField = (u8**)((u8*)office + B_TASK_END);
    u8** capField = (u8**)((u8*)office + B_TASK_CAP);
    u8* savedBegin = *beginField;
    u8* savedEnd = *endField;
    u8* savedCap = *capField;
    if (!savedBegin || !savedEnd || savedEnd < savedBegin)
        return o_DistributionAssignVehicle(game, vehicle, office);

    usize bytes = (usize)(savedEnd - savedBegin);
    if ((bytes % 0x80u) != 0 || bytes / 0x80u > AIR_TASK_VIEW_MAX_ROWS ||
        (bytes && !IsReadable(savedBegin, bytes)))
    {
        if (g_airTaskViewRejectCount < 8 && H && H->log)
        {
            ++g_airTaskViewRejectCount;
            H->log("helido    per-vehicle assignment view refused malformed native task vector #%u",
                   g_airTaskViewRejectCount);
        }
        return o_DistributionAssignVehicle(game, vehicle, office);
    }

    usize totalRows = bytes / 0x80u;
    usize projectedRows = 0;
    for (usize i = 0; i < totalRows; ++i)
    {
        u8* row = savedBegin + i * 0x80u;
        int include = 0;
        if (helicopter)
        {
            include = TaskAirReachability(game, (i64*)row) == 1;
        }
        else
        {
            int roadReachability = TaskRoadReachability(office, (i64*)row);
            include = roadReachability == 1 &&
                      !TaskUsesDedicatedAirEndpoint(game, (i64*)row);
        }
        if (!include) continue;
        u8* copy = g_airTaskView + projectedRows * 0x80u;
        for (usize j = 0; j < 0x80u; ++j) copy[j] = row[j];
        ++projectedRows;
    }

    // A vector already valid for this vehicle needs no presentation change.
    if (projectedRows == totalRows)
        return o_DistributionAssignVehicle(game, vehicle, office);

    *beginField = g_airTaskView;
    *endField = g_airTaskView + projectedRows * 0x80u;
    *capField = g_airTaskView + projectedRows * 0x80u;
    g_airTaskViewActive = 1;

    if (g_airTaskViewLogCount < 24 && H && H->log)
    {
        ++g_airTaskViewLogCount;
        H->log("helido    %s assignment view #%u: mixed_rows=%llu compatible_rows=%llu hidden_rows=%llu",
               helicopter ? "helicopter" : "road-vehicle",
               g_airTaskViewLogCount, (u64)totalRows, (u64)projectedRows,
               (u64)(totalRows - projectedRows));
    }

    u64 result = o_DistributionAssignVehicle(game, vehicle, office);

    *beginField = savedBegin;
    *endField = savedEnd;
    *capField = savedCap;
    g_airTaskViewActive = 0;

    // FUN_1401e5480 normally finishes a successful assignment by refreshing
    // all rows sharing the selected source or destination. That refresh ran
    // against the temporary copy above. Recompute the complete native vector
    // now so a following road vehicle or helicopter in the same office scan
    // sees the reservation made by this assignment. No copied row is written
    // back and task ordering in the game-owned vector remains unchanged.
    if ((u8)result != 0 && f_DistributionTaskRefresh)
    {
        for (usize i = 0; i < totalRows; ++i)
            f_DistributionTaskRefresh(game, savedBegin + i * 0x80u);
    }
    return result;
}

// --------------------------------------------------------------------- HDO task-queue bridge
static int ReadPointerRange(void* owner, usize beginOffset, usize endOffset,
                            void*** beginOut, void*** endOut, usize maximumCount)
{
    if (!owner || !beginOut || !endOut ||
        !IsReadable((u8*)owner + beginOffset, sizeof(void*)) ||
        !IsReadable((u8*)owner + endOffset, sizeof(void*)))
        return 0;

    void** begin = *(void***)((u8*)owner + beginOffset);
    void** end = *(void***)((u8*)owner + endOffset);
    if (!begin && !end)
    {
        *beginOut = 0;
        *endOut = 0;
        return 1;
    }
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > maximumCount || (count && !IsReadable(begin, count * sizeof(void*))))
        return 0;
    *beginOut = begin;
    *endOut = end;
    return 1;
}

static int HasNativeDistributionTasks(void* office)
{
    if (!office || !IsReadable((u8*)office + B_TASK_BEGIN, 16)) return 0;
    u8* begin = *(u8**)((u8*)office + B_TASK_BEGIN);
    u8* end = *(u8**)((u8*)office + B_TASK_END);
    if (!begin || !end || end <= begin) return 0;
    usize bytes = (usize)(end - begin);
    return (bytes % 0x80u) == 0 && bytes / 0x80u <= 256u && IsReadable(begin, bytes);
}

static int HasNativeDistributionAssignments(void* office)
{
    void** begin = 0;
    void** end = 0;
    return ReadPointerRange(office, B_ASSIGN_BEGIN, B_ASSIGN_END,
                            &begin, &end, 64u) && begin && end > begin;
}

// Reproduce the task-preparation phase immediately preceding the native
// FUN_1401e5350 loop in FUN_1401c6050. Structural UI edits rebuild the native
// task vector first; every dispatch opportunity then refreshes each 0x80-byte
// row's live supply, demand, threshold and reservation values. The plugin does
// not create or own a parallel task representation.
static int RefreshNativeHdoTasks(void* game, void* office, int forceRebuild)
{
    if (!game || !office || !f_DistributionTaskRebuild ||
        !f_DistributionTaskRefresh || !HasNativeDistributionAssignments(office))
        return 0;

    int dirty = 0;
    if (IsReadable((u8*)office + B_ASSIGN_DIRTY, 1))
        dirty = *(u8*)((u8*)office + B_ASSIGN_DIRTY) != 0;
    // An HDO restored from a save can already have a populated UI assignment
    // list while its derived 0x80-byte task vector has not yet been rebuilt.
    // Treat an absent task vector as structural dirtiness even when the saved
    // dirty byte is clear.
    int missing = !HasNativeDistributionTasks(office);
    if (forceRebuild || dirty || missing)
    {
        usize beforeCount = 0;
        if (!missing)
        {
            u8* beforeBegin = *(u8**)((u8*)office + B_TASK_BEGIN);
            u8* beforeEnd = *(u8**)((u8*)office + B_TASK_END);
            beforeCount = (usize)(beforeEnd - beforeBegin) / 0x80u;
        }
        f_DistributionTaskRebuild(game, office);

        if (g_structuralRebuildLogCount < 16 && H && H->log)
        {
            usize afterCount = 0;
            if (HasNativeDistributionTasks(office))
            {
                u8* afterBegin = *(u8**)((u8*)office + B_TASK_BEGIN);
                u8* afterEnd = *(u8**)((u8*)office + B_TASK_END);
                afterCount = (usize)(afterEnd - afterBegin) / 0x80u;
            }
            ++g_structuralRebuildLogCount;
            H->log("helido    native HDO structural task rebuild #%u: forced=%d dirty=%d missing=%d rows=%llu->%llu",
                   g_structuralRebuildLogCount, forceRebuild, dirty, missing,
                   (u64)beforeCount, (u64)afterCount);
        }
    }

    if (!HasNativeDistributionTasks(office)) return 0;
    u8* begin = *(u8**)((u8*)office + B_TASK_BEGIN);
    u8* end = *(u8**)((u8*)office + B_TASK_END);
    usize count = (usize)(end - begin) / 0x80u;
    for (usize i = 0; i < count; ++i)
        f_DistributionTaskRefresh(game, begin + i * 0x80u);
    return count != 0;
}

static int NativeDistributionPassWasDue(void* game, void* office,
                                        float timerBefore, float timerAfter)
{
    int distributionActive = 0;
    if (!game || !office ||
        !ReadInt(game, GAME_DISTRIBUTION_ACTIVE, &distributionActive) ||
        distributionActive <= 0 || !HasNativeDistributionAssignments(office))
        return 0;

    // FUN_1401c6050 counts the timer down itself. When its normal candidate
    // list is empty it leaves a due timer at/below zero. When it runs a normal
    // candidate pass it resets the timer, producing a clear upward transition.
    return timerAfter <= 0.0f || timerAfter > timerBefore + 0.25f;
}

static int IsParkedHdoHelicopterCandidate(void* vehicle, void* office)
{
    if (!vehicle || !office || !IsHelicopterVehicleObject(vehicle) ||
        !IsReadable((u8*)vehicle + V_ROUTE_ACTIVE, 1) ||
        *(u8*)((u8*)vehicle + V_ROUTE_ACTIVE) != 0 ||
        ReadPointer(vehicle, V_HOME) != office ||
        ReadPointer(vehicle, V_ARRIVAL) != 0 ||
        ReadPointer(vehicle, V_PARKING_OCCUPANCY) != office)
        return 0;

    void* current = ReadPointer(vehicle, V_CURRENT_BUILDING);
    int state = -1;
    return (current == 0 || current == office) &&
           ReadInt(vehicle, V_HELICOPTER_STATE, &state) &&
           state >= 0 && state <= 3;
}

static u64 HashQueueValue(u64 hash, u64 value)
{
    hash ^= value;
    return hash * 1099511628211ull;
}

static u64 HdoNativeTaskSignature(void* office)
{
    if (!office || !IsReadable((u8*)office + B_TASK_BEGIN, 16)) return 0;
    u8* begin = *(u8**)((u8*)office + B_TASK_BEGIN);
    u8* end = *(u8**)((u8*)office + B_TASK_END);
    if (!begin && !end) return HashQueueValue(1469598103934665603ull, 0);
    if (!begin || !end || end < begin) return 0;
    usize bytes = (usize)(end - begin);
    if ((bytes % 0x80u) != 0 || bytes / 0x80u > 256u ||
        (bytes && !IsReadable(begin, bytes)))
        return 0;

    usize count = bytes / 0x80u;
    u64 rowXor = 0;
    u64 rowSum = 0;
    for (usize i = 0; i < count; ++i)
    {
        u8* row = begin + i * 0x80u;
        u64 rowHash = 1469598103934665603ull;
        rowHash = HashQueueValue(rowHash, *(u64*)(row + 0x00));
        rowHash = HashQueueValue(rowHash, (u64)*(void**)(row + TASK_SOURCE));
        rowHash = HashQueueValue(rowHash, (u64)*(void**)(row + TASK_DEST));
        rowXor ^= rowHash;
        rowSum += rowHash;
    }
    u64 hash = HashQueueValue(1469598103934665603ull, (u64)count);
    hash = HashQueueValue(hash, rowXor);
    return HashQueueValue(hash, rowSum);
}

static u64 HdoAssignmentSignature(void* office)
{
    void** begin = 0;
    void** end = 0;
    if (!ReadPointerRange(office, B_ASSIGN_BEGIN, B_ASSIGN_END,
                          &begin, &end, 64u))
        return 0;

    usize count = begin ? (usize)(end - begin) : 0;
    u64 hash = HashQueueValue(1469598103934665603ull, (u64)count);
    for (usize i = 0; i < count; ++i)
    {
        u8* record = (u8*)begin[i];
        if (!record || !IsReadable(record, 0x18))
        {
            hash = HashQueueValue(hash, 0xBAD0000000000000ull + (u64)i);
            continue;
        }

        hash = HashQueueValue(hash, (u64)*(void**)(record + 0x08));
        u8* config = *(u8**)(record + 0x10);
        if (!config || !IsReadable(config, 0xF1))
        {
            hash = HashQueueValue(hash, 0xBAD1000000000000ull + (u64)i);
            continue;
        }

        // Stable source/destination enable, cargo and threshold controls from
        // the two halves of the native 0x198-byte assignment configuration.
        // record[0] is deliberately excluded: FUN_1401c6050 rewrites it from
        // road-network reachability, so it is runtime state rather than a UI
        // assignment edit.
        hash = HashQueueValue(hash, (u64)config[0x00]);
        hash = HashQueueValue(hash, *(u64*)(config + 0x08));
        hash = HashQueueValue(hash, *(u64*)(config + 0x10));
        hash = HashQueueValue(hash, *(u64*)(config + 0x20));
        hash = HashQueueValue(hash, (u64)config[0x28]);
        hash = HashQueueValue(hash, (u64)config[0xC8]);
        hash = HashQueueValue(hash, *(u64*)(config + 0xD0));
        hash = HashQueueValue(hash, *(u64*)(config + 0xD8));
        hash = HashQueueValue(hash, *(u64*)(config + 0xE8));
        hash = HashQueueValue(hash, (u64)config[0xF0]);
    }
    return hash;
}

struct HdoVehicleSignatures
{
    u64 residents;
    u64 airState;
    u64 parked;
    int parkedCount;
};

static int BuildHdoVehicleSignatures(void* office, HdoVehicleSignatures* out)
{
    if (!out) return 0;
    out->residents = HashQueueValue(1469598103934665603ull, 0);
    out->airState = HashQueueValue(1469598103934665603ull, 0);
    out->parked = HashQueueValue(1469598103934665603ull, 0);
    out->parkedCount = 0;

    void** begin = 0;
    void** end = 0;
    if (!ReadPointerRange(office, B_VEHICLE_BEGIN, B_VEHICLE_END,
                          &begin, &end, 128u))
        return 0;

    usize residentCount = begin ? (usize)(end - begin) : 0;
    out->residents = HashQueueValue(out->residents, (u64)residentCount);
    int helicopterCount = 0;
    for (usize i = 0; i < residentCount; ++i)
    {
        void* vehicle = begin[i];
        out->residents = HashQueueValue(out->residents, (u64)vehicle);
        if (!vehicle || !IsHelicopterVehicleObject(vehicle)) continue;

        ++helicopterCount;
        out->airState = HashQueueValue(out->airState, (u64)vehicle);
        if (IsReadable((u8*)vehicle + V_ROUTE_ACTIVE, 1))
            out->airState = HashQueueValue(out->airState,
                                           (u64)*(u8*)((u8*)vehicle + V_ROUTE_ACTIVE));
        out->airState = HashQueueValue(out->airState,
                                       (u64)ReadPointer(vehicle, V_CURRENT_BUILDING));
        out->airState = HashQueueValue(out->airState,
                                       (u64)ReadPointer(vehicle, V_ARRIVAL));
        out->airState = HashQueueValue(out->airState,
                                       (u64)ReadPointer(vehicle, V_PARKING_OCCUPANCY));
        int state = -1;
        ReadInt(vehicle, V_HELICOPTER_STATE, &state);
        out->airState = HashQueueValue(out->airState, (u64)(u32)state);

        if (IsParkedHdoHelicopterCandidate(vehicle, office))
        {
            ++out->parkedCount;
            out->parked = HashQueueValue(out->parked, (u64)vehicle);
            out->parked = HashQueueValue(out->parked, (u64)(u32)state);
        }
    }
    out->airState = HashQueueValue(out->airState, (u64)helicopterCount);
    out->parked = HashQueueValue(out->parked, (u64)out->parkedCount);
    return 1;
}

enum HdoQueueTrigger
{
    HDO_TRIGGER_INITIAL       = 0x01,
    HDO_TRIGGER_ASSIGNMENT_CHANGE = 0x02,
    HDO_TRIGGER_FLEET_CHANGE  = 0x04,
    HDO_TRIGGER_AIR_STATE     = 0x08,
    HDO_TRIGGER_UI_EDIT       = 0x10,
    HDO_TRIGGER_UI_CLOSE      = 0x20,
    HDO_TRIGGER_PERIODIC      = 0x40,
    HDO_TRIGGER_RETURN        = 0x80,
    HDO_TRIGGER_TASK_STATE    = 0x100
};

struct HdoQueueSlot
{
    void* office;
    void* game;
    u64 taskSignature;
    u64 assignmentSignature;
    u64 residentSignature;
    u64 airStateSignature;
    u64 parkedSignature;
    u32 dirtyReasons;
    u32 panelPulse;
    u32 panelPulseAtScan;
    u8 initialized;
    u8 panelWasOpen;
    u8 panelQuietScans;
};

static HdoQueueSlot g_hdoQueueSlots[64];
static u32 g_hdoQueueReplaceIndex = 0;

static HdoQueueSlot* FindHdoQueueSlot(void* office, int create)
{
    if (!office) return 0;
    for (usize i = 0; i < 64; ++i)
        if (g_hdoQueueSlots[i].office == office) return &g_hdoQueueSlots[i];
    if (!create) return 0;

    for (usize i = 0; i < 64; ++i)
    {
        if (!g_hdoQueueSlots[i].office)
        {
            ZeroBytes(&g_hdoQueueSlots[i], sizeof(HdoQueueSlot));
            g_hdoQueueSlots[i].office = office;
            return &g_hdoQueueSlots[i];
        }
    }

    HdoQueueSlot* slot = &g_hdoQueueSlots[g_hdoQueueReplaceIndex++ % 64u];
    ZeroBytes(slot, sizeof(HdoQueueSlot));
    slot->office = office;
    return slot;
}

static void MarkHdoQueueDirty(void* office, u32 reasons)
{
    HdoQueueSlot* slot = FindHdoQueueSlot(office, 1);
    if (slot) slot->dirtyReasons |= reasons;
}

static void NoteHdoPanelPulse(void* office, int edited)
{
    HdoQueueSlot* slot = FindHdoQueueSlot(office, 1);
    if (!slot) return;
    ++slot->panelPulse;
    if (slot->panelPulse == 0) ++slot->panelPulse;
    if (edited)
        slot->dirtyReasons |= HDO_TRIGGER_UI_EDIT |
                              HDO_TRIGGER_ASSIGNMENT_CHANGE;
}

static u32 DetectHdoPanelClose(HdoQueueSlot* slot)
{
    if (!slot) return 0;
    if (slot->panelPulse != slot->panelPulseAtScan)
    {
        slot->panelPulseAtScan = slot->panelPulse;
        slot->panelWasOpen = 1;
        slot->panelQuietScans = 0;
        return 0;
    }
    if (!slot->panelWasOpen) return 0;
    if (slot->panelQuietScans < 8) ++slot->panelQuietScans;
    if (slot->panelQuietScans < 8) return 0;
    slot->panelWasOpen = 0;
    slot->panelQuietScans = 0;
    return HDO_TRIGGER_UI_CLOSE;
}

// The stock predicate assumes a route-at-home or current-building-at-home view.
// A returned HDO helicopter instead has an empty route, null current building,
// own-HDO pad occupancy, and state 1/3. Present the load-like values only during
// the native predicate so all of its breakdown and maintenance checks still
// decide whether dispatch is safe. Restore both fields before route creation.
static int ProbeQueueOperationalReadiness(void* vehicle, void* office,
                                          int* nativeReadyOut)
{
    if (nativeReadyOut) *nativeReadyOut = 0;
    if (!vehicle || !office || !f_VehicleReady ||
        !IsReadable((u8*)vehicle + V_CURRENT_BUILDING, sizeof(void*)) ||
        !IsReadable((u8*)vehicle + V_HELICOPTER_STATE, sizeof(int)))
        return 0;

    int nativeReady = f_VehicleReady(vehicle, '\x01') ? 1 : 0;
    if (nativeReadyOut) *nativeReadyOut = nativeReady;
    if (nativeReady) return 1;

    void** currentField = (void**)((u8*)vehicle + V_CURRENT_BUILDING);
    int* stateField = (int*)((u8*)vehicle + V_HELICOPTER_STATE);
    void* savedCurrent = *currentField;
    int savedState = *stateField;
    if ((savedCurrent != 0 && savedCurrent != office) ||
        savedState < 0 || savedState > 3 ||
        ReadPointer(vehicle, V_READINESS_BLOCKER) != 0)
        return 0;

    *currentField = office;
    *stateField = 0;
    int ready = f_VehicleReady(vehicle, '\x01') ? 1 : 0;
    *stateField = savedState;
    *currentField = savedCurrent;
    return ready;
}

static void* RouteTargetRelative(void* vehicle, int relativeIndex)
{
    if (!vehicle || !IsReadable((u8*)vehicle + V_ROUTE_BEGIN, 0x20)) return 0;
    void** begin = *(void***)((u8*)vehicle + V_ROUTE_BEGIN);
    void** end = *(void***)((u8*)vehicle + V_ROUTE_END);
    int index = *(int*)((u8*)vehicle + V_ROUTE_INDEX);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (!count || count > 256u || !IsReadable(begin, count * sizeof(void*))) return 0;
    if (index < 0 || (usize)index >= count) return 0;

    int adjusted = index + relativeIndex;
    while (adjusted < 0) adjusted += (int)count;
    adjusted %= (int)count;
    return begin[adjusted];
}

static usize NativeRouteStopCount(void* vehicle)
{
    if (!vehicle || !IsReadable((u8*)vehicle + V_ROUTE_BEGIN, 16)) return 0;
    void** begin = *(void***)((u8*)vehicle + V_ROUTE_BEGIN);
    void** end = *(void***)((u8*)vehicle + V_ROUTE_END);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    return count <= 256u ? count : 0;
}

// FUN_1401e5480 has already built the native [source, destination, HDO]
// route when it returns true. While a helicopter still occupies its HDO pad,
// FUN_1406b8eb0 intentionally queues departure for the helicopter updater.
// Treat that outbound route as the completion latch instead of calling the
// assignment routine again on every state/timer pulse.
static int HasQueuedOutboundHdoRoute(void* vehicle, void* office)
{
    if (!vehicle || !office || ReadPointer(vehicle, V_ARRIVAL) != 0 ||
        ReadPointer(vehicle, V_PARKING_OCCUPANCY) != office)
        return 0;
    void* target = RouteTargetRelative(vehicle, 0);
    return target && target != office;
}

struct HdoDispatchResult
{
    int candidates;
    int assigned;
};

static HdoDispatchResult DispatchHdoQueue(void* game, void* office, int forceRebuild)
{
    HdoDispatchResult result = {};
    if (!RefreshNativeHdoTasks(game, office, forceRebuild)) return result;

    void** begin = 0;
    void** end = 0;
    if (!ReadPointerRange(office, B_VEHICLE_BEGIN, B_VEHICLE_END,
                          &begin, &end, 128u) || !begin)
        return result;

    for (void** cursor = begin; cursor != end; ++cursor)
    {
        void* vehicle = *cursor;
        if (!IsParkedHdoHelicopterCandidate(vehicle, office)) continue;

        if (HasQueuedOutboundHdoRoute(vehicle, office))
        {
            if (g_queuePendingRouteSkipCount < 12 && H && H->log)
            {
                ++g_queuePendingRouteSkipCount;
                H->log("helido    HDO queue retained pending native helicopter departure #%u",
                       g_queuePendingRouteSkipCount);
            }
            continue;
        }
        ++result.candidates;

        int state = -1;
        ReadInt(vehicle, V_HELICOPTER_STATE, &state);
        int nativeReady = 0;
        int queueReady = ProbeQueueOperationalReadiness(vehicle, office, &nativeReady);
        if (g_queueCandidateLogCount < 24 && H && H->log)
        {
            ++g_queueCandidateLogCount;
            H->log("helido    HDO queue candidate #%u: native_ready=%d queue_ready=%d state=%d current_is_home=%d current_is_null=%d",
                   g_queueCandidateLogCount, nativeReady, queueReady, state,
                   ReadPointer(vehicle, V_CURRENT_BUILDING) == office,
                   ReadPointer(vehicle, V_CURRENT_BUILDING) == 0);
        }
        if (!queueReady)
        {
            if (g_queueSafetyRejectLogCount < 12 && H && H->log)
            {
                ++g_queueSafetyRejectLogCount;
                H->log("helido    HDO queue left parked helicopter idle because native operational readiness rejected it");
            }
            continue;
        }

        u64 assigned = f_DistributionAssignVehicle(game, vehicle, office);
        if ((u8)assigned != 0)
        {
            ++result.assigned;
            ++g_queueAssignmentCount;
            if (g_queueAssignmentCount <= 24 && H && H->log)
                H->log("helido    HDO queue dispatched native helicopter assignment #%u",
                       g_queueAssignmentCount);
        }
        else if (g_queueNoAssignmentLogCount < 24 && H && H->log)
        {
            ++g_queueNoAssignmentLogCount;
            H->log("helido    HDO queue found no currently eligible air task for a parked helicopter");
        }
    }
    return result;
}

static void StoreHdoQueueSnapshot(HdoQueueSlot* slot, void* office)
{
    if (!slot || !office) return;
    HdoVehicleSignatures vehicles = {};
    BuildHdoVehicleSignatures(office, &vehicles);
    slot->taskSignature = HdoNativeTaskSignature(office);
    slot->assignmentSignature = HdoAssignmentSignature(office);
    slot->residentSignature = vehicles.residents;
    slot->airStateSignature = vehicles.airState;
    slot->parkedSignature = vehicles.parked;
    slot->initialized = 1;
}

static void h_DistributionOfficeScan(void* game, void* office)
{
    int ours = IsHdoBuilding(office);
    float timerBefore = ours ? ReadFloatOr(office, B_ASSIGN_TIMER, 1000000.0f) : 1000000.0f;

    // Road vehicles and every stock Distribution Office path run first.
    o_DistributionOfficeScan(game, office);

    if (!ours || !g_initReady || !f_VehicleReady || !f_DistributionAssignVehicle) return;
    HdoQueueSlot* slot = FindHdoQueueSlot(office, 1);
    if (!slot) return;
    slot->game = game;

    HdoVehicleSignatures vehicles = {};
    if (!BuildHdoVehicleSignatures(office, &vehicles)) return;
    u64 taskSignature = HdoNativeTaskSignature(office);
    u64 assignmentSignature = HdoAssignmentSignature(office);
    u32 reasons = slot->dirtyReasons | DetectHdoPanelClose(slot);

    if (!slot->initialized) reasons |= HDO_TRIGGER_INITIAL;
    else
    {
        // The native 0x80-byte rows contain live supply, demand and reservation
        // state. Their signature is expected to change after a dispatch and
        // must never be interpreted as a request to regenerate the vector:
        // FUN_1401e2380 omits currently reserved source/destination pairs.
        if (taskSignature != slot->taskSignature)
            reasons |= HDO_TRIGGER_TASK_STATE;
        if (assignmentSignature != slot->assignmentSignature)
            reasons |= HDO_TRIGGER_ASSIGNMENT_CHANGE;
        if (vehicles.residents != slot->residentSignature)
            reasons |= HDO_TRIGGER_FLEET_CHANGE;
        if (vehicles.airState != slot->airStateSignature)
            reasons |= HDO_TRIGGER_AIR_STATE;
        if (vehicles.parked != slot->parkedSignature)
            reasons |= HDO_TRIGGER_RETURN;
    }

    float timerAfter = ReadFloatOr(office, B_ASSIGN_TIMER, 1000000.0f);
    if (NativeDistributionPassWasDue(game, office, timerBefore, timerAfter))
        reasons |= HDO_TRIGGER_PERIODIC;

    if (!slot->initialized && g_savedBootstrapLogCount < 16 && H && H->log)
    {
        void** assignmentBegin = 0;
        void** assignmentEnd = 0;
        void** residentBegin = 0;
        void** residentEnd = 0;
        ReadPointerRange(office, B_ASSIGN_BEGIN, B_ASSIGN_END,
                         &assignmentBegin, &assignmentEnd, 64u);
        ReadPointerRange(office, B_VEHICLE_BEGIN, B_VEHICLE_END,
                         &residentBegin, &residentEnd, 128u);
        usize assignmentCount = assignmentBegin ? (usize)(assignmentEnd - assignmentBegin) : 0;
        usize residentCount = residentBegin ? (usize)(residentEnd - residentBegin) : 0;
        usize taskCount = 0;
        if (HasNativeDistributionTasks(office))
        {
            u8* taskBegin = *(u8**)((u8*)office + B_TASK_BEGIN);
            u8* taskEnd = *(u8**)((u8*)office + B_TASK_END);
            taskCount = (usize)(taskEnd - taskBegin) / 0x80u;
        }
        ++g_savedBootstrapLogCount;
        H->log("helido    saved-HDO bootstrap #%u: assignments=%llu task_rows_before_rebuild=%llu residents=%llu parked_helicopters=%d",
               g_savedBootstrapLogCount, (u64)assignmentCount, (u64)taskCount,
               (u64)residentCount, vehicles.parkedCount);
    }

    int distributionActive = 0;
    int canDispatch = reasons != 0 &&
                      ReadInt(game, GAME_DISTRIBUTION_ACTIVE, &distributionActive) &&
                      distributionActive > 0 && HasNativeDistributionAssignments(office);
    HdoDispatchResult result = {};
    if (canDispatch)
    {
        // WRSR's own dirty byte is the structural rebuild authority. The
        // original office monitor above normally consumes it after a UI edit;
        // if that monitor had no native candidate, RefreshNativeHdoTasks still
        // sees the byte and performs the same rebuild. The plugin never forces
        // a second regeneration merely because signatures or states changed.
        result = DispatchHdoQueue(game, office, 0);
    }

    if (reasons != 0 && (result.candidates > 0 ||
                         (reasons & (HDO_TRIGGER_ASSIGNMENT_CHANGE |
                                     HDO_TRIGGER_FLEET_CHANGE |
                                     HDO_TRIGGER_UI_CLOSE)) != 0) &&
        g_queuePassLogCount < 24 && H && H->log)
    {
        ++g_queuePassLogCount;
        H->log("helido    HDO queue pass #%u: triggers=0x%03X parked=%d assigned=%d",
               g_queuePassLogCount, reasons, result.candidates, result.assigned);
    }

    // Throttle threshold monitoring to WRSR's normal three-second office
    // cadence. Event-triggered passes still run immediately before this timer.
    if (canDispatch && timerAfter <= 0.0f &&
        IsReadable((u8*)office + B_ASSIGN_TIMER, sizeof(float)))
        *(float*)((u8*)office + B_ASSIGN_TIMER) = 3.0f;

    slot->dirtyReasons = 0;
    StoreHdoQueueSnapshot(slot, office);
}

// --------------------------------------------------------------------- HDO helicopter return/rehome native arrival gate
static void* CurrentRouteTarget(void* vehicle)
{
    return RouteTargetRelative(vehicle, 0);
}

// FUN_1406cd1e0 calls FUN_14067db00 only after the current building stop has
// completed. For a native Distribution Office route, destination -> HDO is the
// precise point at which the return leg is about to be selected and
// FUN_1406dd640 is called immediately afterward. Replacing the route here lets
// that existing take-off path depart for the new source without synthesising a
// flight state or waiting for another office scan.
static int IsCompletedHdoDestinationAdvance(void* game, void* vehicle,
                                            void* office)
{
    if (!game || !vehicle || !office || !IsHdoBuilding(office) ||
        !IsHelicopterVehicleObject(vehicle))
        return 0;

    int routeIndex = -1;
    if (!ReadInt(vehicle, V_ROUTE_INDEX, &routeIndex) ||
        NativeRouteStopCount(vehicle) != 3u || routeIndex != 1)
        return 0;

    void* source = RouteTargetRelative(vehicle, -1);
    void* current = RouteTargetRelative(vehicle, 0);
    void* next = RouteTargetRelative(vehicle, 1);
    void* arrival = ReadPointer(vehicle, V_ARRIVAL);
    return source && source != office && current && current != office &&
           arrival == current && next == office &&
           IsAirReachableTaskEndpoint(game, source) &&
           IsAirReachableTaskEndpoint(game, current);
}

static int TryChainNativeHdoAssignment(void* vehicle, void* office,
                                       HdoQueueSlot* slot)
{
    if (!vehicle || !office || !slot || !slot->game ||
        !f_DistributionAssignVehicle || !f_DistributionTaskRefresh)
        return 0;

    void* game = slot->game;
    int distributionActive = 0;
    if (!ReadInt(game, GAME_DISTRIBUTION_ACTIVE, &distributionActive) ||
        distributionActive <= 0 ||
        !IsCompletedHdoDestinationAdvance(game, vehicle, office) ||
        !RefreshNativeHdoTasks(game, office, 0) ||
        !IsReadable((u8*)vehicle + V_ARRIVAL, sizeof(void*)))
        return 0;

    // FUN_1401e5350's sole precondition before native task selection is an
    // empty +0x528 arrival pointer. At this hook the old destination is still
    // intentionally present because the caller has not completed its cleanup.
    // Hide it only for this synchronous assignment transaction, then restore it
    // so FUN_1406cd1e0 can finish the already-approved stop normally.
    void** arrivalField = (void**)((u8*)vehicle + V_ARRIVAL);
    void* completedDestination = *arrivalField;
    *arrivalField = 0;
    u64 assigned = f_DistributionAssignVehicle(game, vehicle, office);
    *arrivalField = completedDestination;

    if ((u8)assigned == 0)
    {
        if (g_chainNoAssignmentCount < 16 && H && H->log)
        {
            ++g_chainNoAssignmentCount;
            H->log("helido    completed helicopter destination #%u had no further eligible HDO air task; native return retained",
                   g_chainNoAssignmentCount);
        }
        return 0;
    }

    // A successful native assignment resets the route index to zero and builds
    // [source, destination, HDO]. The caller will now execute its normal
    // post-stop cleanup and FUN_1406dd640 against that new first target.
    void* outboundTarget = RouteTargetRelative(vehicle, 0);
    if (!outboundTarget || outboundTarget == office)
    {
        if (H && H->log)
            H->log("helido    WARNING native chained assignment returned success without an outbound source; preserving native route state");
        return 1;
    }

    ++g_chainAssignmentCount;
    if (g_chainAssignmentCount <= 24 && H && H->log)
        H->log("helido    helicopter inherited next native HDO task at unload completion #%u",
               g_chainAssignmentCount);
    return 1;
}

static void h_RouteAdvance(void* vehicle, char skipDisabledStops)
{
    if (!g_chainDispatchActive && skipDisabledStops && vehicle &&
        IsHelicopterVehicleObject(vehicle))
    {
        void* office = ReadPointer(vehicle, V_HOME);
        HdoQueueSlot* slot = office && IsHdoBuilding(office)
                                 ? FindHdoQueueSlot(office, 0) : 0;
        if (office && IsHdoBuilding(office) &&
            g_routeAdvanceProbeCount < 24 && H && H->log)
        {
            int routeIndex = -1;
            ReadInt(vehicle, V_ROUTE_INDEX, &routeIndex);
            void* current = RouteTargetRelative(vehicle, 0);
            void* next = RouteTargetRelative(vehicle, 1);
            ++g_routeAdvanceProbeCount;
            H->log("helido    HDO helicopter route-advance probe #%u: indexed=%d stops=%llu index=%d current_is_arrival=%d next_is_home=%d",
                   g_routeAdvanceProbeCount, slot && slot->game ? 1 : 0,
                   (u64)NativeRouteStopCount(vehicle), routeIndex,
                   current && current == ReadPointer(vehicle, V_ARRIVAL),
                   next && next == office);
        }
        if (slot && IsCompletedHdoDestinationAdvance(slot->game, vehicle, office))
        {
            g_chainDispatchActive = 1;
            int chained = TryChainNativeHdoAssignment(vehicle, office, slot);
            g_chainDispatchActive = 0;
            if (chained) return;
        }
    }
    o_RouteAdvance(vehicle, skipDisabledStops);
}

static int ResidentVehicleCount(void* building)
{
    if (!building || !IsReadable((u8*)building + B_VEHICLE_BEGIN, 16)) return -1;
    void** begin = *(void***)((u8*)building + B_VEHICLE_BEGIN);
    void** end   = *(void***)((u8*)building + B_VEHICLE_END);
    if (!begin && !end) return 0;
    if (!begin || !end || end < begin) return -1;
    usize count = (usize)(end - begin);
    return count <= 128 ? (int)count : -1;
}

static void* FindHdoResidenceTarget(void* vehicle)
{
    if (!vehicle || !IsHelicopterVehicleObject(vehicle) ||
        !IsReadable((u8*)vehicle + V_ROUTE_ACTIVE, 1) ||
        *(u8*)((u8*)vehicle + V_ROUTE_ACTIVE) == 0)
        return 0;

    void* home = ReadPointer(vehicle, V_HOME);
    void* arrival = ReadPointer(vehicle, V_ARRIVAL);
    void* approach = ReadPointer(vehicle, V_APPROACH);
    void* routeTarget = CurrentRouteTarget(vehicle);

    void* target = 0;
    if (arrival && IsHdoBuilding(arrival)) target = arrival;
    else if (approach && IsHdoBuilding(approach)) target = approach;
    else if (routeTarget && IsHdoBuilding(routeTarget)) target = routeTarget;

    // Returning resident helicopters are handled by the HDO queue engine;
    // this descriptor shim remains limited to an actual rehome transition.
    return target && home != target ? target : 0;
}

static int CopyBytesChecked(void* dst, const void* src, usize count)
{
    if (!dst || !src || !count || !IsReadable(src, count)) return 0;
    u8* d = (u8*)dst;
    const u8* p = (const u8*)src;
    for (usize i = 0; i < count; ++i) d[i] = p[i];
    return 1;
}

static void h_VehicleResidenceUpdate(void* vehicle, void* context)
{
    void* target = FindHdoResidenceTarget(vehicle);
    if (!target)
    {
        void* homeBefore = ReadPointer(vehicle, V_HOME);
        int parkedBefore = homeBefore && IsHdoBuilding(homeBefore) &&
                           IsParkedHdoHelicopterCandidate(vehicle, homeBefore);
        o_VehicleResidenceUpdate(vehicle, context);
        void* homeAfter = ReadPointer(vehicle, V_HOME);
        if (homeAfter && IsHdoBuilding(homeAfter) && !parkedBefore &&
            IsParkedHdoHelicopterCandidate(vehicle, homeAfter))
            MarkHdoQueueDirty(homeAfter, HDO_TRIGGER_RETURN | HDO_TRIGGER_AIR_STATE);
        return;
    }

    u8* building = (u8*)target;
    void* originalTd = ReadPointer(target, B_TYPEDESC);
    if (!originalTd || !IsReadable(originalTd, TYPEDESC_STRIDE) ||
        !IsReadable(building + B_TYPEDESC, sizeof(void*)))
    {
        o_VehicleResidenceUpdate(vehicle, context);
        return;
    }

    int originalType = -1, originalSubtype = -1;
    if (!ReadInt(originalTd, TD_TYPE, &originalType) ||
        !ReadInt(originalTd, TD_SUBTYPE, &originalSubtype) ||
        originalType != BUILDING_DISTRIBUTION_OFFICE ||
        originalSubtype != SUBTYPE_AIRPLANE ||
        !CopyBytesChecked(g_residenceTypeDesc, originalTd, TYPEDESC_STRIDE))
    {
        o_VehicleResidenceUpdate(vehicle, context);
        return;
    }

    // FUN_1403bb5a0 explicitly accepts VEHICLE_HELICOPTER (10) for
    // TYPE_AIRPLANE_PARKING (0x2F). Present only this HDO instance that way for
    // the duration of FUN_1406cd1e0. All HDO-specific station vectors, subtype,
    // asset identity and every other descriptor field remain identical because
    // this is a byte-for-byte private copy with only TD_TYPE changed.
    *(int*)(g_residenceTypeDesc + TD_TYPE) = BUILDING_AIRPLANE_PARKING;
    *(void**)(building + B_TYPEDESC) = g_residenceTypeDesc;

    if (g_lastArrivalVehicle != vehicle && H && H->log)
    {
        H->log("helido    existing helicopter arrival: native residence transition admitted for HDO");
        g_lastArrivalVehicle = vehicle;
    }

    o_VehicleResidenceUpdate(vehicle, context);

    // Restore the real descriptor before any plugin-side inspection/logging.
    if (IsReadable(building + B_TYPEDESC, sizeof(void*)) &&
        *(void**)(building + B_TYPEDESC) == g_residenceTypeDesc)
        *(void**)(building + B_TYPEDESC) = originalTd;

    void* homeAfter = ReadPointer(vehicle, V_HOME);
    if (homeAfter == target)
    {
        MarkHdoQueueDirty(target, HDO_TRIGGER_FLEET_CHANGE | HDO_TRIGGER_RETURN);
        int count = ResidentVehicleCount(target);
        if (H && H->log)
            H->log("helido    existing helicopter reassignment: native arrival finalized; HDO resident vehicles=%d", count);
        if (g_lastArrivalVehicle == vehicle) g_lastArrivalVehicle = 0;
    }
}

// Dynamic hooks accept only complete, position-independent MSVC prologue
// instructions. Capture every required target before installing any hook so a
// deterministic signature/prologue mismatch cannot leave a stale detour behind.
static int DecodeSafePrologueInstruction(const u8* p, usize available, usize* length)
{
    if (!p || !length || available == 0) return 0;
    usize i = 0;

    // Optional REX prefix.
    if (i < available && (p[i] & 0xF0) == 0x40) ++i;
    if (i >= available) return 0;

    u8 op = p[i++];

    // push r64 (50-57, optionally REX.B for r8-r15)
    if (op >= 0x50 && op <= 0x57) { *length = i; return 1; }

    // Group-one immediate arithmetic. Register and register-relative forms are
    // position independent; this also covers the small add/inc sequence at
    // FUN_14067db00 without baking compiler bytes into the plugin.
    if (op == 0x83)
    {
        if (i >= available) return 0;
        u8 modrm = p[i++];
        u8 mod = (modrm >> 6) & 3;
        u8 rm = modrm & 7;
        if (mod == 0) return 0;
        if (mod != 3 && rm == 4)
        {
            if (i >= available) return 0;
            ++i;
        }
        usize disp = mod == 1 ? 1 : (mod == 2 ? 4 : 0);
        if (i + disp + 1 > available) return 0;
        *length = i + disp + 1; return 1;
    }
    if (op == 0x81)
    {
        if (i >= available) return 0;
        u8 modrm = p[i++];
        u8 mod = (modrm >> 6) & 3;
        u8 rm = modrm & 7;
        if (mod == 0) return 0;
        if (mod != 3 && rm == 4)
        {
            if (i >= available) return 0;
            ++i;
        }
        usize disp = mod == 1 ? 1 : (mod == 2 ? 4 : 0);
        if (i + disp + 4 > available) return 0;
        *length = i + disp + 4; return 1;
    }

    // Group-five INC/DEC register or register-relative memory. No relative
    // address is embedded, so copying it into the trampoline is safe.
    if (op == 0xFF)
    {
        if (i >= available) return 0;
        u8 modrm = p[i++];
        u8 extension = (modrm >> 3) & 7;
        u8 mod = (modrm >> 6) & 3;
        u8 rm = modrm & 7;
        if ((extension != 0 && extension != 1) || mod == 0) return 0;
        if (mod != 3 && rm == 4)
        {
            if (i >= available) return 0;
            ++i;
        }
        usize disp = mod == 1 ? 1 : (mod == 2 ? 4 : 0);
        if (i + disp > available) return 0;
        *length = i + disp; return 1;
    }

    // mov memory/register forms used by MSVC prologues. Mod=1/2 addressing is
    // register-relative and therefore safe to relocate verbatim. Mod=0 is
    // refused because rm=5 is RIP-relative and there is no reason to accept a
    // more exotic no-displacement form in a function prologue.
    if (op == 0x63 || op == 0x88 || op == 0x89 || op == 0x8A || op == 0x8B)
    {
        if (i >= available) return 0;
        u8 modrm = p[i++];
        u8 mod = (modrm >> 6) & 3;
        u8 rm = modrm & 7;
        if (mod == 3) { *length = i; return 1; }
        if (mod != 1 && mod != 2) return 0;
        if (rm == 4)
        {
            if (i >= available) return 0;
            ++i; // SIB; base/index are still register-relative for mod 1/2
        }
        usize disp = (mod == 1) ? 1 : 4;
        if (i + disp > available) return 0;
        *length = i + disp; return 1;
    }

    // lea r64,[reg+disp] is likewise position independent for mod 1/2.
    if (op == 0x8D)
    {
        if (i >= available) return 0;
        u8 modrm = p[i++];
        u8 mod = (modrm >> 6) & 3;
        u8 rm = modrm & 7;
        if (mod != 1 && mod != 2) return 0;
        if (rm == 4)
        {
            if (i >= available) return 0;
            ++i;
        }
        usize disp = (mod == 1) ? 1 : 4;
        if (i + disp > available) return 0;
        *length = i + disp; return 1;
    }

    return 0;
}

struct RuntimePrologueGuard
{
    u8 expect[32];
    usize stolen;
};

// FUN_14067db00 requires a complete 14-byte stolen boundary. Preserve that
// validated boundary and pass its live bytes back to TesmioLoader as the
// install-time expectation so a mismatched or pre-detoured entry is rejected.
static int CaptureProven14ByteBoundary(u8* target,
                                       RuntimePrologueGuard* guard,
                                       const char* label)
{
    if (!target || !guard || !H || !IsReadable(target, 14) ||
        target[0] == 0x00 || target[0] == 0xCC || target[0] == 0xE9 ||
        (target[0] == 0xFF && target[1] == 0x25))
    {
        if (H && H->log)
            H->log("helido    fixed-boundary preflight failed for %s",
                   label ? label : "unknown target");
        return 0;
    }
    for (usize i = 0; i < 14; ++i) guard->expect[i] = target[i];
    guard->stolen = 14;
    return 1;
}

static int CaptureRuntimePrologue(u8* target, RuntimePrologueGuard* guard,
                                   const char* label)
{
    if (!target || !guard || !H || !IsReadable(target, 32)) return 0;
    usize stolen = 0;
    while (stolen < 14)
    {
        usize len = 0;
        if (!DecodeSafePrologueInstruction(target + stolen, 32 - stolen, &len) ||
            len == 0 || stolen + len > sizeof(guard->expect))
        {
            if (H->log)
                H->log("helido    hook preflight refused for %s: unsupported prologue at +0x%llX",
                       label ? label : "unknown target", (u64)stolen);
            return 0;
        }
        stolen += len;
    }
    for (usize i = 0; i < stolen; ++i) guard->expect[i] = target[i];
    guard->stolen = stolen;
    return 1;
}

static int InstallCapturedPrologueHook(u8* target, void* detour,
                                        void** trampoline,
                                        const RuntimePrologueGuard* guard,
                                        const char* label)
{
    return target && guard && guard->stolen && H && H->installInlineHook &&
           H->installInlineHook(target, detour, trampoline,
                                guard->expect, guard->stolen, label);
}

// These entries are invoked normally and are never overwritten by this plugin.
// Requiring their first 14 bytes to be relocatable was a category error in Test
// 6: the readiness routine contains a valid instruction at +0xD which the
// deliberately-small hook decoder does not support. Keep the native-call guard
// separate from hook-prologue validation so callable implementation details do
// not prevent the HDO detours from loading.
static int ValidateDirectNativeCallable(u8* target, const char* label)
{
    if (target && H && IsReadable(target, 32) &&
        target[0] != 0x00 && target[0] != 0xCC)
        return 1;

    if (H && H->log)
        H->log("helido    native callable preflight failed for %s",
               label ? label : "unknown target");
    return 0;
}

// --------------------------------------------------------------------- native-menu-safe helipad masking
static int DescriptorNameContains(void* td, const char* needle)
{
    if (!td || !needle || !needle[0]) return 0;
    const char* text = (const char*)td;
    usize needleLen = 0;
    while (needle[needleLen] && needleLen < 128) ++needleLen;
    if (!needleLen) return 0;

    // The first field is the descriptor's internal/path name. Workshop assets can
    // carry a path prefix, so exact equality is intentionally not used here.
    for (usize i = 0; i < 512; ++i)
    {
        if (!IsReadable(text + i, 1)) return 0;
        if (text[i] == '\0') break;
        if (text[i] != needle[0]) continue;
        usize j = 1;
        while (j < needleLen)
        {
            if (!IsReadable(text + i + j, 1) || text[i + j] == '\0' ||
                text[i + j] != needle[j]) break;
            ++j;
        }
        if (j == needleLen) return 1;
    }
    return 0;
}

static int ReadDescriptorStations(void* td, u8** beginOut, u8** endOut, u8** capOut)
{
    if (!td || !beginOut || !endOut || !capOut ||
        !IsReadable((u8*)td + TD_STATION_BEGIN, 24)) return 0;
    u8* begin = *(u8**)((u8*)td + TD_STATION_BEGIN);
    u8* end   = *(u8**)((u8*)td + TD_STATION_END);
    u8* cap   = *(u8**)((u8*)td + TD_STATION_CAP);
    if (!begin && !end && !cap)
    {
        *beginOut = *endOut = *capOut = 0;
        return 1;
    }
    if (!begin || !end || !cap || end < begin || cap < end) return 0;
    usize used = (usize)(end - begin);
    usize capacity = (usize)(cap - begin);
    if ((used % STATION_SIZE) != 0 || (capacity % STATION_SIZE) != 0 ||
        capacity > STATION_SIZE * 512u || (used && !IsReadable(begin, used))) return 0;
    *beginOut = begin;
    *endOut = end;
    *capOut = cap;
    return 1;
}

static int CountStationKind(void* td, int wantedKind)
{
    u8* begin = 0; u8* end = 0; u8* cap = 0;
    if (!ReadDescriptorStations(td, &begin, &end, &cap) || !begin) return 0;
    int count = 0;
    for (u8* p = begin; p < end; p += STATION_SIZE)
    {
        if (*(int*)(p + STATION_KIND) == wantedKind) ++count;
    }
    return count;
}

static void* FindHdoTypeDescriptor(i64 game)
{
    if (!game || !IsReadable((u8*)game + G_TYPEDESC_BEGIN, 16)) return 0;
    u8* begin = *(u8**)((u8*)game + G_TYPEDESC_BEGIN);
    u8* end   = *(u8**)((u8*)game + G_TYPEDESC_END);
    if (!begin || !end || end < begin) return 0;
    usize bytes = (usize)(end - begin);
    if ((bytes % TYPEDESC_STRIDE) != 0) return 0;
    usize count = bytes / TYPEDESC_STRIDE;
    if (count > 8192 || (count && !IsReadable(begin, bytes))) return 0;

    void* propertyFallback = 0;
    int propertyMatches = 0;
    for (usize i = 0; i < count; ++i)
    {
        void* td = begin + i * TYPEDESC_STRIDE;
        int type = -1, subtype = -1;
        if (!ReadInt(td, TD_TYPE, &type) || !ReadInt(td, TD_SUBTYPE, &subtype) ||
            type != BUILDING_DISTRIBUTION_OFFICE || subtype != SUBTYPE_AIRPLANE)
            continue;

        // Prefer the Workshop path/name when identifying this descriptor.
        if (DescriptorNameContains(td, "HelicopterDistributionOffice") ||
            DescriptorNameContains(td, "3778940406"))
            return td;

        // Fail-safe fallback for Workshop builds whose internal name is rewritten:
        // this mod is an AIRPLANE-subtype DO with exactly two native heli stations.
        // Require a Workshop flag and uniqueness before touching anything.
        u8* raw = (u8*)td;
        int workshop = 0;
        if (IsReadable(raw + TD_WORKSHOP_A, 2))
            workshop = raw[TD_WORKSHOP_A] != 0 || raw[TD_WORKSHOP_B] != 0;
        if (workshop && CountStationKind(td, 0x22) == 2)
        {
            propertyFallback = td;
            ++propertyMatches;
        }
    }
    return propertyMatches == 1 ? propertyFallback : 0;
}

static int MaskHdoStationsForMenu(void* hdoTd, u8** savedBeginOut, u8** savedEndOut)
{
    if (savedBeginOut) *savedBeginOut = 0;
    if (savedEndOut) *savedEndOut = 0;
    if (!hdoTd) return 0;

    u8* begin = 0; u8* end = 0; u8* cap = 0;
    if (!ReadDescriptorStations(hdoTd, &begin, &end, &cap) || !begin || end <= begin)
        return 0;

    // Never hide a malformed/partial station set. The safe failure is to leave
    // the functional pads alone and accept Workshop-menu-only placement.
    if (CountStationKind(hdoTd, 0x22) != 2) return 0;
    if (!IsReadable((u8*)hdoTd + TD_STATION_END, sizeof(void*))) return 0;

    // The native classifier only tests whether kind 0x22 exists for this generic
    // Distribution Office grouping gate. Exposing an empty vector for this one
    // descriptor makes it compare like Small/Medium DO without mutating records.
    *(u8**)((u8*)hdoTd + TD_STATION_END) = begin;
    if (savedBeginOut) *savedBeginOut = begin;
    if (savedEndOut) *savedEndOut = end;
    return 1;
}

static void h_BottomMenuRebuild(i64 game)
{
    void* hdoTd = FindHdoTypeDescriptor(game);
    u8* savedBegin = 0;
    u8* savedEnd = 0;
    int masked = MaskHdoStationsForMenu(hdoTd, &savedBegin, &savedEnd);

    if (!masked && H && H->log)
        H->log("helido    warning: native menu classification mask unavailable; helipads remain functional");

    o_BottomMenuRebuild(game);

    if (masked && hdoTd && savedBegin && savedEnd &&
        IsReadable((u8*)hdoTd + TD_STATION_BEGIN, 16))
    {
        // Only restore if this is still the same vector. If anything unexpected
        // rebuilt it during classification, leave memory untouched rather than
        // write a stale pointer; the asset itself remains the source of truth.
        u8* currentBegin = *(u8**)((u8*)hdoTd + TD_STATION_BEGIN);
        u8* currentEnd   = *(u8**)((u8*)hdoTd + TD_STATION_END);
        if (currentBegin == savedBegin && currentEnd == savedBegin)
        {
            *(u8**)((u8*)hdoTd + TD_STATION_END) = savedEnd;
        }
        else if (H && H->log)
        {
            H->log("helido    WARNING station vector changed during menu rebuild; skipped pointer restore");
        }
    }
}

// --------------------------------------------------------------------- native assignment helpers
static int AssignmentCount(void* building)
{
    u8* b = (u8*)building;
    if (!b || !IsReadable(b + B_ASSIGN_BEGIN, 16)) return -1;
    u8* begin = *(u8**)(b + B_ASSIGN_BEGIN);
    u8* end = *(u8**)(b + B_ASSIGN_END);
    if (!begin && !end) return 0;
    if (!begin || !end || end < begin) return -1;
    usize bytes = (usize)(end - begin);
    if ((bytes % sizeof(void*)) != 0) return -1;
    usize count = bytes / sizeof(void*);
    if (count > 49) return -1;
    return (int)count;
}

static int AssignmentExists(void* home, void* target)
{
    u8* b = (u8*)home;
    if (!b || !target || !IsReadable(b + B_ASSIGN_BEGIN, 16)) return 0;
    void** begin = *(void***)(b + B_ASSIGN_BEGIN);
    void** end = *(void***)(b + B_ASSIGN_END);
    if (!begin || !end || end < begin) return 0;
    usize count = (usize)(end - begin);
    if (count > 49) count = 49;
    if (count && !IsReadable(begin, count * sizeof(void*))) return 0;
    for (usize i = 0; i < count; ++i)
    {
        void* record = begin[i];
        if (record && IsReadable((u8*)record + 8, sizeof(void*)) &&
            *(void**)((u8*)record + 8) == target) return 1;
    }
    return 0;
}

// FUN_1402b78b0 owns all four parts of Distribution Office target selection:
// collision discovery, the per-target validity cache at GAME_SELECTOR_VALID,
// the red/valid building overlay, and the native assignment/back-reference
// transaction on click. A roadless cargo heliport is rejected before the cache
// is consulted because the stock branch requires one 0x60-byte road connection
// record. A synchronous record projection crosses only that existence gate;
// promoting the cache then bypasses the road-graph test while the original
// function preserves every other guard and performs the real add.
static int IsSelectableHdoCargoHeliport(void* office, void* target)
{
    if (!office || !target || target == office || !IsHdoBuilding(office) ||
        !HasNativeHeliportStation(target))
        return 0;

    // Keep the stock selector's non-connectivity safety gates: completed
    // construction, no duplicate row, and the native 49-target ceiling.
    u8* building = (u8*)target;
    if (!IsReadable(building + B_UNDER_CONSTRUCTION, 1) ||
        !IsReadable(building + B_BUILD_PROGRESS, sizeof(float)) ||
        building[B_UNDER_CONSTRUCTION] != 0)
        return 0;

    float progress = *(float*)(building + B_BUILD_PROGRESS);
    if (progress < 1.0f) return 0;

    int count = AssignmentCount(office);
    return count >= 0 && count < 49 && !AssignmentExists(office, target);
}

static int HdoSelectorStillActive(void* game, void* office)
{
    return game && office &&
           ReadPointer(game, GAME_SELECTOR_OFFICE) == office &&
           ReadPointer(game, GAME_SELECTOR_WINDOW) != 0 &&
           ReadPointer(game, GAME_ACTIVE_TOOL) != 0;
}

struct SelectorConnectionProjection
{
    u8* target;
    u8* savedBegin;
    u8* savedEnd;
    u8* savedCap;
    int active;
};

static u8* FindOfficeRoadConnection(void* office)
{
    u8* building = (u8*)office;
    if (!building || !IsReadable(building + B_CONNECTION_BEGIN, 16)) return 0;
    u8* begin = *(u8**)(building + B_CONNECTION_BEGIN);
    u8* end = *(u8**)(building + B_CONNECTION_END);
    if (!begin || !end || end < begin) return 0;
    usize bytes = (usize)(end - begin);
    if ((bytes % CONNECTION_SIZE) != 0 ||
        bytes / CONNECTION_SIZE > SELECTOR_CONNECTION_MAX_ROWS ||
        (bytes && !IsReadable(begin, bytes)))
        return 0;

    usize count = bytes / CONNECTION_SIZE;
    for (usize i = 0; i < count; ++i)
    {
        u8* record = begin + i * CONNECTION_SIZE;
        if (*(int*)record == 0 && *(void**)(record + CONNECTION_LINK) != 0)
            return record;
    }
    return 0;
}

static int BeginSelectorConnectionProjection(void* office, void* target,
                                             SelectorConnectionProjection* out)
{
    if (!office || !target || !out) return 0;
    out->active = 0;

    u8* building = (u8*)target;
    if (!IsReadable(building + B_CONNECTION_BEGIN, 24)) return 0;
    u8* begin = *(u8**)(building + B_CONNECTION_BEGIN);
    u8* end = *(u8**)(building + B_CONNECTION_END);
    u8* cap = *(u8**)(building + B_CONNECTION_CAP);

    usize count = 0;
    if (begin || end)
    {
        if (!begin || !end || end < begin) return 0;
        usize bytes = (usize)(end - begin);
        if ((bytes % CONNECTION_SIZE) != 0 ||
            bytes / CONNECTION_SIZE > SELECTOR_CONNECTION_MAX_ROWS ||
            (bytes && !IsReadable(begin, bytes)))
            return 0;
        count = bytes / CONNECTION_SIZE;
        for (usize i = 0; i < bytes; ++i) g_selectorConnectionView[i] = begin[i];
    }

    u8* projected = g_selectorConnectionView + count * CONNECTION_SIZE;
    u8* officeRoad = FindOfficeRoadConnection(office);
    if (officeRoad)
    {
        for (usize i = 0; i < CONNECTION_SIZE; ++i) projected[i] = officeRoad[i];
    }
    else
    {
        // The cache-hit path does not dereference this link; it needs only a
        // non-null road-shaped record to reach the promoted result. An actual
        // HDO road record is preferred above and is present in the Workshop
        // asset's normal configuration.
        ZeroBytes(projected, CONNECTION_SIZE);
        *(void**)(projected + CONNECTION_LINK) = office;
    }

    out->target = building;
    out->savedBegin = begin;
    out->savedEnd = end;
    out->savedCap = cap;
    *(u8**)(building + B_CONNECTION_BEGIN) = g_selectorConnectionView;
    *(u8**)(building + B_CONNECTION_END) = projected + CONNECTION_SIZE;
    *(u8**)(building + B_CONNECTION_CAP) = projected + CONNECTION_SIZE;
    out->active = 1;
    return 1;
}

static void EndSelectorConnectionProjection(SelectorConnectionProjection* state)
{
    if (!state || !state->active || !state->target) return;
    *(u8**)(state->target + B_CONNECTION_BEGIN) = state->savedBegin;
    *(u8**)(state->target + B_CONNECTION_END) = state->savedEnd;
    *(u8**)(state->target + B_CONNECTION_CAP) = state->savedCap;
    state->active = 0;
}

static void LogSelectorOverride(void* target)
{
    if (target == g_lastSelectorOverrideTarget) return;
    g_lastSelectorOverrideTarget = target;
    if (g_selectorOverrideLogCount < 24 && H && H->log)
    {
        ++g_selectorOverrideLogCount;
        H->log("helido    HDO selector admitted cargo heliport #%u without requiring a road connection",
               g_selectorOverrideLogCount);
    }
}

static int RunPromotedHdoSelector(void* game, void* office, void* target,
                                  u64 arg2, u64 arg3, float scale)
{
    SelectorConnectionProjection projection = {};
    if (!BeginSelectorConnectionProjection(office, target, &projection)) return 0;
    ((u8*)game)[GAME_SELECTOR_VALID] = 1;
    LogSelectorOverride(target);
    o_DistributionTargetSelector(game, arg2, arg3, scale);
    EndSelectorConnectionProjection(&projection);
    return 1;
}

static void h_DistributionTargetSelector(void* game, u64 arg2,
                                         u64 arg3, float scale)
{
    if (!o_DistributionTargetSelector) return;

    void* office = ReadPointer(game, GAME_SELECTOR_OFFICE);
    u8* click = EXE ? EXE + G_MOUSE_CLICK_RVA : 0;
    u8* state = (u8*)game;
    if (!game || !IsHdoBuilding(office) ||
        !IsReadable(state + GAME_SELECTOR_VALID, 1) ||
        !IsReadable(state + GAME_SELECTOR_TARGET, sizeof(void*)) ||
        !click || !IsReadable(click, 1))
    {
        o_DistributionTargetSelector(game, arg2, arg3, scale);
        return;
    }

    // Run collision discovery with this selector's click byte masked. When the
    // cached target is already a cargo heliport, project it for this first pass
    // as well so steady-state hover never falls back to the red road overlay.
    u8 savedClick = *click;
    *click = 0;
    void* cachedTarget = *(void**)(state + GAME_SELECTOR_TARGET);
    int firstPassPromoted = 0;
    if (IsSelectableHdoCargoHeliport(office, cachedTarget))
    {
        firstPassPromoted = RunPromotedHdoSelector(game, office, cachedTarget,
                                                   arg2, arg3, scale);
    }
    if (!firstPassPromoted)
        o_DistributionTargetSelector(game, arg2, arg3, scale);
    *click = savedClick;

    if (!HdoSelectorStillActive(game, office)) return;

    void* discoveredTarget = *(void**)(state + GAME_SELECTOR_TARGET);
    if (IsSelectableHdoCargoHeliport(office, discoveredTarget))
    {
        // A stable hover with no click was already drawn by the promoted first
        // pass. A newly-discovered heliport or a real click needs one final
        // promoted pass with the restored input state.
        if (firstPassPromoted && discoveredTarget == cachedTarget &&
            savedClick == 0)
            return;

        // Re-enter native once with the existence gate projected and its own
        // validity cache promoted. This pass supplies the valid overlay and,
        // when savedClick is set, creates the normal Distribution Office record
        // and target-building back-reference.
        if (!RunPromotedHdoSelector(game, office, discoveredTarget,
                                    arg2, arg3, scale) && savedClick != 0)
            o_DistributionTargetSelector(game, arg2, arg3, scale);
    }
    else if (savedClick != 0)
    {
        // Preserve click-on-first-hover behaviour for ordinary road targets.
        o_DistributionTargetSelector(game, arg2, arg3, scale);
    }
}

static int AddNativeOverseasAssignment(void* home, void* target, const char* label)
{
    if (!home || !target || !f_Malloc || !f_AssignPush || !f_AssignInit) return 0;
    if (AssignmentExists(home, target)) return 1;

    void* targetDescriptor = ReadPointer(target, B_TYPEDESC);
    int targetType = -1;
    if (!targetDescriptor || !ReadInt(targetDescriptor, TD_TYPE, &targetType) || targetType != TYPE_EXTERNAL)
    {
        if (H && H->log) H->log("helido    refused non-external overseas target %p", target);
        return 0;
    }

    int countBefore = AssignmentCount(home);
    if (countBefore < 0 || countBefore >= 49) return 0;

    u8* record = (u8*)f_Malloc(0x18);
    u8* config = (u8*)f_Malloc(0x198);
    if (!record || !config)
    {
        if (H && H->log) H->log("helido    overseas assignment allocation failed");
        return 0;
    }

    ZeroBytes(record, 0x18);
    ZeroBytes(config, 0x198);
    f_AssignInit(config);

    // Mirror the game's / DDO's native external-target assignment initialisation.
    record[0] = 1;
    *(void**)(record + 8) = target;
    *(void**)(record + 0x10) = config;

    config[0] = 1;
    config[200] = 1;
    *(float*)(config + 4) = 1.0f;
    *(u64*)(config + 0x10) = *(u64*)(config + 8);
    *(float*)(config + 0xCC) = 1.0f;
    *(u64*)(config + 0xD8) = *(u64*)(config + 0xD0);
    *(u64*)(config + 0x190) = 0;
    *(u64*)(config + 0x20) = 0;
    config[0x28] = 1;
    *(u64*)(config + 0xE8) = 0;
    config[0xF0] = 1;

    // Native external-node defaults used by the Distribution Office task parser.
    *(u32*)(config + 4) = 0;
    *(u32*)(config + 0xCC) = 0x3F7D70A4u;
    config[0] = 0;

    void* recordValue = record;
    f_AssignPush((u64*)((u8*)home + B_ASSIGN_BEGIN), &recordValue);
    if (AssignmentCount(home) != countBefore + 1)
    {
        if (H && H->log) H->log("helido    native assignment push failed for %s", label);
        return 0;
    }

    // External pseudo-buildings deliberately do not receive a back-reference.
    if (IsReadable((u8*)home + B_ASSIGN_DIRTY, 1)) *((u8*)home + B_ASSIGN_DIRTY) = 1;
    if (IsReadable((u8*)home + B_ASSIGN_TIMER, sizeof(float))) *(float*)((u8*)home + B_ASSIGN_TIMER) = 3.0f;

    if (H && H->log) H->log("helido    added %s as native Distribution Office target", label);
    return 1;
}

// --------------------------------------------------------------------- DDO-style native overseas buttons
static void* ReadIatFunction(void* module, const char* dll, const char* name)
{
    if (!H || !H->findIatSlot || !module) return 0;
    void** slot = H->findIatSlot(module, dll, name);
    if (!slot || !IsReadable(slot, sizeof(void*))) return 0;
    return *slot;
}

static void* ReadEngineImport(const char* name)
{
    static const char* dlls[] = { "C3DDLL64.dll", "c3ddll64.dll" };
    for (usize i = 0; i < sizeof(dlls) / sizeof(dlls[0]); ++i)
    {
        void* p = ReadIatFunction(H->exeModule, dlls[i], name);
        if (p) return p;
    }
    return 0;
}

static int ResolvePanelImports(void)
{
    f_PanelDraw = (FnPanelDraw)ReadEngineImport("?Draw@C3D_PANEL2D@@QEAAXMMMMM_N@Z");
    f_GetMouseSolid = (FnGetMouseSolid)ReadEngineImport("?GetMouseSolid@C3D_INPUT@@QEAA?AVC3DVECTOR3@@XZ");
    f_PanelCollisionRect = (FnPanelCollisionRect)ReadEngineImport("?CollisionRect@C3D_PANEL2D@@QEAA_NVC3DVECTOR3@@MMMM@Z");
    if (!f_PanelDraw || !f_GetMouseSolid)
    {
        if (H && H->log) H->log("helido    overseas buttons disabled: C3D draw/mouse imports missing");
        return 0;
    }
    return 1;
}

struct PanelRenderState
{
    float size[2];
    float position[2];
    u32 pad;
    float color[4];
};

static int CapturePanelRenderState(PanelRenderState* state)
{
    if (!state || !EXE ||
        !IsReadable(EXE + G_PANEL_SIZE_RVA, sizeof(state->size)) ||
        !IsReadable(EXE + G_PANEL_POS_RVA, sizeof(state->position)) ||
        !IsReadable(EXE + G_PANEL_PAD_RVA, sizeof(state->pad)) ||
        !IsReadable(EXE + G_PANEL_COLOR_RVA, sizeof(state->color)))
        return 0;

    const float* size = (const float*)(EXE + G_PANEL_SIZE_RVA);
    const float* position = (const float*)(EXE + G_PANEL_POS_RVA);
    const float* color = (const float*)(EXE + G_PANEL_COLOR_RVA);
    state->size[0] = size[0]; state->size[1] = size[1];
    state->position[0] = position[0]; state->position[1] = position[1];
    state->pad = *(const u32*)(EXE + G_PANEL_PAD_RVA);
    for (int i = 0; i < 4; ++i) state->color[i] = color[i];
    return 1;
}

static void RestorePanelRenderState(const PanelRenderState* state)
{
    if (!state || !EXE ||
        !IsReadable(EXE + G_PANEL_SIZE_RVA, sizeof(state->size)) ||
        !IsReadable(EXE + G_PANEL_POS_RVA, sizeof(state->position)) ||
        !IsReadable(EXE + G_PANEL_PAD_RVA, sizeof(state->pad)) ||
        !IsReadable(EXE + G_PANEL_COLOR_RVA, sizeof(state->color)))
        return;

    float* size = (float*)(EXE + G_PANEL_SIZE_RVA);
    float* position = (float*)(EXE + G_PANEL_POS_RVA);
    float* color = (float*)(EXE + G_PANEL_COLOR_RVA);
    size[0] = state->size[0]; size[1] = state->size[1];
    position[0] = state->position[0]; position[1] = state->position[1];
    *(u32*)(EXE + G_PANEL_PAD_RVA) = state->pad;
    for (int i = 0; i < 4; ++i) color[i] = state->color[i];
}


static void SetPanelRect(float x, float y, float w, float h)
{
    if (!EXE || !IsReadable(EXE + G_PANEL_POS_RVA, 16)) return;
    float* pos = (float*)(EXE + G_PANEL_POS_RVA);
    float* size = (float*)(EXE + G_PANEL_SIZE_RVA);
    pos[0] = x; pos[1] = y;
    size[0] = w; size[1] = h;
    *(u32*)(EXE + G_PANEL_PAD_RVA) = 0;
}

static void SetPanelAlpha(float alpha)
{
    if (!EXE || !IsReadable(EXE + G_PANEL_COLOR_RVA, 16)) return;
    float* c = (float*)(EXE + G_PANEL_COLOR_RVA);
    c[0] = 1.0f; c[1] = 1.0f; c[2] = 1.0f; c[3] = alpha;
}

static int BindPanelTexture(void* texture, void* technique)
{
    if (!texture || !technique || !IsReadable(texture, sizeof(void*))) return 0;
    void** vtable = *(void***)texture;
    if (!vtable || !IsReadable(vtable, 0x78)) return 0;
    typedef void (*FnBind)(void* self, int stage, void* technique);
    FnBind bind = (FnBind)vtable[0x70 / sizeof(void*)];
    if (!bind) return 0;
    bind(texture, 0, technique);
    return 1;
}

static int DrawCompactOverseasButton(void* texture, float x, float y, float icon,
                                     const float* mouse, int assigned)
{
    if (!texture || !mouse || !f_PanelDraw) return 0;
    void* technique = ReadPointer(EXE, G_TECHNIQUE_RVA);
    if (!technique || !BindPanelTexture(texture, technique)) return 0;

    SetPanelRect(x, y, icon, icon);
    SetPanelAlpha(assigned ? 0.32f : 0.72f);
    // C3D_PANEL2D::Draw uses normalized UVs. Keeping this literal avoids
    // depending on an unrelated data address whose contents can vary by build.
    const float full = 1.0f;
    f_PanelDraw(EXE + G_PANEL_RVA, 0.0f, 0.0f, full, full, 0.0f, 1);

    int hovered = 0;
    const float hitShift = icon * 0.70f;
    const float hitX = x - hitShift;
    const float hitY = y - hitShift;

    if (f_PanelCollisionRect)
    {
        alignas(16) float collisionMouse[4] = { mouse[0], mouse[1], mouse[2], 0.0f };
        hovered = f_PanelCollisionRect(EXE + G_PANEL_RVA, collisionMouse,
                                       hitX, hitX + icon, hitY, hitY + icon);
    }
    else
    {
        float scrollY = 0.0f;
        u8* panel = EXE + G_PANEL_RVA;
        if (IsReadable(panel + 0x674c, sizeof(float))) scrollY = *(float*)(panel + 0x674c);
        float adjustedY = mouse[1] - scrollY;
        hovered = mouse[0] >= hitX && mouse[0] <= hitX + icon &&
                  adjustedY >= hitY && adjustedY <= hitY + icon;
    }

    if (hovered && !assigned)
    {
        SetPanelRect(x, y, icon, icon);
        SetPanelAlpha(1.0f);
        f_PanelDraw(EXE + G_PANEL_RVA, 0.0f, 0.0f, full, full, 0.0f, 1);
    }
    return hovered;
}

static void DrawOverseasButtons(void* game, void* window, void* home, int suppressInput)
{
    if (!game || !window || !home || !f_PanelDraw || !f_GetMouseSolid) return;

    void* soviet = ReadPointer(game, G_SOVIET_NODE);
    void* western = ReadPointer(game, G_WESTERN_NODE);
    void* sovietIcon = ReadPointer(game, GAME_SOVIET_ICON);
    void* westernIcon = ReadPointer(game, GAME_WESTERN_ICON);
    if (!soviet || !western || !sovietIcon || !westernIcon) return;

    alignas(16) u8 mouseBuffer[16];
    ZeroBytes(mouseBuffer, sizeof(mouseBuffer));
    f_GetMouseSolid(EXE + G_MOUSE_OBJECT_RVA, mouseBuffer);
    const float* mouse = (const float*)mouseBuffer;
    if (!IsReadable(mouse, sizeof(float) * 3)) return;

    float dpi = GlobalFloat(G_DPI_RVA, 1.0f);
    float baseX = ReadFloatOr(window, W_POS_X, 0.0f) + ReadFloatOr(window, W_OFF_X, 0.0f);
    float baseY = ReadFloatOr(window, W_POS_Y, 0.0f) + ReadFloatOr(window, W_OFF_Y, 0.0f);
    float icon = dpi * (float)BUTTON_SIZE;
    float x = baseX + dpi * (float)BUTTON_X_OFFSET;
    float rowY = baseY + dpi * (float)BUTTON_Y_OFFSET;
    float westernX = x + icon + dpi * (float)BUTTON_GAP;

    int sovietAssigned = AssignmentExists(home, soviet);
    int westernAssigned = AssignmentExists(home, western);

    // The shared C3D panel is also used by the native Distribution Office UI.
    // Our icon draws change its global rectangle/padding/colour fields, so save
    // and restore them around both buttons. Leaving the icon state installed
    // corrupts the first native draws on the following frame.
    PanelRenderState panelState;
    if (!CapturePanelRenderState(&panelState)) return;
    int hoverSoviet = DrawCompactOverseasButton(sovietIcon, x, rowY, icon, mouse, sovietAssigned);
    int hoverWestern = DrawCompactOverseasButton(westernIcon, westernX, rowY, icon, mouse, westernAssigned);
    RestorePanelRenderState(&panelState);

    int leftDown = (((unsigned short)GetAsyncKeyState(0x01) & 0x8000u) != 0u); // VK_LBUTTON

    if (g_buttonHome != home)
    {
        g_buttonHome = home;
        g_buttonCapture = 0;
        g_lastLeftButtonDown = leftDown;
    }

    // A native row add/remove action owns the current click gesture.
    if (suppressInput)
    {
        g_buttonCapture = 0;
        g_buttonSuppressFrames = 2;
        g_lastLeftButtonDown = leftDown;
        return;
    }
    if (g_buttonSuppressFrames > 0)
    {
        --g_buttonSuppressFrames;
        g_buttonCapture = 0;
        g_lastLeftButtonDown = leftDown;
        return;
    }

    int pressEdge = leftDown && !g_lastLeftButtonDown;
    int releaseEdge = !leftDown && g_lastLeftButtonDown;

    if (pressEdge)
    {
        if (hoverSoviet && !hoverWestern && !sovietAssigned) g_buttonCapture = 1;
        else if (hoverWestern && !hoverSoviet && !westernAssigned) g_buttonCapture = 2;
        else g_buttonCapture = 0;
    }

    if (leftDown)
    {
        if (g_buttonCapture == 1 && !hoverSoviet) g_buttonCapture = 0;
        if (g_buttonCapture == 2 && !hoverWestern) g_buttonCapture = 0;
    }

    if (releaseEdge)
    {
        int captured = g_buttonCapture;
        g_buttonCapture = 0;
        if (captured == 1 && hoverSoviet && !hoverWestern && !sovietAssigned)
        {
            AddNativeOverseasAssignment(home, soviet, "Soviet overseas connection");
            g_buttonSuppressFrames = 2;
        }
        else if (captured == 2 && hoverWestern && !hoverSoviet && !westernAssigned)
        {
            AddNativeOverseasAssignment(home, western, "Western/NATO overseas connection");
            g_buttonSuppressFrames = 2;
        }
    }

    g_lastLeftButtonDown = leftDown;
}

// Hook the normal Distribution Office panel. All non-HDO buildings pass through
// byte-for-byte and receive no additional controls.
static void h_RoadDoPanel(void* ui, void* window)
{
    void* building = ReadPointer(window, W_BUILDING);
    int ours = IsHdoBuilding(building);
    int before = ours ? AssignmentCount(building) : -1;
    u64 signatureBefore = ours ? HdoAssignmentSignature(building) : 0;

    o_RoadDoPanel(ui, window);

    if (!ours) return;
    int after = AssignmentCount(building);
    DrawOverseasButtons(ui, window, building, before >= 0 && after != before);
    u64 signatureAfter = HdoAssignmentSignature(building);
    NoteHdoPanelPulse(building,
                      (before >= 0 && after != before) ||
                      signatureAfter != signatureBefore);
}

// --------------------------------------------------------------------- exports
EXPORT unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

EXPORT int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    H = host;
    if (info)
    {
        info->name = "helicopter_distribution_office";
        info->version = "1.2.0";
    }

    if (!H || H->apiVersion != TSM_API_VERSION || !H->exeBase ||
        !H->installInlineHook || !H->readablePtr)
        return 1;

    EXE = H->exeBase;
    g_initReady = 0;

    // Validate the directly-called vanilla helicopter helper before installing
    // the compatibility detour.
    u8* heliHelper = EXE + RVA_HELICOPTER_PAD_COMPAT;
    if (!IsReadable(heliHelper, sizeof(EXPECT_HELI_HELPER)) ||
        !BytesEqual(heliHelper, EXPECT_HELI_HELPER, sizeof(EXPECT_HELI_HELPER)))
    {
        if (H->log) H->log("helido    FAILED helicopter helper signature - wrong game build");
        return 1;
    }
    f_HelicopterPadCompat = (FnHelicopterPadCompat)heliHelper;

    u8* residenceTarget = EXE + RVA_VEHICLE_RESIDENCE_UPDATE;
    u8* officeScanTarget = EXE + RVA_DISTRIBUTION_OFFICE_SCAN;
    u8* assignVehicleTarget = EXE + RVA_DISTRIBUTION_ASSIGN_VEHICLE;
    u8* taskRebuildTarget = EXE + RVA_DISTRIBUTION_TASK_REBUILD;
    u8* taskRefreshTarget = EXE + RVA_DISTRIBUTION_TASK_REFRESH;
    u8* vehicleReadyTarget = EXE + RVA_VEHICLE_READY;
    u8* routeAdvanceTarget = EXE + RVA_ROUTE_ADVANCE;
    u8* targetSelectorTarget = EXE + RVA_DISTRIBUTION_TARGET_SELECTOR;
    u8* vehicleCompatTarget = EXE + RVA_VEHICLE_BUILDING_COMPAT;
    u8* taskTarget = EXE + RVA_DO_TASK_ELIGIBILITY;
    u8* menuTarget = EXE + RVA_BOTTOM_MENU_REBUILD;
    RuntimePrologueGuard residenceGuard = {};
    RuntimePrologueGuard officeScanGuard = {};
    RuntimePrologueGuard routeAdvanceGuard = {};
    RuntimePrologueGuard targetSelectorGuard = {};
    RuntimePrologueGuard assignVehicleGuard = {};
    RuntimePrologueGuard taskRebuildGuard = {};
    RuntimePrologueGuard taskGuard = {};
    RuntimePrologueGuard menuGuard = {};

    // Preflight every required Init-phase target before installing the first
    // detour. Once installation begins the DLL must remain resident because
    // TesmioLoader does not expose an unhook operation.
    if (!CaptureRuntimePrologue(residenceTarget, &residenceGuard,
                                "heli DO helicopter rehome arrival") ||
        !CaptureRuntimePrologue(officeScanTarget, &officeScanGuard,
                                "heli DO task-queue engine") ||
        !CaptureProven14ByteBoundary(routeAdvanceTarget, &routeAdvanceGuard,
                                    "heli DO unload-to-next-task bridge") ||
        !CaptureRuntimePrologue(targetSelectorTarget, &targetSelectorGuard,
                                "heli DO cargo-heliport target selector") ||
        !CaptureRuntimePrologue(assignVehicleTarget, &assignVehicleGuard,
                                "heli DO per-vehicle task projection") ||
        !CaptureRuntimePrologue(taskRebuildTarget, &taskRebuildGuard,
                                "heli DO mixed task-index rebuild") ||
        !ValidateDirectNativeCallable(taskRefreshTarget,
                                      "heli DO native task refresh") ||
        !ValidateDirectNativeCallable(vehicleReadyTarget,
                                      "heli DO native operational readiness") ||
        !IsReadable(vehicleCompatTarget, sizeof(EXPECT_COMPAT)) ||
        !BytesEqual(vehicleCompatTarget, EXPECT_COMPAT, sizeof(EXPECT_COMPAT)) ||
        !CaptureRuntimePrologue(taskTarget, &taskGuard,
                                "heli DO road/air task filter") ||
        !CaptureRuntimePrologue(menuTarget, &menuGuard,
                                "heli DO menu-safe helipad mask"))
    {
        if (H->log)
            H->log("helido    FAILED required-hook preflight - no HDO hooks installed");
        return 1;
    }

    f_DistributionAssignVehicle =
        (FnDistributionAssignVehicle)assignVehicleTarget;
    f_DistributionTaskRebuild =
        (FnDistributionTaskRebuild)taskRebuildTarget;
    f_DistributionTaskRefresh =
        (FnDistributionTaskRefresh)taskRefreshTarget;
    f_VehicleReady = (FnVehicleReady)vehicleReadyTarget;

    // After the first successful install the module must never be declined and
    // unloaded: TesmioLoader has no unhook API. An unexpected later failure
    // therefore keeps this DLL resident and suppresses the Start-phase UI hook.
    if (!InstallCapturedPrologueHook(residenceTarget,
                                     (void*)h_VehicleResidenceUpdate,
                                     (void**)&o_VehicleResidenceUpdate,
                                     &residenceGuard,
                                     "heli DO helicopter rehome arrival"))
    {
        if (H->log)
            H->log("helido    FAILED first hook install - safe to unload; no HDO hooks installed");
        return 1;
    }

    if (!InstallCapturedPrologueHook(officeScanTarget,
                                     (void*)h_DistributionOfficeScan,
                                     (void**)&o_DistributionOfficeScan,
                                     &officeScanGuard,
                                     "heli DO task-queue engine"))
    {
        if (H->log)
            H->log("helido    FATAL partial install after task-queue engine failure; DLL retained to keep detour valid");
        return 0;
    }

    if (!InstallCapturedPrologueHook(routeAdvanceTarget,
                                     (void*)h_RouteAdvance,
                                     (void**)&o_RouteAdvance,
                                     &routeAdvanceGuard,
                                     "heli DO unload-to-next-task bridge"))
    {
        if (H->log)
            H->log("helido    FATAL partial install after unload-to-next-task bridge failure; DLL retained to keep detours valid");
        return 0;
    }

    if (!InstallCapturedPrologueHook(targetSelectorTarget,
                                     (void*)h_DistributionTargetSelector,
                                     (void**)&o_DistributionTargetSelector,
                                     &targetSelectorGuard,
                                     "heli DO cargo-heliport target selector"))
    {
        if (H->log)
            H->log("helido    FATAL partial install after cargo-heliport selector failure; DLL retained to keep detours valid");
        return 0;
    }

    if (!InstallCapturedPrologueHook(assignVehicleTarget,
                                     (void*)h_DistributionAssignVehicle,
                                     (void**)&o_DistributionAssignVehicle,
                                     &assignVehicleGuard,
                                     "heli DO per-vehicle task projection"))
    {
        if (H->log)
            H->log("helido    FATAL partial install after per-vehicle task-projection failure; DLL retained to keep detours valid");
        return 0;
    }

    if (!InstallCapturedPrologueHook(taskRebuildTarget,
                                     (void*)h_DistributionTaskRebuild,
                                     (void**)&o_DistributionTaskRebuild,
                                     &taskRebuildGuard,
                                     "heli DO mixed task-index rebuild"))
    {
        if (H->log)
            H->log("helido    FATAL partial install after mixed task-index rebuild failure; DLL retained to keep detours valid");
        return 0;
    }

    if (!H->installInlineHook(vehicleCompatTarget,
                              (void*)h_VehicleBuildingCompat,
                              (void**)&o_VehicleBuildingCompat,
                              EXPECT_COMPAT, sizeof(EXPECT_COMPAT),
                              "heli DO vehicle compatibility"))
    {
        if (H->log)
            H->log("helido    FATAL partial install after vehicle-compatibility failure; DLL retained to keep detours valid");
        return 0;
    }

    if (!InstallCapturedPrologueHook(taskTarget,
                                     (void*)h_DoTaskEligibility,
                                     (void**)&o_DoTaskEligibility,
                                     &taskGuard,
                                     "heli DO road/air task filter"))
    {
        if (H->log)
            H->log("helido    FATAL partial install after task-filter failure; DLL retained to keep detours valid");
        return 0;
    }

    if (!InstallCapturedPrologueHook(menuTarget,
                                     (void*)h_BottomMenuRebuild,
                                     (void**)&o_BottomMenuRebuild,
                                     &menuGuard,
                                     "heli DO menu-safe helipad mask"))
    {
        if (H->log)
            H->log("helido    FATAL partial install after menu-mask failure; DLL retained to keep detours valid");
        return 0;
    }

    g_initReady = 1;
    if (H->log)
        H->log("helido    v1.2.0 initialized for WRSR v1.1.1.9: mixed road/helicopter dispatch, chained helicopter tasks and roadless cargo-heliport selection active");
    return 0;
}

EXPORT int TsmPluginStart(void)
{
    if (!H || !EXE) return 1;
    if (!g_initReady)
    {
        if (H->log)
            H->log("helido    Start phase suppressed because Init did not complete all required hooks");
        return 0;
    }

    // Guard all native calls used to create a real Distribution Office task row.
    u8* assignPush = EXE + RVA_ASSIGN_PUSH;
    u8* assignInit = EXE + RVA_ASSIGN_INIT;
    if (!IsReadable(assignPush, sizeof(EXPECT_ASSIGN_PUSH)) ||
        !BytesEqual(assignPush, EXPECT_ASSIGN_PUSH, sizeof(EXPECT_ASSIGN_PUSH)) ||
        !IsReadable(assignInit, sizeof(EXPECT_ASSIGN_INIT)) ||
        !BytesEqual(assignInit, EXPECT_ASSIGN_INIT, sizeof(EXPECT_ASSIGN_INIT)))
    {
        if (H->log) H->log("helido    overseas UI disabled: assignment helper signature mismatch");
        return 0; // helicopter support remains active
    }
    f_AssignPush = (FnAssignPush)assignPush;
    f_AssignInit = (FnAssignInit)assignInit;

    f_Malloc = (FnMalloc)ReadIatFunction(H->exeModule,
                                         "api-ms-win-crt-heap-l1-1-0.dll", "malloc");
    if (!f_Malloc)
    {
        if (H->log) H->log("helido    overseas UI disabled: game malloc import not resolved");
        return 0;
    }

    if (!ResolvePanelImports()) return 0;
    // DDO resolves this callable before HDO's start in the normal loader order,
    // so the two plugins can coexist: DDO's ship panel calls through this hook,
    // HDO sees subtype SHIP and simply forwards to the original road-DO panel.
    if (!H->installInlineHook(EXE + RVA_ROAD_DO_PANEL,
                              (void*)h_RoadDoPanel,
                              (void**)&o_RoadDoPanel,
                              EXPECT_ROAD_DO_PANEL, sizeof(EXPECT_ROAD_DO_PANEL),
                              "heli DO overseas UI"))
    {
        if (H->log) H->log("helido    overseas UI disabled: Distribution Office panel hook failed");
        return 0; // non-fatal; helicopter purchase/compatibility still works
    }

    if (H->log)
        H->log("helido    overseas UI active: native Soviet/Western border buttons grafted onto HDO Distribution Office panel");
    return 0;
}
