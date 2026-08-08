// Helicopter Distribution Office - TesmioLoader bridge
// v1.1.0, target SOVIET64.exe v1.1.1.7, TesmioLoader API 3
//
// Extends a native Distribution Office so it can own and dispatch cargo
// helicopters while retaining normal road-distribution behaviour. The Workshop
// asset supplies two native HELIPORT_STATION records and can attach additional
// heliports through the game's normal heliport-area system.
//
// Runtime changes are deliberately narrow:
//  1) helicopter purchase compatibility for the HDO is routed through the
//     vanilla helicopter-pad compatibility helper;
//  2) cargo-heliport tasks are rejected for road vehicles belonging to the HDO
//     while remaining native and available to helicopters;
//  3) the HDO's two helipad records are masked only while the native bottom-menu
//     classifier groups Workshop buildings, allowing the asset to appear with
//     ordinary Distribution Offices without removing its functional pads;
//  4) Soviet and Western overseas pseudo-buildings can be added as normal native
//     Distribution Office targets from the office panel;
//  5) when an already-existing helicopter is rehomed to the HDO through the
//     vehicle UI, the physical-arrival update temporarily presents that one HDO
//     instance as AIRPLANE_PARKING. This admits it through WRSR's native
//     helicopter residence transition so the game itself finalizes home ownership,
//     resident-roster membership and parking/station state.
//
// The rehome compatibility shim does not write vehicle home, route, resident
// roster, parking reservation or helipad occupancy fields. The real HDO type
// descriptor is restored immediately after the synchronous native arrival call.
// If the arrival hook cannot be safely installed, initialization fails before any
// established HDO hooks are applied, preventing a partial-plugin state.

#define TSM_API_VERSION 3u
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

// --------------------------------------------------------------------- v1.1.1.7 layout
static const usize RVA_VEHICLE_BUILDING_COMPAT = 0x003E2860u; // FUN_1403e2860
static const usize RVA_HELICOPTER_PAD_COMPAT    = 0x003E3AE0u; // FUN_1403e3ae0
static const usize RVA_ROAD_DO_PANEL            = 0x00741050u; // FUN_140741050
static const usize RVA_DO_TASK_ELIGIBILITY       = 0x001DE240u; // FUN_1401de240
static const usize RVA_BOTTOM_MENU_REBUILD        = 0x000797E0u; // FUN_1400797e0
static const usize RVA_ASSIGN_PUSH              = 0x0008E050u;
static const usize RVA_ASSIGN_INIT              = 0x00151230u;
static const usize RVA_VEHICLE_RESIDENCE_UPDATE         = 0x006CD0C0u; // FUN_1406cd0c0

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

// Vehicle state used by the native rehome/arrival path.
static const usize V_HOME          = 0x04F8u; // param_1[0x9f]
static const usize V_ROUTE_ACTIVE  = 0x05FDu;
static const usize V_ROUTE_BEGIN   = 0x0680u;
static const usize V_ROUTE_END     = 0x0688u;
static const usize V_ROUTE_INDEX   = 0x0698u;
static const usize V_ARRIVAL       = 0x0528u; // param_1[0xa5]
static const usize V_APPROACH      = 0x0530u; // param_1[0xa6]

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

// Distribution Office task record (0x80 bytes).
static const usize TASK_SOURCE     = 0x0010u;
static const usize TASK_DEST       = 0x0048u;

static const usize G_WESTERN_NODE       = 0x11AF8u;
static const usize G_SOVIET_NODE        = 0x11B00u;
static const usize GAME_SOVIET_ICON     = 0x0B2C0u;
static const usize GAME_WESTERN_ICON    = 0x0B2C8u;
static const usize G_MOUSE_OBJECT_RVA   = 0x00A54B90u;
static const usize G_PANEL_RVA          = 0x009BE060u;
static const usize G_PANEL_POS_RVA      = 0x009BE2F0u;
static const usize G_PANEL_SIZE_RVA     = 0x009BE2E8u;
static const usize G_PANEL_PAD_RVA      = 0x009BE2F8u;
static const usize G_PANEL_COLOR_RVA    = 0x009BE30Cu;
static const usize G_TECHNIQUE_RVA      = 0x009EAD08u;
static const usize G_PANEL_FULLSIZE_RVA = 0x00909F70u;
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
typedef void (*FnRoadDoPanel)(void* ui, void* window, void* descriptor, float clipTop);
typedef u64 (*FnDoTaskEligibility)(i64 game, i64* task, i64 vehicle, i64 office);
typedef void (*FnBottomMenuRebuild)(i64 game);
typedef void (*FnAssignPush)(u64* vector, void** value);
typedef void* (*FnAssignInit)(void* config);
typedef void (*FnVehicleResidenceUpdate)(void* vehicle, void* context);
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
static FnMalloc                  f_Malloc = 0;
static FnPanelDraw               f_PanelDraw = 0;
static FnGetMouseSolid           f_GetMouseSolid = 0;
static FnPanelCollisionRect      f_PanelCollisionRect = 0;

static void* g_buttonHome = 0;
static int g_buttonCapture = 0; // 0 none, 1 Soviet, 2 Western
static int g_lastLeftButtonDown = 0;
static int g_buttonSuppressFrames = 0;

// Private descriptor scratch used only during one synchronous native
// residence-update call. WRSR's simulation update is single-threaded here; the
// actual building descriptor and every building/vehicle state field remain
// game-owned.
__declspec(align(16)) static u8 g_residenceTypeDesc[TYPEDESC_STRIDE];
static void* g_lastArrivalVehicle = 0;

// --------------------------------------------------------------------- exact v1.1.1.7 byte guards
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

static u64 h_DoTaskEligibility(i64 game, i64* task, i64 vehicle, i64 office)
{
    // Only the mixed Helicopter Distribution Office gets this split. A cargo
    // heliport remains a perfectly normal road-accessible building everywhere
    // else in the game.
    if (office && vehicle && IsHdoBuilding((void*)office) &&
        !IsHelicopterVehicleObject((void*)vehicle) && TaskTouchesCargoHeliport(task))
    {
        return 0;
    }
    return o_DoTaskEligibility(game, task, vehicle, office);
}

// --------------------------------------------------------------------- existing helicopter -> HDO native arrival gate
static void* CurrentRouteTarget(void* vehicle)
{
    if (!vehicle || !IsReadable((u8*)vehicle + V_ROUTE_BEGIN, 0x20)) return 0;
    void** begin = *(void***)((u8*)vehicle + V_ROUTE_BEGIN);
    void** end   = *(void***)((u8*)vehicle + V_ROUTE_END);
    int index    = *(int*)((u8*)vehicle + V_ROUTE_INDEX);
    if (!begin || !end || end < begin || index < 0) return 0;
    usize count = (usize)(end - begin);
    if ((usize)index >= count || count > 256 || !IsReadable(begin, count * sizeof(void*))) return 0;
    return begin[index];
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

static void* FindRouteActiveHdoArrivalTarget(void* vehicle)
{
    if (!vehicle || !IsHelicopterVehicleObject(vehicle) ||
        !IsReadable((u8*)vehicle + V_ROUTE_ACTIVE, 1)) return 0;
    if (*(u8*)((u8*)vehicle + V_ROUTE_ACTIVE) == 0) return 0;

    void* home = ReadPointer(vehicle, V_HOME);
    void* arrival = ReadPointer(vehicle, V_ARRIVAL);
    void* approach = ReadPointer(vehicle, V_APPROACH);
    void* routeTarget = CurrentRouteTarget(vehicle);

    void* target = 0;
    if (arrival && IsHdoBuilding(arrival)) target = arrival;
    else if (approach && IsHdoBuilding(approach)) target = approach;
    else if (routeTarget && IsHdoBuilding(routeTarget)) target = routeTarget;

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
    void* target = FindRouteActiveHdoArrivalTarget(vehicle);
    if (!target)
    {
        o_VehicleResidenceUpdate(vehicle, context);
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
    // the duration of FUN_1406cd0c0. All HDO-specific station vectors, subtype,
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
        int count = ResidentVehicleCount(target);
        if (H && H->log)
            H->log("helido    existing helicopter reassignment: native arrival finalized; HDO resident vehicles=%d", count);
        if (g_lastArrivalVehicle == vehicle) g_lastArrivalVehicle = 0;
    }
}

// The v1.1.1.7 build is already byte-guarded at the helicopter helper and
// vehicle/building compatibility seam before we reach this point. For the task
// predicate we additionally require a plain position-independent MSVC prologue,
// then hand its live bytes to TesmioLoader's normal byte-checked hook installer.
// This avoids hardcoding another fragile compiler prologue while preserving the
// loader's trampoline safety requirements.
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

    // sub rsp, imm8 / imm32: 48 83 EC xx / 48 81 EC xxxxxxxx
    if (op == 0x83)
    {
        if (i + 2 <= available && p[i] == 0xEC) { *length = i + 2; return 1; }
        return 0;
    }
    if (op == 0x81)
    {
        if (i + 5 <= available && p[i] == 0xEC) { *length = i + 5; return 1; }
        return 0;
    }

    // mov memory/register forms used by MSVC prologues. Mod=1/2 addressing is
    // register-relative and therefore safe to relocate verbatim. Mod=0 is
    // refused because rm=5 is RIP-relative and there is no reason to accept a
    // more exotic no-displacement form in a function prologue.
    if (op == 0x88 || op == 0x89 || op == 0x8A || op == 0x8B)
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

static int InstallRuntimeGuardedPrologueHook(u8* target, void* detour,
                                              void** trampoline, const char* label)
{
    if (!target || !H || !H->installInlineHook || !IsReadable(target, 32)) return 0;
    u8 expect[32];
    usize stolen = 0;
    while (stolen < 14)
    {
        usize len = 0;
        if (!DecodeSafePrologueInstruction(target + stolen, 32 - stolen, &len) ||
            len == 0 || stolen + len > sizeof(expect))
        {
            if (H->log) H->log("helido    hook preflight refused: unsupported prologue at +0x%llX",
                               (u64)stolen);
            return 0;
        }
        stolen += len;
    }
    for (usize i = 0; i < stolen; ++i) expect[i] = target[i];
    return H->installInlineHook(target, detour, trampoline, expect, stolen, label);
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
    float full = GlobalFloat(G_PANEL_FULLSIZE_RVA, 1.0f);
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
    int hoverSoviet = DrawCompactOverseasButton(sovietIcon, x, rowY, icon, mouse, sovietAssigned);
    int hoverWestern = DrawCompactOverseasButton(westernIcon, westernX, rowY, icon, mouse, westernAssigned);

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
static void h_RoadDoPanel(void* ui, void* window, void* descriptor, float clipTop)
{
    void* building = ReadPointer(window, W_BUILDING);
    int ours = IsHdoBuilding(building);
    int before = ours ? AssignmentCount(building) : -1;

    o_RoadDoPanel(ui, window, descriptor, clipTop);

    if (!ours) return;
    int after = AssignmentCount(building);
    DrawOverseasButtons(ui, window, building, before >= 0 && after != before);
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
        info->version = "1.1.0";
    }

    if (!H || H->apiVersion != TSM_API_VERSION || !H->exeBase ||
        !H->installInlineHook || !H->readablePtr)
        return 1;

    EXE = H->exeBase;

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

    // Install the residence seam first. If it cannot be safely relocated, fail
    // before applying any of the established HDO hooks so startup cannot be left
    // with a partial plugin state.
    if (!InstallRuntimeGuardedPrologueHook(EXE + RVA_VEHICLE_RESIDENCE_UPDATE,
                                            (void*)h_VehicleResidenceUpdate,
                                            (void**)&o_VehicleResidenceUpdate,
                                            "heli DO existing-helicopter arrival"))
    {
        if (H->log) H->log("helido    FAILED existing-helicopter arrival hook - no HDO hooks installed");
        return 1;
    }

    if (!H->installInlineHook(EXE + RVA_VEHICLE_BUILDING_COMPAT,
                              (void*)h_VehicleBuildingCompat,
                              (void**)&o_VehicleBuildingCompat,
                              EXPECT_COMPAT, sizeof(EXPECT_COMPAT),
                              "heli DO vehicle compatibility"))
        return 1;

    if (!InstallRuntimeGuardedPrologueHook(EXE + RVA_DO_TASK_ELIGIBILITY,
                                            (void*)h_DoTaskEligibility,
                                            (void**)&o_DoTaskEligibility,
                                            "heli DO road/air task filter"))
    {
        if (H->log) H->log("helido    FAILED mixed-fleet task filter hook");
        return 1;
    }

    if (!InstallRuntimeGuardedPrologueHook(EXE + RVA_BOTTOM_MENU_REBUILD,
                                            (void*)h_BottomMenuRebuild,
                                            (void**)&o_BottomMenuRebuild,
                                            "heli DO menu-safe helipad mask"))
    {
        if (H->log) H->log("helido    FAILED menu-safe helipad mask hook");
        return 1;
    }

    if (H->log)
        H->log("helido    v1.1.0 initialized: helicopter distribution office support active; existing-helicopter rehome support active");
    return 0;
}

EXPORT int TsmPluginStart(void)
{
    if (!H || !EXE) return 1;

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
