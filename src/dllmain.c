#include "winapi.h"

void* memcpy(void* dst, const void* src, unsigned long long n);
void* memset(void* dst, int value, unsigned long long n);

#define PAL_CHARS 47
#define CHARS 51
#define BASE_COLORS 4
#define COLOR_COUNT 8
#define PAL_BYTES 512
#define PAL_FILE (PAL_CHARS * BASE_COLORS * PAL_BYTES)
#define COLS 16
#define ROWS 3
#define GRID_COLS 4
#define GRID_ROWS 2
#define CELL_W 150
#define CELL_H 196
#define GAP 12
#define PAD 16
#define SPR_W 256
#define SPR_H 255
#define WIN_W (PAD + GRID_COLS * CELL_W + (GRID_COLS - 1) * GAP + PAD)
#define WIN_H (PAD + GRID_ROWS * CELL_H + (GRID_ROWS - 1) * GAP + PAD)

#define SCENE_RVA 0x19F537Au
#define P1_RVA 0x19F1115u
#define P2_RVA 0x19F0D6Eu

#define PAD_UP 0x0001u
#define PAD_DOWN 0x0002u
#define PAD_LEFT 0x0004u
#define PAD_RIGHT 0x0008u
#define PAD_START 0x0010u
#define PAD_BACK 0x0020u
#define PAD_A 0x1000u
#define PAD_B 0x2000u
#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define PAGE_EXECUTE_READWRITE 0x40u

typedef DWORD (WINAPI *XInputGetState_fn)(DWORD, void*);

typedef struct XPad {
    u16 buttons;
    u8 lt, rt;
    short lx, ly, rx, ry;
} XPad;

typedef struct XState {
    DWORD packet;
    XPad pad;
} XState;

typedef struct Latch {
    int player;
    int slot;
    int id;
    int color;
    int on;
} Latch;

static HMODULE g_self;
static u8* g_base;
static u8 g_pal[PAL_FILE];
static int g_pal_ok;
typedef struct Pick {
    HWND hwnd;
    int on;
    int player;
    int slot;
    int id;
    int choice;
    int original;
    u16 pad_prev;
    int pad_idle;
    u32 abcd_prev;
    int sheet_ok;
    int sheet_w;
    int sheet_h;
    u8* pix;
} Pick;

static HWND g_hwnd;
static int g_visible;
static int g_player;
static Pick g_pk[2];
static u32 g_slot_raw[8];
static u32 g_slot_xi[4];
static int g_slot;
static int g_id;
static int g_choice;
static int g_original;
static int g_prev[2][3];
static int g_rows[BASE_COLORS][ROWS];
static Latch g_latch[6];
static u8 g_owned[2][CHARS];
static int g_q_p[6];
static int g_q_s[6];
static int g_q_id[6];
static int g_q_n;
static XInputGetState_fn g_xinput;
static int g_pad_ready;
static u16 g_pad_prev;
static int g_pad_idle;
static int g_pad_seen;
static wchar_t g_log[MAX_PATH];
static int g_scene_log;
static u64 g_swallow_until;
static volatile u32 g_held_raw;
static volatile u32 g_held_x;
static int g_input_hooked;
static u8* g_in_tramp;
static u8 g_pix[SPR_W * SPR_H];
static DWORD g_costume[COLOR_COUNT][256];
static u8* g_grc_in;
static u8* g_grc_out;
static int g_spr_ok;
static int g_bg;
static int g_x0, g_y0, g_x1, g_y1;
static u8 g_scale[150 * 170 * 3];
static u8* g_arr;
static int g_arr_n;
static int g_arr_base;
static u8* g_sheet;
static u8* g_sheet_file;
static int g_sheet_w;
static int g_sheet_h;
static int g_sheet_ok;
static volatile u8 g_force[CHARS];
static u32 g_abcd_prev;
static u8* g_res_tramp;
static u8* g_order_tramp;
static volatile int g_order_held;
static volatile int g_order_wait;
static volatile int g_in_order;
static volatile int g_answered[2][3];
static int g_cpu_logged[2];
static u64 g_order_stamp;

/* Cells follow the sheets: A B C D / A+B C+D A+D B+C. The game stores C+D as 4 and A+B as 5. */
static const int g_show[COLOR_COUNT] = { 0, 1, 2, 3, 5, 4, 6, 7 };

static int lstrlenA(const char* s);
static void copy_str(char* dst, int cap, const char* src);
static void cat_str(char* dst, int cap, const char* src);
static void cat_int(char* dst, int cap, int value);
static int pos_of(int color);

static const char* g_color_name[COLOR_COUNT] = {
    "A", "B", "C", "D", "C+D", "A+B", "A+D", "B+C"
};

static const char* g_names[CHARS] = {
    "Kyo", "Benimaru", "Daimon", "Terry", "Andy", "Joe", "Ryo", "Robert", "Yuri",
    "Leona", "Ralf", "Clark", "Athena", "Kensou", "Chin", "Chizuru", "Mai", "King",
    "Kim", "Chang", "Choi", "Yashiro", "Shermie", "Chris", "Yamazaki", "Mary", "Billy",
    "Iori", "Mature", "Vice", "Heidern", "Takuma", "Saisyu", "Heavy D", "Lucky", "Brian",
    "Rugal", "Shingo", "Kasumi", "Geese", "Krauser", "Mr. Big", "Goenitz",
    "Iori Orochi", "Leona Orochi", "Orochi", "Eiji", "Yashiro Orochi", "Shermie Orochi",
    "Chris Orochi", "Omega Rugal"
};

static void log_line(const char* text) {
    HANDLE f;
    DWORD w = 0;
    if (!g_log[0] || !text) return;
    f = CreateFileW(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE) return;
    WriteFile(f, text, (DWORD)lstrlenA(text), &w, 0);
    WriteFile(f, "\r\n", 2, &w, 0);
    CloseHandle(f);
}

static int lstrlenA(const char* s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static void copy_str(char* dst, int cap, const char* src) {
    int i = 0;
    if (!dst || cap <= 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void cat_str(char* dst, int cap, const char* src) {
    int n = lstrlenA(dst);
    int i = 0;
    if (!src) return;
    while (src[i] && n + 1 < cap) dst[n++] = src[i++];
    dst[n] = 0;
}

static void cat_int(char* dst, int cap, int value) {
    char rev[16];
    int n = 0;
    unsigned v;
    if (value < 0) {
        cat_str(dst, cap, "-");
        value = -value;
    }
    v = (unsigned)value;
    if (v == 0) rev[n++] = '0';
    while (v && n < 15) {
        rev[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        char one[2];
        one[0] = rev[--n];
        one[1] = 0;
        cat_str(dst, cap, one);
    }
}

static void log_num(const char* prefix, int n) {
    char buf[80];
    copy_str(buf, 80, prefix);
    cat_int(buf, 80, n);
    log_line(buf);
}

static void setup_pad(void) {
    static const wch* names[2] = { L"XInput1_4.dll", L"xinput1_3.dll" };
    HMODULE mod = 0;
    u8* fn;
    int i;
    wchar_t path[MAX_PATH];
    if (g_pad_ready) return;
    for (i = 0; i < 2; ++i) {
        mod = GetModuleHandleW(names[i]);
        if (!mod) mod = LoadLibraryW(names[i]);
        if (mod) break;
    }
    if (!mod) {
        int n = (int)GetModuleFileNameW(0, path, MAX_PATH);
        while (n > 0 && path[n - 1] != L'\\') --n;
        path[n] = 0;
        {
            const wch* tail = L"XInput1_4.dll";
            int t = 0;
            while (tail[t] && n + 1 < MAX_PATH) path[n++] = tail[t++];
            path[n] = 0;
        }
        mod = LoadLibraryW(path);
    }
    if (!mod) return;
    fn = (u8*)GetProcAddress(mod, "XInputGetState");
    if (!fn) return;
    g_xinput = (XInputGetState_fn)fn;
    g_pad_ready = 1;
    log_line("mando leido");
}

static void latch_apply(void);
static int colors_ready(void);
static void order_pump(void);
static int cursor_block(void);
static void watch_select(void);

static void hk_game_input(void) {
    u32 raw = 0;
    u32 xin = 0;
    int block;
    int i;
    for (i = 0; i < 8; ++i) {
        g_slot_raw[i] = *(u32*)(g_base + 0x5527A0u + (unsigned)i * 4u);
        raw |= g_slot_raw[i];
    }
    block = (g_pk[0].on || g_pk[1].on) || GetTickCount64() < g_swallow_until;
    if (block) {
        g_held_raw = raw;
        for (i = 0; i < 8; ++i) *(u32*)(g_base + 0x5527A0u + (unsigned)i * 4u) = 0;
    }
    ((void (*)(void))g_in_tramp)();
    if (block) {
        for (i = 0; i < 4; ++i) {
            g_slot_xi[i] = *(u32*)(g_base + 0x5527E8u + (unsigned)i * 4u);
            xin |= g_slot_xi[i];
        }
        g_held_x = xin;
        for (i = 0; i < 8; ++i) {
            *(u32*)(g_base + 0x5527A0u + (unsigned)i * 4u) = 0;
            *(u32*)(g_base + 0x5527C8u + (unsigned)i * 4u) = 0;
            *(u32*)(g_base + 0x552770u + (unsigned)i * 4u) = 0;
        }
        for (i = 0; i < 4; ++i) *(u32*)(g_base + 0x5527E8u + (unsigned)i * 4u) = 0;
    }
    /* Sync with the game thread: enqueue color as soon as the last character
       locks in, before order/music can advance on the same frame. */
    watch_select();
    if (colors_ready()) latch_apply();
    order_pump();
}

static void emit_pop_vol(u8* stub, int* n) {
    stub[(*n)++] = 0x58;
    stub[(*n)++] = 0x41; stub[(*n)++] = 0x5B;
    stub[(*n)++] = 0x41; stub[(*n)++] = 0x5A;
    stub[(*n)++] = 0x41; stub[(*n)++] = 0x59;
    stub[(*n)++] = 0x41; stub[(*n)++] = 0x58;
    stub[(*n)++] = 0x5A;
    stub[(*n)++] = 0x59;
}

static void install_game_input(void) {
    static const u8 prologue[15] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18
    };
    u8* fn;
    u8* tramp;
    u8* stub;
    DWORD old = 0;
    int i;
    int n;
    int jnz_at;
    if (g_input_hooked || !g_base) return;
    fn = g_base + 0x5AE30u;
    for (i = 0; i < 15; ++i) if (fn[i] != prologue[i]) {
        log_line("no pude frenar el cursor del juego");
        g_input_hooked = -1;
        return;
    }
    tramp = (u8*)VirtualAlloc(0, 48, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    stub = (u8*)VirtualAlloc(0, 160, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp || !stub) return;
    for (i = 0; i < 15; ++i) tramp[i] = fn[i];
    tramp[15] = 0x48;
    tramp[16] = 0xB8;
    *(u64*)(tramp + 17) = (u64)(fn + 15);
    tramp[25] = 0xFF;
    tramp[26] = 0xE0;
    g_in_tramp = tramp;
    n = 0;
    stub[n++] = 0x51;
    stub[n++] = 0x52;
    stub[n++] = 0x41; stub[n++] = 0x50;
    stub[n++] = 0x41; stub[n++] = 0x51;
    stub[n++] = 0x41; stub[n++] = 0x52;
    stub[n++] = 0x41; stub[n++] = 0x53;
    stub[n++] = 0x50;
    stub[n++] = 0x48; stub[n++] = 0x83; stub[n++] = 0xEC; stub[n++] = 0x28;
    stub[n++] = 0x48; stub[n++] = 0xB8;
    *(u64*)(stub + n) = (u64)cursor_block;
    n += 8;
    stub[n++] = 0xFF; stub[n++] = 0xD0;
    stub[n++] = 0x48; stub[n++] = 0x83; stub[n++] = 0xC4; stub[n++] = 0x28;
    stub[n++] = 0x85; stub[n++] = 0xC0;
    stub[n++] = 0x75;
    jnz_at = n;
    stub[n++] = 0x00;
    emit_pop_vol(stub, &n);
    stub[n++] = 0xFF; stub[n++] = 0x25; stub[n++] = 0; stub[n++] = 0; stub[n++] = 0; stub[n++] = 0;
    *(u64*)(stub + n) = (u64)tramp;
    n += 8;
    stub[jnz_at] = (u8)(n - (jnz_at + 1));
    emit_pop_vol(stub, &n);
    stub[n++] = 0xFF; stub[n++] = 0x25; stub[n++] = 0; stub[n++] = 0; stub[n++] = 0; stub[n++] = 0;
    *(u64*)(stub + n) = (u64)hk_game_input;
    n += 8;
    if (!VirtualProtect(fn, 15, PAGE_EXECUTE_READWRITE, &old)) return;
    fn[0] = 0x48;
    fn[1] = 0xB8;
    *(u64*)(fn + 2) = (u64)stub;
    fn[10] = 0xFF;
    fn[11] = 0xE0;
    fn[12] = 0x90;
    fn[13] = 0x90;
    fn[14] = 0x90;
    VirtualProtect(fn, 15, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 15);
    FlushInstructionCache(GetCurrentProcess(), stub, 160);
    g_input_hooked = 1;
    log_line("cursor del juego quieto mientras eliges color");
}

static u8* g_btn_tramp;

static int cursor_block(void) {
    return g_visible || GetTickCount64() < g_swallow_until;
}

static void install_cursor_block(void) {
    static const u8 prologue[15] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20
    };
    u8* fn;
    u8* stub;
    DWORD old = 0;
    int i;
    int n;
    if (!g_base) return;
    fn = g_base + 0x5B240u;
    for (i = 0; i < 15; ++i) if (fn[i] != prologue[i]) {
        log_line("no pude frenar los botones del cursor");
        return;
    }
    g_btn_tramp = (u8*)VirtualAlloc(0, 48, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    stub = (u8*)VirtualAlloc(0, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_btn_tramp || !stub) return;
    for (i = 0; i < 15; ++i) g_btn_tramp[i] = fn[i];
    g_btn_tramp[15] = 0x48;
    g_btn_tramp[16] = 0xB8;
    *(u64*)(g_btn_tramp + 17) = (u64)(fn + 15);
    g_btn_tramp[25] = 0xFF;
    g_btn_tramp[26] = 0xE0;
    n = 0;
    stub[n++] = 0x51;
    stub[n++] = 0x52;
    stub[n++] = 0x41; stub[n++] = 0x50;
    stub[n++] = 0x41; stub[n++] = 0x51;
    stub[n++] = 0x41; stub[n++] = 0x52;
    stub[n++] = 0x41; stub[n++] = 0x53;
    stub[n++] = 0x50;
    stub[n++] = 0x48; stub[n++] = 0x83; stub[n++] = 0xEC; stub[n++] = 0x28;
    stub[n++] = 0x48; stub[n++] = 0xB8;
    *(u64*)(stub + n) = (u64)cursor_block;
    n += 8;
    stub[n++] = 0xFF; stub[n++] = 0xD0;
    stub[n++] = 0x48; stub[n++] = 0x83; stub[n++] = 0xC4; stub[n++] = 0x28;
    stub[n++] = 0x85; stub[n++] = 0xC0;
    stub[n++] = 0x75;
    {
        int jnz_at = n;
        stub[n++] = 0x00;
        emit_pop_vol(stub, &n);
        stub[n++] = 0xFF; stub[n++] = 0x25; stub[n++] = 0; stub[n++] = 0; stub[n++] = 0; stub[n++] = 0;
        *(u64*)(stub + n) = (u64)g_btn_tramp;
        n += 8;
        stub[jnz_at] = (u8)(n - (jnz_at + 1));
    }
    emit_pop_vol(stub, &n);
    stub[n++] = 0x31; stub[n++] = 0xC0;
    stub[n++] = 0xC3;
    if (!VirtualProtect(fn, 15, PAGE_EXECUTE_READWRITE, &old)) return;
    fn[0] = 0x48;
    fn[1] = 0xB8;
    *(u64*)(fn + 2) = (u64)stub;
    fn[10] = 0xFF;
    fn[11] = 0xE0;
    fn[12] = 0x90;
    fn[13] = 0x90;
    fn[14] = 0x90;
    VirtualProtect(fn, 15, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 15);
    FlushInstructionCache(GetCurrentProcess(), stub, 128);
    log_line("botones del cursor frenados");
}

static u8* player_ptr(int player) {
    return g_base + (player == 0 ? P1_RVA : P2_RVA);
}

static int scene_now(void) {
    return g_base[SCENE_RVA];
}

static u8* g_team_tramp;
static u8* g_spawn_tramp;

static void apply_team_colors(u8* chars, u8* colors) {
    int player, n, i;
    if (!chars || !colors) return;
    for (player = 0; player < 2; ++player) {
        u8* p = player_ptr(player);
        int ok = 1;
        for (n = 0; n < 3; ++n) {
            int slot = p[0x0A + n];
            if (slot > 2 || p[4 + slot] != chars[n]) {
                ok = 0;
                break;
            }
        }
        if (!ok) continue;
        for (n = 0; n < 3; ++n) {
            int slot = p[0x0A + n];
            for (i = 0; i < 6; ++i) {
                if (!g_latch[i].on || g_latch[i].player != player || g_latch[i].slot != slot) continue;
                if (g_latch[i].id != (int)chars[n]) continue;
                colors[n] = (u8)(g_latch[i].color & 7);
            }
        }
        return;
    }
    for (n = 0; n < 3; ++n) {
        int hits = 0;
        int color = colors[n];
        for (i = 0; i < 6; ++i) {
            if (!g_latch[i].on || g_latch[i].id != (int)chars[n]) continue;
            color = g_latch[i].color & 7;
            ++hits;
        }
        if (hits == 1) colors[n] = (u8)color;
    }
}

static void hk_team(u8* chars, u8* colors) {
    latch_apply();
    ((void (*)(u8*, u8*))g_team_tramp)(chars, colors);
    apply_team_colors(chars, colors);
}

static void install_team_hook(void) {
    static const u8 prologue[15] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18
    };
    u8* fn;
    u8* tramp;
    DWORD old = 0;
    int i;
    fn = g_base + 0x281F20u;
    for (i = 0; i < 15; ++i) if (fn[i] != prologue[i]) {
        log_line("no enganche el color del equipo");
        return;
    }
    tramp = (u8*)VirtualAlloc(0, 48, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return;
    for (i = 0; i < 15; ++i) tramp[i] = fn[i];
    tramp[15] = 0x48;
    tramp[16] = 0xB8;
    *(u64*)(tramp + 17) = (u64)(fn + 15);
    tramp[25] = 0xFF;
    tramp[26] = 0xE0;
    g_team_tramp = tramp;
    if (!VirtualProtect(fn, 15, PAGE_EXECUTE_READWRITE, &old)) return;
    fn[0] = 0x48;
    fn[1] = 0xB8;
    *(u64*)(fn + 2) = (u64)&hk_team;
    fn[10] = 0xFF;
    fn[11] = 0xE0;
    fn[12] = 0x90;
    fn[13] = 0x90;
    fn[14] = 0x90;
    VirtualProtect(fn, 15, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 15);
    log_line("color del equipo enganchado");
}

static u8* hk_palette(unsigned id, unsigned color) {
    u16* table;
    u64 palbase;
    unsigned index;
    if (id < CHARS && g_force[id] <= 7) color = g_force[id];
    color &= 7u;
    if (id > 50) id = 0;
    index = id * 8u + color;
    table = (u16*)(g_base + 0xA0DD22u);
    palbase = *(u64*)(g_base + 0x1AA1BA8u);
    return (u8*)(palbase + (unsigned long long)table[index] * 32ull);
}

static void install_palette_hook(void) {
    static const u8 expect[12] = {
        0x8D, 0x04, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x48, 0x63, 0xC8, 0x81, 0xE2
    };
    u8* fn = g_base + 0x27EFD0u;
    DWORD old = 0;
    int i;
    for (i = 0; i < 12; ++i) if (fn[i] != expect[i]) {
        log_line("no enganche la paleta");
        return;
    }
    if (!VirtualProtect(fn, 12, PAGE_EXECUTE_READWRITE, &old)) return;
    fn[0] = 0x48;
    fn[1] = 0xB8;
    *(u64*)(fn + 2) = (u64)&hk_palette;
    fn[10] = 0xFF;
    fn[11] = 0xE0;
    VirtualProtect(fn, 12, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 12);
    log_line("paleta enganchada");
}

static u8 hk_resolve(u8 id, u8 slot, u8 requested) {
    u8* active = *(u8**)(g_base + 0x779958u);
    int player = -1;
    int i;
    int found = -1;
    int hits = 0;
    int only = -1;
    if (active == player_ptr(0)) player = 0;
    else if (active == player_ptr(1)) player = 1;
    for (i = 0; i < 6; ++i) {
        if (!g_latch[i].on || g_latch[i].id != (int)id) continue;
        if (player >= 0 && g_latch[i].player != player) continue;
        if (g_latch[i].slot == (int)slot) found = g_latch[i].color & 7;
        only = g_latch[i].color & 7;
        ++hits;
    }
    if (player >= 0 && found >= 0) requested = (u8)found;
    else if (hits == 1 && only >= 0) requested = (u8)only;
    return ((u8 (*)(u8, u8, u8))g_res_tramp)(id, slot, requested);
}

static void install_resolve_hook(void) {
    static const u8 prologue[15] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18
    };
    u8* fn = g_base + 0x275470u;
    u8* tramp;
    DWORD old = 0;
    int i;
    for (i = 0; i < 15; ++i) if (fn[i] != prologue[i]) {
        log_line("no enganche el resolvedor");
        return;
    }
    tramp = (u8*)VirtualAlloc(0, 48, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return;
    for (i = 0; i < 15; ++i) tramp[i] = fn[i];
    tramp[15] = 0x48;
    tramp[16] = 0xB8;
    *(u64*)(tramp + 17) = (u64)(fn + 15);
    tramp[25] = 0xFF;
    tramp[26] = 0xE0;
    g_res_tramp = tramp;
    if (!VirtualProtect(fn, 15, PAGE_EXECUTE_READWRITE, &old)) return;
    fn[0] = 0x48;
    fn[1] = 0xB8;
    *(u64*)(fn + 2) = (u64)&hk_resolve;
    fn[10] = 0xFF;
    fn[11] = 0xE0;
    fn[12] = 0x90;
    fn[13] = 0x90;
    fn[14] = 0x90;
    VirtualProtect(fn, 15, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 15);
    log_line("resolvedor de color enganchado");
}

static const char* char_name(int id);

static int team_side(u8* team) {
    int player, slot, score, best, best_score;
    if (!team) return -1;
    if (team == player_ptr(0)) return 0;
    if (team == player_ptr(1)) return 1;
    best = -1;
    best_score = 1;
    for (player = 0; player < 2; ++player) {
        u8* p = player_ptr(player);
        score = 0;
        for (slot = 0; slot < 3; ++slot) {
            int id = p[4 + slot];
            int k;
            if (id < 0 || id >= CHARS) continue;
            for (k = 0; k < 3; ++k) if (team[4 + k] == (u8)id) {
                ++score;
                break;
            }
        }
        if (score > best_score) {
            best = player;
            best_score = score;
        }
    }
    return best;
}

static int latched_color(int player, int slot, int id) {
    int i, any = -1, n = 0, exact = -1;
    if (id < 0 || id >= CHARS) return -1;
    for (i = 0; i < 6; ++i) {
        if (!g_latch[i].on || g_latch[i].id != id) continue;
        if (player >= 0 && g_latch[i].player != player) continue;
        any = g_latch[i].color & 7;
        ++n;
        if (player >= 0 && g_latch[i].slot == slot) exact = any;
    }
    if (exact >= 0) return exact;
    if (player >= 0 && n > 0) return any;
    if (player < 0 && n == 1) return any;
    return -1;
}

static void paint_team(u8* team) {
    int side, slot, wrote;
    char msg[140];
    if ((u64)team < 0x10000ull) return;
    side = team_side(team);
    wrote = 0;
    for (slot = 0; slot < 3; ++slot) {
        int id = team[4 + slot];
        int color = latched_color(side, slot, id);
        if (color < 0) color = latched_color(-1, slot, id);
        if (color < 0) continue;
        team[7 + slot] = (u8)color;
        ++wrote;
    }
    if (!wrote) return;
    copy_str(msg, 140, "pelea ");
    cat_str(msg, 140, side == 1 ? "P2" : "P1");
    for (slot = 0; slot < 3; ++slot) {
        int id = team[4 + slot];
        int color;
        if (id < 0 || id >= CHARS) continue;
        color = team[7 + slot] & 7;
        cat_str(msg, 140, " ");
        cat_str(msg, 140, char_name(id));
        cat_str(msg, 140, " ");
        if (color >= 0 && color < COLOR_COUNT) cat_str(msg, 140, g_color_name[color]);
    }
    log_line(msg);
}

static void hk_spawn(void) {
    if (g_base) {
        latch_apply();
        paint_team(*(u8**)(g_base + 0x779968u));
    }
    ((void (*)(void))g_spawn_tramp)();
}

static void install_spawn_hook(void) {
    static const u8 prologue[15] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18
    };
    u8* fn = g_base + 0x2453A0u;
    u8* tramp;
    DWORD old = 0;
    int i;
    for (i = 0; i < 15; ++i) if (fn[i] != prologue[i]) {
        log_line("no enganche la pelea");
        return;
    }
    tramp = (u8*)VirtualAlloc(0, 48, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return;
    for (i = 0; i < 15; ++i) tramp[i] = fn[i];
    tramp[15] = 0x48;
    tramp[16] = 0xB8;
    *(u64*)(tramp + 17) = (u64)(fn + 15);
    tramp[25] = 0xFF;
    tramp[26] = 0xE0;
    g_spawn_tramp = tramp;
    if (!VirtualProtect(fn, 15, PAGE_EXECUTE_READWRITE, &old)) return;
    fn[0] = 0x48;
    fn[1] = 0xB8;
    *(u64*)(fn + 2) = (u64)&hk_spawn;
    fn[10] = 0xFF;
    fn[11] = 0xE0;
    fn[12] = 0x90;
    fn[13] = 0x90;
    fn[14] = 0x90;
    VirtualProtect(fn, 15, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 15);
    log_line("pelea enganchada");
}

static int g_pick;

static int color_at_slot(u8* team, unsigned index) {
    int id, side, color;
    if ((u64)team < 0x10000ull || index > 2) return -1;
    id = team[4 + index];
    if (id < 0 || id >= CHARS) return -1;
    side = team_side(team);
    if (side >= 0 && g_owned[side][id] <= 7) return g_owned[side][id];
    if (side < 0) return latched_color(-1, -1, id);
    color = latched_color(side, (int)index, id);
    if (color < 0) color = latched_color(side, -1, id);
    return color;
}

static int g_hit_log;

static int owned_color(u8* obj) {
    int id, side, color, now;
    char msg[80];
    if ((u64)obj < 0x10000ull) return -1;
    id = obj[0x70];
    if (id < 0 || id >= CHARS) id = obj[0xD6];
    if (id < 0 || id >= CHARS) return -1;
    side = obj[0x170] & 1;
    color = g_owned[side][id];
    if (color > 7) {
        color = g_owned[side ^ 1][id];
        if (color <= 7 && g_owned[side][id] > 7 && g_owned[side ^ 1][id] <= 7) side ^= 1;
    }
    if (color > 7) return -1;
    now = obj[0xDD];
    if (now != color && g_hit_log < 24) {
        ++g_hit_log;
        copy_str(msg, 80, side ? "paleta P2 " : "paleta P1 ");
        cat_str(msg, 80, char_name(id));
        cat_str(msg, 80, " ");
        cat_str(msg, 80, g_color_name[color]);
        log_line(msg);
    }
    return color;
}

static int color_on_fighter(u8* fighter) {
    return owned_color(fighter);
}

static int spawn_color(void) {
    u8* fighter;
    if (!g_base) return -1;
    fighter = *(u8**)(g_base + 0x779960u);
    return owned_color(fighter);
}

static void write_u64(u8* p, u64 v) {
    int i;
    for (i = 0; i < 8; ++i) p[i] = (u8)(v >> (8 * i));
}

static void write_i32(u8* p, int v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static void emit_abs(u8* s, int* n, u8 rex, u8 op, u64 addr) {
    s[(*n)++] = rex;
    s[(*n)++] = op;
    write_u64(s + *n, addr);
    *n += 8;
}

static void emit_jmp_abs(u8* s, int* n, u64 addr) {
    s[(*n)++] = 0xFF;
    s[(*n)++] = 0x25;
    s[(*n)++] = 0x00;
    s[(*n)++] = 0x00;
    s[(*n)++] = 0x00;
    s[(*n)++] = 0x00;
    write_u64(s + *n, addr);
    *n += 8;
}

static u8* build_color_stub(void* fn, int mode, const u8* orig, int orig_len, u8* back) {
    static u8* block;
    static int used;
    u8* s;
    int n = 0;
    int i;
    int jl_at;
    if (!block) {
        block = (u8*)VirtualAlloc(0, 16384, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        used = 0;
    }
    if (!block || used + 240 > 16384) return 0;
    s = block + used;
    s[n++] = 0x50;
    s[n++] = 0x51;
    s[n++] = 0x52;
    s[n++] = 0x41; s[n++] = 0x50;
    s[n++] = 0x41; s[n++] = 0x51;
    s[n++] = 0x41; s[n++] = 0x52;
    s[n++] = 0x41; s[n++] = 0x53;
    s[n++] = 0x56;
    s[n++] = 0x48; s[n++] = 0x89; s[n++] = 0xE6;
    s[n++] = 0x48; s[n++] = 0x83; s[n++] = 0xE4; s[n++] = 0xF0;
    s[n++] = 0x48; s[n++] = 0x83; s[n++] = 0xEC; s[n++] = 0x20;
    if (mode == 1) {
        s[n++] = 0x48; s[n++] = 0x89; s[n++] = 0xD1;
    } else if (mode == 3) {
        s[n++] = 0x48; s[n++] = 0x89; s[n++] = 0xC1;
    } else if (mode == 0) {
        s[n++] = 0x48; s[n++] = 0x89; s[n++] = 0xD9;
        s[n++] = 0x89; s[n++] = 0xC2;
    }
    emit_abs(s, &n, 0x48, 0xB8, (u64)fn);
    s[n++] = 0xFF; s[n++] = 0xD0;
    s[n++] = 0x89; s[n++] = 0xC2;
    emit_abs(s, &n, 0x48, 0xB8, (u64)&g_pick);
    s[n++] = 0x89; s[n++] = 0x10;
    s[n++] = 0x48; s[n++] = 0x89; s[n++] = 0xF4;
    s[n++] = 0x5E;
    s[n++] = 0x41; s[n++] = 0x5B;
    s[n++] = 0x41; s[n++] = 0x5A;
    s[n++] = 0x41; s[n++] = 0x59;
    s[n++] = 0x41; s[n++] = 0x58;
    s[n++] = 0x5A;
    s[n++] = 0x59;
    s[n++] = 0x58;
    s[n++] = 0x50;
    emit_abs(s, &n, 0x48, 0xB8, (u64)&g_pick);
    s[n++] = 0x8B; s[n++] = 0x00;
    s[n++] = 0x83; s[n++] = 0xF8; s[n++] = 0x00;
    s[n++] = 0x7C;
    jl_at = n;
    s[n++] = 0x00;
    s[n++] = 0x48; s[n++] = 0x83; s[n++] = 0xC4; s[n++] = 0x08;
    if (mode == 0 || mode == 3) {
        s[n++] = 0x89; s[n++] = 0xC1;
    }
    emit_jmp_abs(s, &n, (u64)back);
    s[jl_at] = (u8)(n - (jl_at + 1));
    s[n++] = 0x58;
    for (i = 0; i < orig_len; ++i) s[n++] = orig[i];
    emit_jmp_abs(s, &n, (u64)back);
    used += (n + 15) & ~15;
    return s;
}

static u8* build_edx_stub(void* fn, const u8* orig, int orig_len, u8* back) {
    static u8* block;
    static int used;
    u8* s;
    int n = 0;
    int i;
    int jl_at;
    if (!block) {
        block = (u8*)VirtualAlloc(0, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        used = 0;
    }
    if (!block || used + 200 > 4096) return 0;
    s = block + used;
    s[n++] = 0x50;
    s[n++] = 0x51;
    s[n++] = 0x52;
    s[n++] = 0x41; s[n++] = 0x50;
    s[n++] = 0x41; s[n++] = 0x51;
    s[n++] = 0x41; s[n++] = 0x52;
    s[n++] = 0x41; s[n++] = 0x53;
    s[n++] = 0x56;
    s[n++] = 0x48; s[n++] = 0x89; s[n++] = 0xE6;
    s[n++] = 0x48; s[n++] = 0x83; s[n++] = 0xE4; s[n++] = 0xF0;
    s[n++] = 0x48; s[n++] = 0x83; s[n++] = 0xEC; s[n++] = 0x20;
    emit_abs(s, &n, 0x48, 0xB8, (u64)fn);
    s[n++] = 0xFF; s[n++] = 0xD0;
    s[n++] = 0x83; s[n++] = 0xF8; s[n++] = 0x00;
    s[n++] = 0x7C;
    jl_at = n;
    s[n++] = 0x00;
    s[n++] = 0x89; s[n++] = 0x46; s[n++] = 0x28;
    s[n++] = 0x48; s[n++] = 0x89; s[n++] = 0xF4;
    s[n++] = 0x5E;
    s[n++] = 0x41; s[n++] = 0x5B;
    s[n++] = 0x41; s[n++] = 0x5A;
    s[n++] = 0x41; s[n++] = 0x59;
    s[n++] = 0x41; s[n++] = 0x58;
    s[n++] = 0x5A;
    s[n++] = 0x59;
    s[n++] = 0x58;
    emit_jmp_abs(s, &n, (u64)back);
    s[jl_at] = (u8)(n - (jl_at + 1));
    s[n++] = 0x48; s[n++] = 0x89; s[n++] = 0xF4;
    s[n++] = 0x5E;
    s[n++] = 0x41; s[n++] = 0x5B;
    s[n++] = 0x41; s[n++] = 0x5A;
    s[n++] = 0x41; s[n++] = 0x59;
    s[n++] = 0x41; s[n++] = 0x58;
    s[n++] = 0x5A;
    s[n++] = 0x59;
    s[n++] = 0x58;
    for (i = 0; i < orig_len; ++i) s[n++] = orig[i];
    emit_jmp_abs(s, &n, (u64)back);
    used += (n + 15) & ~15;
    return s;
}

static u8* find_cave(u8* from, int need) {
    u8* text = g_base + 0x1000u;
    u8* end = text + 0x33BC2C;
    u8* p = from ? from : text;
    if (p < text) p = text;
    while (p + need <= end) {
        int run = 0;
        if (*p != 0xCC) {
            ++p;
            continue;
        }
        while (p + run < end && p[run] == 0xCC) ++run;
        if (run >= need) return p;
        p += run;
    }
    return 0;
}

static void install_fight_color(void) {
    static const u8 team_expect[5] = { 0x0F, 0xB6, 0x4C, 0x18, 0x07 };
    static const u8 dd_expect[7] = { 0x0F, 0xB6, 0x82, 0xDD, 0x00, 0x00, 0x00 };
    static const u32 dd_sites[3] = { 0x1F8173u, 0x1F8303u, 0x1F83C7u };
    u8* slots[4];
    u8* cursor = 0;
    DWORD old = 0;
    int hooked = 0;
    int i;
    for (i = 0; i < 4; ++i) {
        slots[i] = find_cave(cursor, 12);
        if (!slots[i]) {
            log_line("no enganche el color de la pelea");
            return;
        }
        cursor = slots[i] + 12;
    }
    {
        u8* site = g_base + 0x2539BCu;
        u8* stub;
        u8* slot = slots[0];
        int rel;
        int ok = 1;
        for (i = 0; i < 5; ++i) if (site[i] != team_expect[i]) ok = 0;
        stub = ok ? build_color_stub((void*)color_at_slot, 0, team_expect, 5, site + 5) : 0;
        if (stub && VirtualProtect(slot, 12, PAGE_EXECUTE_READWRITE, &old) && VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) {
            slot[0] = 0x48;
            slot[1] = 0xB8;
            write_u64(slot + 2, (u64)stub);
            slot[10] = 0xFF;
            slot[11] = 0xE0;
            rel = (int)((u64)slot - ((u64)site + 5));
            site[0] = 0xE9;
            write_i32(site + 1, rel);
            VirtualProtect(site, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), slot, 12);
            FlushInstructionCache(GetCurrentProcess(), site, 5);
            ++hooked;
        }
    }
    for (i = 0; i < 3; ++i) {
        u8* site = g_base + dd_sites[i];
        u8* slot = slots[i + 1];
        u8* stub;
        int rel;
        int n;
        int ok = 1;
        for (n = 0; n < 7; ++n) if (site[n] != dd_expect[n]) ok = 0;
        stub = ok ? build_color_stub((void*)color_on_fighter, 1, dd_expect, 7, site + 7) : 0;
        if (!stub || !VirtualProtect(slot, 12, PAGE_EXECUTE_READWRITE, &old) || !VirtualProtect(site, 7, PAGE_EXECUTE_READWRITE, &old)) continue;
        slot[0] = 0x48;
        slot[1] = 0xB8;
        write_u64(slot + 2, (u64)stub);
        slot[10] = 0xFF;
        slot[11] = 0xE0;
        rel = (int)((u64)slot - ((u64)site + 5));
        site[0] = 0xE9;
        write_i32(site + 1, rel);
        site[5] = 0x90;
        site[6] = 0x90;
        VirtualProtect(site, 7, old, &old);
        FlushInstructionCache(GetCurrentProcess(), slot, 12);
        FlushInstructionCache(GetCurrentProcess(), site, 7);
        ++hooked;
    }
    {
        static const u32 more_rva[3] = { 0x1F59C7u, 0x1F58F3u, 0x1F5AD4u };
        static const int more_mode[3] = { 3, 2, 2 };
        static const u8 more_mod[3] = { 0x88, 0x81, 0x81 };
        for (i = 0; i < 3; ++i) {
            u8* site = g_base + more_rva[i];
            u8* slot;
            u8* stub;
            u8 expect[7];
            int rel;
            int n;
            int ok = 1;
            expect[0] = 0x0F;
            expect[1] = 0xB6;
            expect[2] = more_mod[i];
            expect[3] = 0xDD;
            expect[4] = 0;
            expect[5] = 0;
            expect[6] = 0;
            for (n = 0; n < 7; ++n) if (site[n] != expect[n]) ok = 0;
            slot = find_cave(cursor, 12);
            if (!slot) break;
            cursor = slot + 12;
            stub = ok ? build_color_stub((void*)color_on_fighter, more_mode[i], expect, 7, site + 7) : 0;
            if (!stub || !VirtualProtect(slot, 12, PAGE_EXECUTE_READWRITE, &old) || !VirtualProtect(site, 7, PAGE_EXECUTE_READWRITE, &old)) continue;
            slot[0] = 0x48;
            slot[1] = 0xB8;
            write_u64(slot + 2, (u64)stub);
            slot[10] = 0xFF;
            slot[11] = 0xE0;
            rel = (int)((u64)slot - ((u64)site + 5));
            site[0] = 0xE9;
            write_i32(site + 1, rel);
            site[5] = 0x90;
            site[6] = 0x90;
            VirtualProtect(site, 7, old, &old);
            FlushInstructionCache(GetCurrentProcess(), slot, 12);
            FlushInstructionCache(GetCurrentProcess(), site, 7);
            ++hooked;
        }
    }
    {
        u8* site = g_base + 0x24556Cu;
        u8* slot;
        u8* stub;
        int rel;
        static const u8 expect[5] = { 0x0F, 0xB6, 0x54, 0x01, 0x07 };
        int ok = 1;
        for (i = 0; i < 5; ++i) if (site[i] != expect[i]) ok = 0;
        slot = find_cave(cursor, 12);
        stub = (ok && slot) ? build_edx_stub((void*)spawn_color, expect, 5, site + 5) : 0;
        if (stub && VirtualProtect(slot, 12, PAGE_EXECUTE_READWRITE, &old) && VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) {
            slot[0] = 0x48;
            slot[1] = 0xB8;
            write_u64(slot + 2, (u64)stub);
            slot[10] = 0xFF;
            slot[11] = 0xE0;
            rel = (int)((u64)slot - ((u64)site + 5));
            site[0] = 0xE9;
            write_i32(site + 1, rel);
            VirtualProtect(site, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), slot, 12);
            FlushInstructionCache(GetCurrentProcess(), site, 5);
            ++hooked;
        }
    }
    if (hooked) log_num("color de la pelea enganchado ", hooked);
    else log_line("no enganche el color de la pelea");
}

static int side_cpu(int player) {
    u8 bits;
    if (!g_base || player < 0 || player > 1) return 0;
    bits = (u8)(g_base[0x19F057Bu] & 3u);
    if (bits == 0 || bits == 3) return 0;
    return (bits & (player == 0 ? 1u : 2u)) == 0;
}

static int colors_ready(void) {
    int player, any = 0;
    if (!g_base || g_visible || g_q_n > 0) return 0;
    for (player = 0; player < 2; ++player) {
        u8* p;
        int slot, n = 0;
        if (side_cpu(player)) continue;
        p = player_ptr(player);
        if (p[3] < 3) return 0;
        for (slot = 0; slot < 3; ++slot) {
            int id = p[4 + slot];
            if (id == 0xFF || id >= CHARS) continue;
            ++n;
            if (!g_answered[player][slot]) return 0;
        }
        if (n < 3) return 0;
        any = 1;
    }
    return any;
}

static int color_busy(void) {
    int player, slot;
    if (g_visible || g_q_n > 0) return 1;
    if (!g_base) return 0;
    for (player = 0; player < 2; ++player) {
        u8* p = player_ptr(player);
        int n = 0;
        int pending = 0;
        int need = 3;
        if (side_cpu(player)) continue;
        /* p[3] can lag one frame behind the third locked ID; still block order. */
        if (p[3] > 0 && p[3] < 3) need = (int)p[3];
        if (p[3] >= 3) need = 3;
        for (slot = 0; slot < 3; ++slot) {
            int id = p[4 + slot];
            if (id == 0xFF || id >= CHARS) continue;
            ++n;
            if (!g_answered[player][slot]) pending = 1;
        }
        if (pending && n >= need && n >= 1) return 1;
        /* Three slots filled but answered incomplete — even if p[3] still < 3. */
        if (pending && n >= 3) return 1;
    }
    return 0;
}

static int team_ready(void) {
    int player, slot;
    if (g_visible || g_q_n > 0) return 0;
    for (player = 0; player < 2; ++player) {
        u8* p = player_ptr(player);
        int n = 0;
        int pending = 0;
        if (side_cpu(player)) continue;
        if (p[3] < 3) continue;
        for (slot = 0; slot < 3; ++slot) {
            int id = p[4 + slot];
            if (id == 0xFF || id >= CHARS) continue;
            ++n;
            if (!g_answered[player][slot]) pending = 1;
        }
        if (n >= 3 && !pending) return 1;
    }
    return 0;
}

static void run_order(void) {
    if (!g_order_tramp || g_in_order) return;
    g_in_order = 1;
    ((void (*)(void))g_order_tramp)();
    g_in_order = 0;
}

static void hk_order(void) {
    if (color_busy()) {
        if (!g_order_held) log_line("orden en espera");
        g_order_held = 1;
        g_order_stamp = 0;
        return;
    }
    if (g_order_held) {
        g_order_held = 0;
        g_order_stamp = 0;
        log_line("orden liberada");
    }
    run_order();
}

static void install_order_hook(void) {
    u8* fn = g_base + 0x280D50u;
    if (g_order_tramp) return;
    u8* flag;
    u8* tramp;
    DWORD old = 0;
    int disp;
    if (fn[0] != 0x48 || fn[1] != 0x83 || fn[2] != 0xEC || fn[3] != 0x38 || fn[4] != 0x8B || fn[5] != 0x05 || fn[10] != 0xA9) {
        log_line("no enganche la orden");
        return;
    }
    disp = *(int*)(fn + 6);
    flag = fn + 10 + disp;
    tramp = (u8*)VirtualAlloc(0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return;
    tramp[0] = 0x48;
    tramp[1] = 0x83;
    tramp[2] = 0xEC;
    tramp[3] = 0x38;
    tramp[4] = 0x48;
    tramp[5] = 0xB8;
    *(u64*)(tramp + 6) = (u64)flag;
    tramp[14] = 0x8B;
    tramp[15] = 0x00;
    tramp[16] = 0xA9;
    tramp[17] = fn[11];
    tramp[18] = fn[12];
    tramp[19] = fn[13];
    tramp[20] = fn[14];
    tramp[21] = 0xFF;
    tramp[22] = 0x25;
    tramp[23] = 0;
    tramp[24] = 0;
    tramp[25] = 0;
    tramp[26] = 0;
    *(u64*)(tramp + 27) = (u64)(fn + 15);
    g_order_tramp = tramp;
    if (!VirtualProtect(fn, 15, PAGE_EXECUTE_READWRITE, &old)) return;
    fn[0] = 0x48;
    fn[1] = 0xB8;
    *(u64*)(fn + 2) = (u64)&hk_order;
    fn[10] = 0xFF;
    fn[11] = 0xE0;
    fn[12] = 0x90;
    fn[13] = 0x90;
    fn[14] = 0x90;
    VirtualProtect(fn, 15, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 15);
    FlushInstructionCache(GetCurrentProcess(), tramp, 64);
    log_line("orden enganchada");
}

static void release_order(void) {
    if (!g_order_wait || !g_base || color_busy()) return;
    *(u16*)(g_base + 0x19EB3B8u) = 0x20;
    *(u16*)(g_base + 0x19EB5B8u) = 0x20;
    g_order_wait = 0;
    log_line("orden lista");
}

static int order_gate(void) {
    if (!color_busy()) return 1;
    if (!g_order_wait) log_line("orden en espera");
    g_order_wait = 1;
    return 0;
}

static void install_order_gate(void) {
    u8* site = g_base + 0xBC81Fu;
    u8* slot;
    u8* s;
    u8* stub;
    int n = 0;
    int rel;
    int jl_at;
    DWORD old = 0;
    static const u8 expect[5] = { 0xB8, 0x20, 0x00, 0x00, 0x00 };
    int i;
    for (i = 0; i < 5; ++i) if (site[i] != expect[i]) {
        log_line("no enganche la salida a orden");
        return;
    }
    slot = find_cave(0, 12);
    stub = (u8*)VirtualAlloc(0, 256, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!slot || !stub) return;
    s = stub;
    s[n++] = 0x51;
    s[n++] = 0x52;
    s[n++] = 0x41; s[n++] = 0x50;
    s[n++] = 0x41; s[n++] = 0x51;
    s[n++] = 0x41; s[n++] = 0x52;
    s[n++] = 0x41; s[n++] = 0x53;
    s[n++] = 0x56;
    s[n++] = 0x48; s[n++] = 0x89; s[n++] = 0xE6;
    s[n++] = 0x48; s[n++] = 0x83; s[n++] = 0xE4; s[n++] = 0xF0;
    s[n++] = 0x48; s[n++] = 0x83; s[n++] = 0xEC; s[n++] = 0x20;
    emit_abs(s, &n, 0x48, 0xB8, (u64)order_gate);
    s[n++] = 0xFF; s[n++] = 0xD0;
    s[n++] = 0x48; s[n++] = 0x89; s[n++] = 0xF4;
    s[n++] = 0x5E;
    s[n++] = 0x41; s[n++] = 0x5B;
    s[n++] = 0x41; s[n++] = 0x5A;
    s[n++] = 0x41; s[n++] = 0x59;
    s[n++] = 0x41; s[n++] = 0x58;
    s[n++] = 0x5A;
    s[n++] = 0x59;
    s[n++] = 0x85; s[n++] = 0xC0;
    s[n++] = 0x74;
    jl_at = n;
    s[n++] = 0x00;
    s[n++] = 0xB8; s[n++] = 0x20; s[n++] = 0x00; s[n++] = 0x00; s[n++] = 0x00;
    emit_jmp_abs(s, &n, (u64)(site + 5));
    s[jl_at] = (u8)(n - (jl_at + 1));
    emit_jmp_abs(s, &n, (u64)(site + 19));
    if (!VirtualProtect(slot, 12, PAGE_EXECUTE_READWRITE, &old)) return;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return;
    slot[0] = 0x48;
    slot[1] = 0xB8;
    write_u64(slot + 2, (u64)stub);
    slot[10] = 0xFF;
    slot[11] = 0xE0;
    rel = (int)((u64)slot - ((u64)site + 5));
    site[0] = 0xE9;
    write_i32(site + 1, rel);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), slot, 12);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    FlushInstructionCache(GetCurrentProcess(), stub, 256);
    log_line("salida a orden enganchada");
}

static void order_pump(void) {
    u64 now;
    release_order();
    if (!g_order_held || !g_order_tramp || g_in_order) return;
    if (color_busy()) {
        g_order_stamp = 0;
        return;
    }
    if (!team_ready()) {
        g_order_held = 0;
        g_order_stamp = 0;
        return;
    }
    now = GetTickCount64();
    if (!g_order_stamp) g_order_stamp = now;
    if (now - g_order_stamp < 400) return;
    g_order_held = 0;
    g_order_stamp = 0;
    log_line("orden forzada");
    run_order();
}

static const u8* palette_bytes(int id, int color) {
    if (!g_pal_ok || id < 0 || id >= PAL_CHARS || color < 0 || color >= BASE_COLORS) return 0;
    return g_pal + ((color * PAL_CHARS + id) * PAL_BYTES);
}

static DWORD rgb555(u16 c) {
    u32 r = (c & 31u) * 255u / 31u;
    u32 g = ((c >> 5) & 31u) * 255u / 31u;
    u32 b = ((c >> 10) & 31u) * 255u / 31u;
    return r | (g << 8) | (b << 16);
}

static int row_score(const u8* pal, int row) {
    int i, score = 0;
    for (i = 0; i < COLS; ++i) {
        u16 c = *(const u16*)(pal + (row * COLS + i) * 2);
        int r = c & 31;
        int g = (c >> 5) & 31;
        int b = (c >> 10) & 31;
        int mx = r;
        int mn = r;
        if (g > mx) mx = g;
        if (b > mx) mx = b;
        if (g < mn) mn = g;
        if (b < mn) mn = b;
        score += (mx - mn) + mx / 4;
    }
    return score;
}

static void pick_rows(int id) {
    int color, row, n;
    for (color = 0; color < BASE_COLORS; ++color) {
        const u8* pal = palette_bytes(id, color);
        int best[ROWS];
        int used[16];
        for (n = 0; n < 16; ++n) used[n] = 0;
        for (n = 0; n < ROWS; ++n) best[n] = n + 2;
        if (pal) {
            for (n = 0; n < ROWS; ++n) {
                int pick = 2;
                int score = -1;
                for (row = 1; row < 16; ++row) {
                    int s;
                    if (used[row]) continue;
                    s = row_score(pal, row);
                    if (s > score) {
                        score = s;
                        pick = row;
                    }
                }
                used[pick] = 1;
                best[n] = pick;
            }
            for (n = 0; n < ROWS; ++n) {
                int j;
                for (j = n + 1; j < ROWS; ++j) {
                    if (best[j] < best[n]) {
                        int t = best[n];
                        best[n] = best[j];
                        best[j] = t;
                    }
                }
            }
        }
        for (n = 0; n < ROWS; ++n) g_rows[color][n] = best[n];
    }
}

static void latch_set(int player, int slot, int id, int color);

static void write_color(int player, int slot, int color) {
    u8* p;
    int id;
    if (player < 0 || player > 1 || slot < 0 || slot > 2) return;
    if (color < 0) color = 0;
    if (color > 7) color = 7;
    p = player_ptr(player);
    p[7 + slot] = (u8)color;
    id = p[4 + slot];
    if (id >= 0 && id < CHARS) latch_set(player, slot, id, color);
}

static void force_rebuild(void) {
    u8 next[CHARS];
    int i, id;
    for (id = 0; id < CHARS; ++id) next[id] = 0xFF;
    for (i = 0; i < 6; ++i) {
        int idv, c;
        if (!g_latch[i].on) continue;
        idv = g_latch[i].id;
        c = g_latch[i].color & 7;
        if (idv < 0 || idv >= CHARS) continue;
        if (next[idv] == 0xFF) next[idv] = (u8)c;
        else if (next[idv] != (u8)c) next[idv] = 0xFE;
    }
    for (id = 0; id < CHARS; ++id) {
        if (next[id] == 0xFE) next[id] = 0xFF;
        g_force[id] = next[id];
    }
}

static void latch_set(int player, int slot, int id, int color) {
    int i;
    for (i = 0; i < 6; ++i) {
        if (g_latch[i].on && g_latch[i].player == player && g_latch[i].slot == slot) {
            g_latch[i].id = id;
            g_latch[i].color = color;
            force_rebuild();
            return;
        }
    }
    for (i = 0; i < 6; ++i) {
        if (!g_latch[i].on) {
            g_latch[i].player = player;
            g_latch[i].slot = slot;
            g_latch[i].id = id;
            g_latch[i].color = color;
            g_latch[i].on = 1;
            force_rebuild();
            return;
        }
    }
}

static int latch_has(int player, int slot, int id) {
    int i;
    for (i = 0; i < 6; ++i)
        if (g_latch[i].on && g_latch[i].player == player && g_latch[i].slot == slot && g_latch[i].id == id)
            return 1;
    return 0;
}

static void latch_clear_slot(int player, int slot) {
    int i;
    for (i = 0; i < 6; ++i)
        if (g_latch[i].on && g_latch[i].player == player && g_latch[i].slot == slot)
            g_latch[i].on = 0;
}

static void latch_clear_player(int player) {
    int i;
    for (i = 0; i < 6; ++i)
        if (g_latch[i].on && g_latch[i].player == player)
            g_latch[i].on = 0;
}

static void paint_struct(u8* p) {
    int slot, i;
    int wrote[3];
    wrote[0] = wrote[1] = wrote[2] = 0;
    for (slot = 0; slot < 3; ++slot) {
        for (i = 0; i < 6; ++i) {
            if (!g_latch[i].on || g_latch[i].slot != slot) continue;
            if (p[4 + slot] != (u8)g_latch[i].id) continue;
            p[7 + slot] = (u8)(g_latch[i].color & 7);
            wrote[slot] = 1;
        }
    }
    for (i = 0; i < 6; ++i) {
        int hits = 0;
        int at = -1;
        if (!g_latch[i].on) continue;
        for (slot = 0; slot < 3; ++slot) {
            if (p[4 + slot] == (u8)g_latch[i].id) {
                ++hits;
                at = slot;
            }
        }
        if (hits == 1 && at >= 0 && !wrote[at]) p[7 + at] = (u8)(g_latch[i].color & 7);
    }
}

static void remember_cursor(int id, int color) {
    u8* c;
    if (!g_base || id < 0 || id >= CHARS) return;
    c = *(u8**)(g_base + 0x779978u);
    if ((u64)c < 0x10000ull) return;
    if (c[0xD6] != (u8)id) return;
    c[0xD8] = (u8)(color & 7);
}

static void latch_apply(void) {
    if (!g_base) return;
    paint_struct(player_ptr(0));
    paint_struct(player_ptr(1));
}

static const char* char_name(int id) {
    if (id >= 0 && id < CHARS) return g_names[id];
    return "Personaje";
}

static HWND find_game_window(void) {
    HWND hwnd = 0;
    DWORD pid = GetCurrentProcessId();
    while ((hwnd = FindWindowExW(0, hwnd, 0, 0)) != 0) {
        DWORD wpid = 0;
        if (!IsWindowVisible(hwnd)) continue;
        GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid == pid && GetWindow(hwnd, GW_OWNER) == 0) return hwnd;
    }
    return 0;
}

static void hide_window(void) {
    int i;
    for (i = 0; i < 2; ++i) {
        if (g_pk[i].hwnd == g_hwnd) g_pk[i].on = 0;
    }
    g_visible = g_pk[0].on || g_pk[1].on;
    g_pad_prev = 0;
    g_swallow_until = GetTickCount64() + 700;
    if (g_hwnd) ShowWindow(g_hwnd, SW_HIDE);
}

static int read_bin(const wchar_t* path, u8* dst, int cap) {
    HANDLE f;
    DWORD got = 0;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE) return 0;
    ReadFile(f, dst, (DWORD)cap, &got, 0);
    CloseHandle(f);
    return (int)got;
}

static u32 rd32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int grc_decode(const u8* in, int in_n, u8* out, int out_cap) {
    int i = 0;
    int o = 0;
    while (i < in_n) {
        u8 c = in[i++];
        if (c < 0xC0) {
            if (o >= out_cap) return -1;
            out[o++] = c;
        } else if (c == 0xC0) {
            break;
        } else if (c == 0xC1) {
            if (i >= in_n || o >= out_cap) return -1;
            out[o++] = in[i++];
        } else {
            int cnt = (int)c - 0xC0;
            int back;
            if (i >= in_n) return -1;
            back = in[i++];
            if (back <= 0 || back > o) return -1;
            while (cnt-- > 0) {
                if (o >= out_cap) return -1;
                out[o] = out[o - back];
                ++o;
            }
        }
    }
    return o;
}

static void sprite_path(wchar_t* path, int id) {
    int n;
    const wchar_t* hex = L"0123456789abcdef";
    const wchar_t* tail = L"Data\\game\\obj\\p";
    int i = 0;
    GetModuleFileNameW(0, path, MAX_PATH);
    n = 0;
    while (path[n]) ++n;
    while (n > 0 && path[n - 1] != L'\\') --n;
    while (tail[i] && n + 8 < MAX_PATH) path[n++] = tail[i++];
    path[n++] = hex[(id >> 4) & 15];
    path[n++] = hex[id & 15];
    path[n++] = L'.';
    path[n++] = L'g';
    path[n++] = L'r';
    path[n++] = L'c';
    path[n] = 0;
}

static void blit_tile(const u8* dec, int s3, int tile, int dx, int dy) {
    int ty, tx;
    const u8* src;
    if (tile <= 0) return;
    src = dec + s3 + tile * 128;
    for (ty = 0; ty < 16; ++ty) {
        for (tx = 0; tx < 16; ++tx) {
            int x = dx + tx;
            int y = dy + ty;
            u8 v, n;
            if ((unsigned)x >= SPR_W || (unsigned)y >= SPR_H) continue;
            v = src[ty * 8 + (tx >> 1)];
            n = (tx & 1) ? (u8)(v >> 4) : (u8)(v & 15);
            if (n) g_pix[y * SPR_W + x] = n;
        }
    }
}

static void paint_cells(const u8* meta, const u8* rec, int rec_off, int sw, int sh, const u8* dec, int s3, int mask_words) {
    int col, row;
    const u8* p = rec;
    int off = rec_off;
    for (col = 0; col < sw; ++col) {
        unsigned bits = mask_words ? (unsigned)(meta[4 + col * 2] | (meta[5 + col * 2] << 8)) : meta[4 + col];
        for (row = 0; row < sh; ++row) {
            if (bits & 0x8000u || (!mask_words && (bits & 0x80u))) {
                int odd = off & 1;
                int tile = (p[odd] | (p[odd + 1] << 8)) & 0xFFF;
                blit_tile(dec, s3, tile, col * 16, row * 16);
                p += 3;
                off += 3;
            }
            bits <<= 1;
        }
    }
}

static int ensure_arrange(void) {
    wchar_t path[MAX_PATH];
    u8* in;
    int n, i, got, dec;
    const wchar_t* tail = L"Data\\game\\arrange.grc";
    if (g_arr) return g_arr_base > 0;
    in = (u8*)VirtualAlloc(0, 5 * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, 4);
    g_arr = (u8*)VirtualAlloc(0, 8 * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, 4);
    if (!in || !g_arr) return 0;
    GetModuleFileNameW(0, path, MAX_PATH);
    n = 0;
    while (path[n]) ++n;
    while (n > 0 && path[n - 1] != L'\\') --n;
    i = 0;
    while (tail[i] && n + 1 < MAX_PATH) path[n++] = tail[i++];
    path[n] = 0;
    got = read_bin(path, in, 5 * 1024 * 1024);
    if (got < 64) return 0;
    dec = grc_decode(in, got, g_arr, 8 * 1024 * 1024);
    if (dec < 64) return 0;
    g_arr_n = dec;
    g_arr_base = (int)rd32(g_arr + 20);
    if (g_arr_base <= 0 || g_arr_base + 32 >= dec) {
        g_arr_base = 0;
        return 0;
    }
    log_line("paletas de los 8 colores");
    return 1;
}

static void put_pal(int color, const u8* src) {
    int i;
    g_costume[color][0] = 0;
    for (i = 1; i < 16; ++i) {
        unsigned c = (unsigned)src[i * 2] | ((unsigned)src[i * 2 + 1] << 8);
        unsigned r = (c & 31) * 255 / 31;
        unsigned g = ((c >> 5) & 31) * 255 / 31;
        unsigned b = ((c >> 10) & 31) * 255 / 31;
        g_costume[color][i] = r | (g << 8) | (b << 16);
    }
    for (i = 16; i < 256; ++i) g_costume[color][i] = 0;
}

static void load_fight(int id) {
    wchar_t path[MAX_PATH];
    int got, dec_n, s2, s3, off0, sw, sh, type, x, y, color;
    const u8* meta;
    const u8* rec;
    int rec_off;
    g_spr_ok = 0;
    if (id < 0 || id >= CHARS || !g_pal_ok) return;
    if (!g_grc_in) g_grc_in = (u8*)VirtualAlloc(0, 2 * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, 4);
    if (!g_grc_out) g_grc_out = (u8*)VirtualAlloc(0, 8 * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, 4);
    if (!g_grc_in || !g_grc_out) return;
    sprite_path(path, id);
    got = read_bin(path, g_grc_in, 2 * 1024 * 1024);
    if (got < 16) return;
    dec_n = grc_decode(g_grc_in, got, g_grc_out, 8 * 1024 * 1024);
    if (dec_n < 0x40) return;
    s2 = (int)rd32(g_grc_out + 8);
    s3 = (int)rd32(g_grc_out + 12);
    if (s2 <= 0 || s3 <= s2 || s3 >= dec_n) return;
    off0 = (int)rd32(g_grc_out + s2);
    if (off0 < 4 || s2 + off0 + 8 >= s3) return;
    meta = g_grc_out + s2 + off0;
    type = meta[1] & 15;
    sw = meta[2];
    sh = meta[3];
    if (sw < 1 || sh < 1 || sw > 16 || sh > 16) return;
    memset(g_pix, 0, SPR_W * SPR_H);
    if (type == 6 && sh <= 8) {
        rec = meta + 4 + sw + (sw & 1);
        rec_off = (int)(rec - g_grc_out);
        paint_cells(meta, rec, rec_off, sw, sh, g_grc_out, s3, 0);
    } else if (type == 5 && sh <= 16) {
        rec = meta + 4 + sw * 2;
        rec_off = (int)(rec - g_grc_out);
        paint_cells(meta, rec, rec_off, sw, sh, g_grc_out, s3, 1);
    } else {
        return;
    }
    {
        int arranged = ensure_arrange();
        const u16* tab = arranged ? (const u16*)(g_base + 0x390100u) : 0;
        for (color = 0; color < COLOR_COUNT; ++color) {
            const u8* src = 0;
            if (tab) {
                unsigned index = tab[id * 8 + color];
                int off = g_arr_base + (int)index * 32;
                if (off >= 0 && off + 32 <= g_arr_n) src = g_arr + off;
            }
            if (!src && id < PAL_CHARS) src = g_pal + (((color & 3) * PAL_CHARS + id) * PAL_BYTES);
            if (src) put_pal(color, src);
        }
    }
    g_bg = 0;
    g_x0 = SPR_W;
    g_y0 = SPR_H;
    g_x1 = 0;
    g_y1 = 0;
    for (y = 0; y < SPR_H; ++y) {
        for (x = 0; x < SPR_W; ++x) {
            if (!g_pix[y * SPR_W + x]) continue;
            if (x < g_x0) g_x0 = x;
            if (y < g_y0) g_y0 = y;
            if (x > g_x1) g_x1 = x;
            if (y > g_y1) g_y1 = y;
        }
    }
    if (g_x1 < g_x0) return;
    g_spr_ok = 1;
    log_line("sprite de pelea");
}

static void draw_sprite(HDC dc, int dx, int dy, int dw, int dh, int color) {
    u8 info[40];
    int sw, sh, y, x;
    if (!g_spr_ok || dw < 4 || dh < 4 || dw > 150 || dh > 170) return;
    sw = g_x1 - g_x0 + 1;
    sh = g_y1 - g_y0 + 1;
    if (sw < 1 || sh < 1) return;
    for (y = 0; y < dh; ++y) {
        int sy = g_y0 + y * sh / dh;
        for (x = 0; x < dw; ++x) {
            int sx = g_x0 + x * sw / dw;
            u8 ix = g_pix[sy * SPR_W + sx];
            DWORD col = 0;
            int o = (y * dw + x) * 3;
            if (ix != 0 && ix != (u8)g_bg) col = g_costume[color][ix];
            g_scale[o] = (u8)col;
            g_scale[o + 1] = (u8)(col >> 8);
            g_scale[o + 2] = (u8)(col >> 16);
        }
    }
    memset(info, 0, 40);
    info[0] = 40;
    info[4] = (u8)dw;
    info[5] = (u8)(dw >> 8);
    info[8] = (u8)(-dh);
    info[9] = (u8)((-dh) >> 8);
    info[10] = (u8)((-dh) >> 16);
    info[11] = (u8)((-dh) >> 24);
    info[12] = 1;
    info[14] = 24;
    StretchDIBits(dc, dx, dy, dw, dh, 0, 0, dw, dh, g_scale, info, 0, SRCCOPY);
}

static void show_for(int player, int slot, int id);

static void show_next(void) {
    int i = 0;
    while (i < g_q_n) {
        int p = g_q_p[i];
        int s = g_q_s[i];
        int id = g_q_id[i];
        int k;
        if (p >= 0 && p <= 1 && g_pk[p].on) {
            ++i;
            continue;
        }
        --g_q_n;
        for (k = i; k < g_q_n; ++k) {
            g_q_p[k] = g_q_p[k + 1];
            g_q_s[k] = g_q_s[k + 1];
            g_q_id[k] = g_q_id[k + 1];
        }
        if (side_cpu(p)) {
            if (p >= 0 && p < 2 && s >= 0 && s < 3) g_answered[p][s] = 1;
            continue;
        }
        show_for(p, s, id);
    }
}

static int color_taken(int player, int id, int color) {
    int i;
    u8* other;
    color &= 7;
    if (player < 0 || player > 1 || id < 0 || id >= CHARS) return 0;
    for (i = 0; i < 6; ++i) {
        if (!g_latch[i].on || g_latch[i].id != id || g_latch[i].player == player) continue;
        if ((g_latch[i].color & 7) == color) return 1;
    }
    if (g_owned[player ^ 1][id] == (u8)color) return 1;
    other = player_ptr(player ^ 1);
    for (i = 0; i < 3; ++i) {
        if (other[4 + i] != (u8)id) continue;
        if ((other[7 + i] & 7) == color) return 1;
    }
    return 0;
}

static void recolor_cpu(int player);

/* Prefer keeping the chosen color; if the other side already has it on the
   same character, give A, else B, else the first free (A..D / pairs). */
static int settle_color(int player, int id, int color) {
    int c;
    char msg[96];
    static const int prefer[8] = { 0, 1, 2, 3, 5, 4, 6, 7 }; /* A B C D A+B C+D … */
    color &= 7;
    if (!color_taken(player, id, color)) return color;
    for (c = 0; c < 8; ++c) {
        int try_c = prefer[c];
        if (!color_taken(player, id, try_c)) {
            copy_str(msg, 96, "color repetido ");
            cat_str(msg, 96, char_name(id));
            cat_str(msg, 96, player ? " P2 -> " : " P1 -> ");
            cat_str(msg, 96, g_color_name[try_c]);
            log_line(msg);
            return try_c;
        }
    }
    return color;
}

/* Single-fighter modes fill 3 slots with the same ID — one UI, copy to all. */
static void propagate_same_id(int player, int id, int color) {
    u8* p;
    int slot, i, w;
    if (player < 0 || player > 1 || id < 0 || id >= CHARS) return;
    color &= 7;
    p = player_ptr(player);
    for (slot = 0; slot < 3; ++slot) {
        if (p[4 + slot] != (u8)id) continue;
        p[7 + slot] = (u8)color;
        latch_set(player, slot, id, color);
        g_answered[player][slot] = 1;
    }
    g_owned[player][id] = (u8)color;
    /* Drop queued prompts for this same character. */
    w = 0;
    for (i = 0; i < g_q_n; ++i) {
        if (g_q_p[i] == player && g_q_id[i] == id) continue;
        if (w != i) {
            g_q_p[w] = g_q_p[i];
            g_q_s[w] = g_q_s[i];
            g_q_id[w] = g_q_id[i];
        }
        ++w;
    }
    g_q_n = w;
}

static void enforce_mirror_unique(int player, int id, int color) {
    int other = player ^ 1;
    u8* p;
    int slot;
    if (side_cpu(other)) {
        recolor_cpu(other);
        return;
    }
    p = player_ptr(other);
    for (slot = 0; slot < 3; ++slot) {
        int c;
        if (p[4 + slot] != (u8)id) continue;
        c = p[7 + slot] & 7;
        if (c != (color & 7) && !color_taken(other, id, c)) continue;
        c = settle_color(other, id, c);
        propagate_same_id(other, id, c);
    }
}

static void recolor_cpu(int player) {
    u8* p;
    int slot;
    if (!side_cpu(player)) return;
    p = player_ptr(player);
    for (slot = 0; slot < 3; ++slot) {
        int id = p[4 + slot];
        int color;
        if (id == 0xFF || id >= CHARS) continue;
        color = settle_color(player, id, p[7 + slot] & 7);
        p[7 + slot] = (u8)color;
        latch_set(player, slot, id, color);
        g_owned[player][id] = (u8)color;
    }
}

static void confirm_choice(void) {
    char msg[96];
    int color = settle_color(g_player, g_id, g_choice & 7);
    g_choice = color;
    remember_cursor(g_id, g_choice);
    propagate_same_id(g_player, g_id, g_choice);
    enforce_mirror_unique(g_player, g_id, g_choice);
    copy_str(msg, 96, "elegido ");
    if (g_choice >= 0 && g_choice < COLOR_COUNT) cat_str(msg, 96, g_color_name[g_choice]);
    cat_str(msg, 96, " celda ");
    cat_int(msg, 96, pos_of(g_choice));
    cat_str(msg, 96, " raw ");
    cat_int(msg, 96, (int)g_abcd_prev);
    log_line(msg);
    hide_window();
    recolor_cpu(0);
    recolor_cpu(1);
    show_next();
}

static void cancel_choice(void) {
    write_color(g_player, g_slot, g_original);
    if (g_player >= 0 && g_player < 2 && g_slot >= 0 && g_slot < 3) g_answered[g_player][g_slot] = 1;
    hide_window();
    show_next();
}

static int load_sheet(int id) {
    wchar_t path[MAX_PATH];
    int n, i, got, w, h, off, bpp, stride;
    const wchar_t* tail = L"colors\\";
    g_sheet_ok = 0;
    if (id < 0 || id >= CHARS) return 0;
    GetModuleFileNameW(g_self, path, MAX_PATH);
    n = 0;
    while (path[n]) ++n;
    while (n > 0 && path[n - 1] != L'\\') --n;
    i = 0;
    while (tail[i] && n + 8 < MAX_PATH) path[n++] = tail[i++];
    path[n++] = (wchar_t)(L'0' + id / 10);
    path[n++] = (wchar_t)(L'0' + id % 10);
    path[n++] = L'.';
    path[n++] = L'b';
    path[n++] = L'm';
    path[n++] = L'p';
    path[n] = 0;
    if (!g_sheet_file) g_sheet_file = (u8*)VirtualAlloc(0, 2 * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, 4);
    if (!g_sheet_file) return 0;
    got = read_bin(path, g_sheet_file, 2 * 1024 * 1024);
    if (got < 54 || g_sheet_file[0] != 'B' || g_sheet_file[1] != 'M') return 0;
    off = (int)rd32(g_sheet_file + 10);
    w = (int)rd32(g_sheet_file + 18);
    h = (int)rd32(g_sheet_file + 22);
    bpp = g_sheet_file[28] | (g_sheet_file[29] << 8);
    if (bpp != 24 || w < 16 || h < 16 || w > 800 || h > 400) return 0;
    stride = (w * 3 + 3) & ~3;
    if (off < 54 || off + stride * h > got) return 0;
    /* Pixels stay in the file: bottom-up, each row padded to 4 bytes.
       Copying them tight (w*3) sheared the colors into grey. */
    g_sheet = g_sheet_file + off;
    g_sheet_w = w;
    g_sheet_h = h;
    g_sheet_ok = 1;
    return 1;
}

static void take_sheet(Pick* pk) {
    int h, stride, n;
    pk->sheet_ok = 0;
    pk->pix = pk->pix;
    if (!g_sheet_ok || !g_sheet || g_sheet_w < 16) return;
    h = g_sheet_h < 0 ? -g_sheet_h : g_sheet_h;
    stride = (g_sheet_w * 3 + 3) & ~3;
    n = stride * h;
    if (h < 16 || n <= 0 || n > 2 * 1024 * 1024) return;
    if (!pk->pix) pk->pix = (u8*)VirtualAlloc(0, 2 * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, 4);
    if (!pk->pix) return;
    memcpy(pk->pix, g_sheet, (unsigned long long)n);
    pk->sheet_w = g_sheet_w;
    pk->sheet_h = g_sheet_h;
    pk->sheet_ok = 1;
}

static void show_for(int player, int slot, int id) {
    (void)id;
    /* No A/B overlay. Palettes still apply in-game; this window was grey and blocked select. */
    if (player >= 0 && player < 2 && slot >= 0 && slot < 3)
        g_answered[player][slot] = 1;
}

static void queue_color(int player, int slot, int id) {
    int i, s;
    if (g_q_n >= 6) return;
    if (player >= 0 && player <= 1 && g_pk[player].on && g_pk[player].slot == slot) return;
    /* Already colored this character (single-fighter fills 3 slots) — copy, no UI. */
    for (s = 0; s < 3; ++s) {
        if (!latch_has(player, s, id)) continue;
        for (i = 0; i < 6; ++i) {
            if (!g_latch[i].on || g_latch[i].player != player || g_latch[i].slot != s) continue;
            if (g_latch[i].id != id) continue;
            propagate_same_id(player, id, g_latch[i].color & 7);
            return;
        }
    }
    for (i = 0; i < g_q_n; ++i) {
        if (g_q_p[i] == player && g_q_s[i] == slot) return;
        /* One prompt per character id (not per cloned slot). */
        if (g_q_p[i] == player && g_q_id[i] == id) return;
    }
    g_q_p[g_q_n] = player;
    g_q_s[g_q_n] = slot;
    g_q_id[g_q_n] = id;
    ++g_q_n;
    show_next();
}

static void watch_select(void) {
    int player, slot;
    int scene = scene_now();
    if (scene != g_scene_log) {
        g_scene_log = scene;
        log_num("escena ", scene);
    }
    for (player = 0; player < 2; ++player) {
        u8* p = player_ptr(player);
        if (p[4] == 0xFF && p[5] == 0xFF && p[6] == 0xFF) {
            if (g_prev[player][0] != 0xFF || g_prev[player][1] != 0xFF || g_prev[player][2] != 0xFF)
                latch_clear_player(player);
            g_cpu_logged[player] = 0;
        }
        for (slot = 0; slot < 3; ++slot) {
            int id = p[4 + slot];
            int prev = g_prev[player][slot];
            if (id == 0xFF) {
                if (prev >= 0 && prev < CHARS) g_owned[player][prev] = 0xFF;
                if (prev != 0xFF) latch_clear_slot(player, slot);
                g_prev[player][slot] = 0xFF;
                g_answered[player][slot] = 0;
                continue;
            }
            if (prev < 0) {
                g_prev[player][slot] = id;
                continue;
            }
            if (prev != id) {
                g_answered[player][slot] = 0;
                latch_clear_slot(player, slot);
                if (prev >= 0 && prev < CHARS) g_owned[player][prev] = 0xFF;
            }
            /* Same character already colored (single mode clones 3 slots) — copy. */
            if (id < CHARS) {
                int s, copied = 0;
                for (s = 0; s < 3 && !copied; ++s) {
                    int i;
                    if (!latch_has(player, s, id)) continue;
                    for (i = 0; i < 6; ++i) {
                        if (!g_latch[i].on || g_latch[i].player != player || g_latch[i].slot != s) continue;
                        if (g_latch[i].id != id) continue;
                        propagate_same_id(player, id, g_latch[i].color & 7);
                        copied = 1;
                        break;
                    }
                }
                if (copied) {
                    g_prev[player][slot] = id;
                    continue;
                }
            }
            if (id < CHARS && !g_answered[player][slot] && (prev == 0xFF || prev != id || g_order_held)) {
                if (side_cpu(player)) {
                    g_answered[player][slot] = 1;
                    recolor_cpu(player);
                    if (!g_cpu_logged[player]) {
                        char msg[48];
                        g_cpu_logged[player] = 1;
                        copy_str(msg, 48, player ? "pc P2 sin ventana " : "pc P1 sin ventana ");
                        cat_int(msg, 48, g_base[0x19F057Bu] & 3);
                        log_line(msg);
                    }
                } else {
                    queue_color(player, slot, id);
                }
            }
            g_prev[player][slot] = id;
        }
    }
    if (colors_ready()) latch_apply();
}

static void load_pal(void) {
    wchar_t path[MAX_PATH];
    HANDLE f;
    DWORD got = 0;
    int n;
    g_pal_ok = 0;
    GetModuleFileNameW(0, path, MAX_PATH);
    n = 0;
    while (path[n]) ++n;
    while (n > 0 && path[n - 1] != L'\\') --n;
    path[n] = 0;
    {
        const wch* tail = L"Data\\game\\def.pal";
        int i = 0;
        while (tail[i] && n + 1 < MAX_PATH) path[n++] = tail[i++];
        path[n] = 0;
    }
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE) {
        log_line("no abri def.pal");
        return;
    }
    ReadFile(f, g_pal, PAL_FILE, &got, 0);
    CloseHandle(f);
    g_pal_ok = got == PAL_FILE;
    log_num("paleta ", (int)got);
}

static void swatch_color(int color, int row, int col, DWORD* out) {
    static const int pair[COLOR_COUNT][2] = {
        { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 },
        { 2, 3 }, { 0, 1 }, { 0, 3 }, { 1, 2 }
    };
    int srcA = pair[color][0];
    int srcB = pair[color][1];
    int src = (srcA == srcB || col < 8) ? srcA : srcB;
    int index = col;
    const u8* pal;
    if (srcA != srcB) index = col < 8 ? col : col - 8;
    pal = palette_bytes(g_id, src);
    if (!pal) {
        *out = 0x00201818u;
        return;
    }
    *out = rgb555(*(const u16*)(pal + (g_rows[src][row] * COLS + index) * 2));
}

static void paint_marks(HDC dc, int color, int cx, int bottom, int selected) {
    static const DWORD ink[4] = { 0x00FF00FFu, 0x0000FFFFu, 0x0000FF00u, 0x00FFFF00u };
    static const int pair[COLOR_COUNT][2] = {
        { 0, -1 }, { 1, -1 }, { 2, -1 }, { 3, -1 },
        { 2, 3 }, { 0, 1 }, { 0, 3 }, { 1, 2 }
    };
    static const char* letter[4] = { "A", "B", "C", "D" };
    int n = pair[color][1] < 0 ? 1 : 2;
    int d = 22;
    int gap = 4;
    int total = n * d + (n - 1) * gap;
    int x = cx - total / 2;
    int i;
    for (i = 0; i < n; ++i) {
        RECT_ rc;
        int k = pair[color][i];
        HBRUSH br = CreateSolidBrush(ink[k]);
        HGDIOBJ pen = CreatePen(0, selected ? 2 : 1, selected ? 0x00FFFFFFu : 0x00000000u);
        HGDIOBJ oldb = SelectObject(dc, br);
        HGDIOBJ oldp = SelectObject(dc, pen);
        Ellipse(dc, x, bottom - d, x + d, bottom);
        SelectObject(dc, oldb);
        SelectObject(dc, oldp);
        DeleteObject(br);
        DeleteObject(pen);
        rc.left = x;
        rc.top = bottom - d;
        rc.right = x + d;
        rc.bottom = bottom;
        SetTextColor(dc, 0x00000000u);
        DrawTextA(dc, letter[k], 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += d + gap;
    }
}

static void paint(HWND hwnd) {
    PAINT_ ps;
    RECT_ rc;
    HDC dc = BeginPaint(hwnd, &ps);
    Pick* pk = g_pk[0].hwnd == hwnd ? &g_pk[0] : (g_pk[1].hwnd == hwnd ? &g_pk[1] : 0);
    HDC mem;
    HBITMAP bmp;
    HGDIOBJ old, old_font;
    HFONT font;
    HBRUSH bg;
    int pos;
    GetClientRect(hwnd, &rc);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    old = SelectObject(mem, bmp);
    font = CreateFontW(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    old_font = SelectObject(mem, font);
    SetBkMode(mem, TRANSPARENT);
    bg = CreateSolidBrush(0);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    SetStretchBltMode(mem, 3);
    if (pk && pk->on) {
        g_choice = pk->choice;
        g_id = pk->id;
        if (pk->sheet_ok && pk->pix) {
            g_sheet = pk->pix;
            g_sheet_w = pk->sheet_w;
            g_sheet_h = pk->sheet_h;
            g_sheet_ok = 1;
        }
    }
    if (g_sheet_ok && g_sheet_w > 0 && g_sheet_h > 0) {
        u8 info[40];
        int pos = 0;
        int col, row, gx, gy, cw, ch;
        HBRUSH hi;
        RECT_ edge;
        memset(info, 0, 40);
        info[0] = 40;
        info[4] = (u8)g_sheet_w;
        info[5] = (u8)(g_sheet_w >> 8);
        info[8] = (u8)g_sheet_h;
        info[9] = (u8)(g_sheet_h >> 8);
        info[12] = 1;
        info[14] = 24;
        StretchDIBits(mem, 0, 0, rc.right, rc.bottom, 0, 0, g_sheet_w, g_sheet_h < 0 ? -g_sheet_h : g_sheet_h, g_sheet, info, 0, SRCCOPY);
        for (pos = 0; pos < COLOR_COUNT; ++pos) if (g_show[pos] == g_choice) break;
        col = pos % GRID_COLS;
        row = pos / GRID_COLS;
        cw = rc.right / GRID_COLS;
        ch = rc.bottom / GRID_ROWS;
        gx = col * cw;
        gy = row * ch;
        hi = CreateSolidBrush(0x0000FFFFu);
        edge.left = gx; edge.top = gy; edge.right = gx + cw; edge.bottom = gy + 3;
        FillRect(mem, &edge, hi);
        edge.top = gy + ch - 3; edge.bottom = gy + ch;
        FillRect(mem, &edge, hi);
        edge.top = gy; edge.bottom = gy + ch; edge.right = gx + 3;
        FillRect(mem, &edge, hi);
        edge.left = gx + cw - 3; edge.right = gx + cw;
        FillRect(mem, &edge, hi);
        DeleteObject(hi);
    } else for (pos = 0; pos < COLOR_COUNT; ++pos) {
        int color = g_show[pos];
        int gx = PAD + (pos % GRID_COLS) * (CELL_W + GAP);
        int gy = PAD + (pos / GRID_COLS) * (CELL_H + GAP);
        int on = color == g_choice;
        if (on) {
            RECT_ edge;
            HBRUSH hi = CreateSolidBrush(0x00FFFFFFu);
            edge.left = gx; edge.top = gy; edge.right = gx + CELL_W; edge.bottom = gy + 2;
            FillRect(mem, &edge, hi);
            edge.top = gy + CELL_H - 2; edge.bottom = gy + CELL_H;
            FillRect(mem, &edge, hi);
            edge.top = gy; edge.bottom = gy + CELL_H; edge.right = gx + 2;
            FillRect(mem, &edge, hi);
            edge.left = gx + CELL_W - 2; edge.right = gx + CELL_W;
            FillRect(mem, &edge, hi);
            DeleteObject(hi);
        }
        if (g_spr_ok) {
            int sw = g_x1 - g_x0 + 1;
            int sh = g_y1 - g_y0 + 1;
            int box_w = CELL_W - 16;
            int box_h = CELL_H - 40;
            int dw = box_w;
            int dh = box_h;
            if (sw > 0 && sh > 0 && sw * box_h > sh * box_w) dh = box_w * sh / sw;
            else if (sh > 0) dw = box_h * sw / sh;
            if (dw < 8) dw = 8;
            if (dh < 8) dh = 8;
            if (dw > 150) dw = 150;
            if (dh > 170) dh = 170;
            draw_sprite(mem, gx + (CELL_W - dw) / 2, gy + 6 + (box_h - dh) / 2, dw, dh, color);
        }
        paint_marks(mem, color, gx + CELL_W / 2, gy + CELL_H - 6, on);
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_font);
    SelectObject(mem, old);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}
static int pos_of(int color) {
    int i;
    for (i = 0; i < COLOR_COUNT; ++i) if (g_show[i] == color) return i;
    return 0;
}

static void move_grid(int dx, int dy) {
    int pos = pos_of(g_choice);
    int col = pos % GRID_COLS;
    int row = pos / GRID_COLS;
    col += dx;
    row += dy;
    if (col < 0) col = 0;
    if (col >= GRID_COLS) col = GRID_COLS - 1;
    if (row < 0) row = 0;
    if (row >= GRID_ROWS) row = GRID_ROWS - 1;
    g_choice = g_show[row * GRID_COLS + col];
    write_color(g_player, g_slot, g_choice);
    if (g_hwnd) InvalidateRect(g_hwnd, 0, 0);
}

static int choice_at(int x, int y) {
    int col, row;
    RECT_ rc;
    if (g_sheet_ok && g_hwnd && GetClientRect(g_hwnd, &rc) && rc.right > 0 && rc.bottom > 0) {
        if (x < 0 || y < 0 || x >= rc.right || y >= rc.bottom) return -1;
        col = x * GRID_COLS / rc.right;
        row = y * GRID_ROWS / rc.bottom;
        if (col < 0) col = 0;
        if (row < 0) row = 0;
        if (col >= GRID_COLS) col = GRID_COLS - 1;
        if (row >= GRID_ROWS) row = GRID_ROWS - 1;
        return g_show[row * GRID_COLS + col];
    }
    for (row = 0; row < GRID_ROWS; ++row) {
        for (col = 0; col < GRID_COLS; ++col) {
            int gx = PAD + col * (CELL_W + GAP);
            int gy = PAD + row * (CELL_H + GAP);
            if (x >= gx && x < gx + CELL_W && y >= gy && y < gy + CELL_H)
                return g_show[row * GRID_COLS + col];
        }
    }
    return -1;
}

static void poll_pad(Pick* pk) {
    XState st;
    u16 down = 0;
    u16 pressed;
    u16 xb = 0;
    u32 raw = 0;
    u32 xin = 0;
    u32 abcd;
    u32 edge;
    int who;
    if (!pk || !pk->on || !pk->hwnd || !g_base) return;
    who = pk->player;
    if (who < 0 || who > 1) who = 0;
    g_hwnd = pk->hwnd;
    g_player = pk->player;
    g_slot = pk->slot;
    g_id = pk->id;
    g_choice = pk->choice;
    g_original = pk->original;
    g_pad_prev = pk->pad_prev;
    g_pad_idle = pk->pad_idle;
    g_abcd_prev = pk->abcd_prev;
    g_visible = 1;
    if (g_input_hooked == 1) {
        raw = g_slot_raw[who];
        xin = g_slot_xi[who];
    } else {
        raw = *(u32*)(g_base + 0x5527A0u + (unsigned)who * 4u);
        xin = *(u32*)(g_base + 0x5527E8u + (unsigned)who * 4u);
    }
    if (raw & 1u) down = (u16)(down | PAD_UP);
    if (raw & 2u) down = (u16)(down | PAD_DOWN);
    if (raw & 4u) down = (u16)(down | PAD_LEFT);
    if (raw & 8u) down = (u16)(down | PAD_RIGHT);
    if (xin & 0x01000000u) down = (u16)(down | PAD_UP);
    if (xin & 0x02000000u) down = (u16)(down | PAD_DOWN);
    if (xin & 0x04000000u) down = (u16)(down | PAD_LEFT);
    if (xin & 0x08000000u) down = (u16)(down | PAD_RIGHT);
    if (g_xinput && g_xinput((DWORD)who, &st) == 0) {
        xb = st.pad.buttons;
        if (st.pad.lx > 18000) down = (u16)(down | PAD_RIGHT);
        if (st.pad.lx < -18000) down = (u16)(down | PAD_LEFT);
        if (st.pad.ly > 18000) down = (u16)(down | PAD_UP);
        if (st.pad.ly < -18000) down = (u16)(down | PAD_DOWN);
    }
    down = (u16)(down | (xb & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_START | PAD_BACK)));
    abcd = raw & 0x1E0u;
    if (abcd == 0) {
        if (xb & 0x1000u) abcd |= 0x20u;
        if (xb & 0x2000u) abcd |= 0x40u;
        if (xb & 0x4000u) abcd |= 0x80u;
        if (xb & 0x8000u) abcd |= 0x100u;
    }
    if (!g_pad_seen && (raw || abcd || (down & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_START | PAD_BACK)))) {
        g_pad_seen = 1;
        log_num("botones ", (int)raw);
    }
    if (down == 0) {
        ++g_pad_idle;
        if (g_pad_idle < 3) down = g_pad_prev;
    } else {
        g_pad_idle = 0;
    }
    pressed = (u16)(down & (u16)~g_pad_prev);
    g_pad_prev = down;
    if (pressed & PAD_LEFT) move_grid(-1, 0);
    if (pressed & PAD_RIGHT) move_grid(1, 0);
    if (pressed & PAD_UP) move_grid(0, -1);
    if (pressed & PAD_DOWN) move_grid(0, 1);
    edge = abcd & ~g_abcd_prev;
    g_abcd_prev = abcd;
    pk->choice = g_choice;
    pk->pad_prev = g_pad_prev;
    pk->pad_idle = g_pad_idle;
    pk->abcd_prev = g_abcd_prev;
    if (edge && abcd) {
        confirm_choice();
        return;
    }
    if (pressed & PAD_START) confirm_choice();
    if (pressed & PAD_BACK) cancel_choice();
}

static LRESULT CALLBACK wndproc(HWND hwnd, u32 msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        paint(hwnd);
        return 0;
    }
    if (msg == WM_TIMER) {
        int i;
        setup_pad();
        watch_select();
        order_pump();
        show_next();
        for (i = 0; i < 2; ++i) {
            if (!g_pk[i].on || !g_pk[i].hwnd) continue;
            SetWindowPos(g_pk[i].hwnd, HWND_TOPMOST, 0, 0, 0, 0, 0x0013u);
            poll_pad(&g_pk[i]);
        }
        if (g_input_hooked != 1 && (g_pk[0].on || g_pk[1].on)) {
            for (i = 0; i < 8; ++i) {
                *(u32*)(g_base + 0x5527A0u + (unsigned)i * 4u) = 0;
                *(u32*)(g_base + 0x5527C8u + (unsigned)i * 4u) = 0;
            }
            for (i = 0; i < 4; ++i) *(u32*)(g_base + 0x5527E8u + (unsigned)i * 4u) = 0;
        }
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        Pick* pk = g_pk[0].hwnd == hwnd ? &g_pk[0] : (g_pk[1].hwnd == hwnd ? &g_pk[1] : 0);
        if (!pk || !pk->on) return 0;
        g_hwnd = pk->hwnd;
        g_player = pk->player;
        g_slot = pk->slot;
        g_id = pk->id;
        g_choice = pk->choice;
        g_original = pk->original;
        if (wp == VK_LEFT) move_grid(-1, 0);
        else if (wp == VK_RIGHT) move_grid(1, 0);
        else if (wp == VK_UP) move_grid(0, -1);
        else if (wp == VK_DOWN) move_grid(0, 1);
        else if (wp == VK_RETURN) confirm_choice();
        else if (wp == VK_ESCAPE) cancel_choice();
        else { pk->choice = g_choice; return 0; }
        pk->choice = g_choice;
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        Pick* pk = g_pk[0].hwnd == hwnd ? &g_pk[0] : (g_pk[1].hwnd == hwnd ? &g_pk[1] : 0);
        int x = (short)(lp & 0xFFFF);
        int y = (short)(lp >> 16);
        int index;
        if (!pk || !pk->on) return 0;
        g_hwnd = pk->hwnd;
        g_player = pk->player;
        g_slot = pk->slot;
        g_id = pk->id;
        g_choice = pk->choice;
        g_original = pk->original;
        g_sheet_ok = pk->sheet_ok;
        index = choice_at(x, y);
        if (index >= 0) {
            g_choice = index;
            pk->choice = index;
            confirm_choice();
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        Pick* pk = g_pk[0].hwnd == hwnd ? &g_pk[0] : (g_pk[1].hwnd == hwnd ? &g_pk[1] : 0);
        if (pk && pk->on) {
            g_hwnd = pk->hwnd;
            g_player = pk->player;
            g_slot = pk->slot;
            g_original = pk->original;
            cancel_choice();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI ui_thread(void* unused) {
    WNDCLASSEXW_ wc;
    MSG_ msg;
    wchar_t dir[MAX_PATH];
    int n;
    int i;
    (void)unused;
    for (i = 0; i < 2; ++i) {
        g_prev[i][0] = -1;
        g_prev[i][1] = -1;
        g_prev[i][2] = -1;
    }
    g_base = (u8*)GetModuleHandleW(0);
    n = (int)GetModuleFileNameW(g_self, dir, MAX_PATH);
    while (n > 0 && dir[n - 1] != L'\\') --n;
    dir[n] = 0;
    {
        int k = 0;
        while (dir[k] && k < MAX_PATH) {
            g_log[k] = dir[k];
            ++k;
        }
        {
            const wch* tail = L"kof98color.log";
            int t = 0;
            while (tail[t] && k + 1 < MAX_PATH) g_log[k++] = tail[t++];
            g_log[k] = 0;
        }
    }
    load_pal();
    install_team_hook();
    install_palette_hook();
    install_resolve_hook();
    install_spawn_hook();
    install_fight_color();
    install_order_gate();
    install_order_hook();
    log_line("mando libre hasta la ventana de color");
    setup_pad();
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = g_self;
    wc.hCursor = LoadCursorW(0, (const wch*)(u64)32512);
    wc.lpszClassName = L"Kof98ColorSelect";
    RegisterClassExW(&wc);
    for (i = 0; i < 2; ++i) {
        g_pk[i].player = i;
        g_pk[i].hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"Kof98ColorSelect",
            i ? L"Color P2" : L"Color P1", WS_POPUP, 80 + i * 40, 80, 360, 200, 0, 0, g_self, 0);
    }
    g_hwnd = g_pk[0].hwnd;
    SetTimer(g_pk[0].hwnd, 1, 40, 0);
    log_line("selector de color activo");
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

int WINAPI DllMain(HMODULE self, DWORD reason, void* reserved) {
    (void)reserved;
    if (reason == 1) {
        int i;
        DisableThreadLibraryCalls(self);
        g_self = self;
        for (i = 0; i < CHARS; ++i) {
            g_force[i] = 0xFF;
            g_owned[0][i] = 0xFF;
            g_owned[1][i] = 0xFF;
        }
        CreateThread(0, 0, ui_thread, 0, 0, 0);
    }
    return 1;
}
