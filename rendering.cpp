// Overlay using Direct3D 9... fuck you ImGui.
// Compile command: cl rendering.cpp /I. /link user32.lib gdi32.lib d3d9.lib

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <stdbool.h>
#include <math.h>
#include <process.h> 
#include "rendering.h"
#include <d3d9.h>
#include <assert.h>




LPDIRECT3D9              g_pD3D = NULL;
LPDIRECT3DDEVICE9        g_pd3dDevice = NULL;
D3DPRESENT_PARAMETERS    g_d3dpp = {};

HWND g_hWnd = NULL;
BOOL rpm(HANDLE hProc, uintptr_t addr, void *buf, SIZE_T size);
HANDLE find_cs2_process(void);


#define WINDOW_TITLE L"CS2 External Overlay"
#define TARGET_PROCESS_NAME L"cs2.exe"
#define REFRESH_RATE 32 

struct Entity {
    float x, y, z; 
    float prev_x, prev_y, prev_z;
    float head_x, head_y, head_z; 
    float prev_head_x, prev_head_y, prev_head_z;
    int health;
    int team;
};

float g_viewMatrix[4][4];
struct Entity g_entities[64];
int g_entityCount = 0;
HANDLE g_hProcess = NULL;
volatile bool g_Running = true;
CRITICAL_SECTION g_dataLock;

// Gets the offsets from the header file
uintptr_t g_dwEntityList = ENTITYLIST_OFFSET;
uintptr_t g_dwViewMatrix = VIEWMATRIX_OFFSET;
uintptr_t g_dwLocalPlayerPawn = LOCALPLAYERPAWN_OFFSET;
uintptr_t g_m_hPlayerPawn = HPLAYERPAWN_OFFSET;
uintptr_t g_m_iHealth = IHEALTH_OFFSET;
uintptr_t g_m_iTeamNum = ITEAMNUM_OFFSET;
uintptr_t g_m_vOldOrigin = VOLDORIGIN_OFFSET;


void update_view_matrix() {
    if (!g_hProcess || !g_dwViewMatrix) return;
    rpm(g_hProcess, g_dwViewMatrix, g_viewMatrix, sizeof(g_viewMatrix));
}

void update_entities() {
    memset(g_entities, 0, sizeof(g_entities));
    g_entityCount = 0;
    static int last_print = 0;
    if (!g_hProcess || !g_dwEntityList || !g_dwLocalPlayerPawn) {
        if (last_print != 1) {
            printf("[DEBUG] update_entities: missing process or offsets\n");
            last_print = 1;
        }
        return;
    }
    if (last_print != 2) {
        printf("[DEBUG] update_entities: process and offsets OK\n");
        last_print = 2;
    }
    int found = 0;
    int debug_printed = 0;
    uintptr_t seen_pawns[64] = {0};
    int seen_count = 0;

    uintptr_t ent_list = 0;
    if (!rpm(g_hProcess, g_dwEntityList, &ent_list, sizeof(ent_list))) {
        printf("[ERROR] Failed to read entity_list pointer from 0x%llx\n", (unsigned long long)g_dwEntityList);
        return;
    }

    printf("[DEBUG] Entity list pointer: 0x%llx\n", (unsigned long long)ent_list);

    uintptr_t local = 0;
    if (!rpm(g_hProcess, g_dwLocalPlayerPawn, &local, sizeof(local))) {
        printf("[ERROR] Failed to read local player from 0x%llx\n", (unsigned long long)g_dwLocalPlayerPawn);
        return;
    }

    printf("[DEBUG] Local player pointer: 0x%llx\n", (unsigned long long)local);

    for (int i = 1; i < 65; i++) {
        uintptr_t entry_ptr = 0;
        if (!rpm(g_hProcess, ent_list + (8 * (i & 0x7FFF) >> 9) + 16, &entry_ptr, sizeof(entry_ptr))) {
            if (!debug_printed) { 
                printf("id:%d entry_ptr read fail at 0x%llx\n", i, (unsigned long long)(ent_list + (8 * (i & 0x7FFF) >> 9) + 16));
                debug_printed = 1;
            }
            continue;
        }
        if (!entry_ptr || entry_ptr < 0x1000) continue;
        uintptr_t controller_ptr = 0;
        if (!rpm(g_hProcess, entry_ptr + 120 * (i & 0x1FF), &controller_ptr, sizeof(controller_ptr))) continue;
        if (!controller_ptr || controller_ptr < 0x1000 || controller_ptr == local) continue;
        uintptr_t controller_pawn_ptr = 0;
        if (!rpm(g_hProcess, controller_ptr + g_m_hPlayerPawn, &controller_pawn_ptr, sizeof(controller_pawn_ptr))) continue;
        if (!controller_pawn_ptr || controller_pawn_ptr < 0x1000) continue;
        uintptr_t list_entry_ptr = 0;
        if (!rpm(g_hProcess, ent_list + 0x8 * ((controller_pawn_ptr & 0x7FFF) >> 9) + 16, &list_entry_ptr, sizeof(list_entry_ptr))) continue;
        if (!list_entry_ptr || list_entry_ptr < 0x1000) continue;
        uintptr_t pawn_ptr = 0;
        if (!rpm(g_hProcess, list_entry_ptr + 120 * (controller_pawn_ptr & 0x1FF), &pawn_ptr, sizeof(pawn_ptr))) continue;
        if (!pawn_ptr || pawn_ptr < 0x1000 || pawn_ptr == local) continue;

        // im sorry...
        bool already_seen = false;
        for (int j = 0; j < seen_count; ++j) {
            if (seen_pawns[j] == pawn_ptr) {
                already_seen = true;
                break;
            }
        }
        if (already_seen) continue;
        if (seen_count < 64) {
            seen_pawns[seen_count++] = pawn_ptr;
        } else {
            // Should never happen, but just in case
            continue;
        }

        int health = 0, team = 0;
        float pos[3] = {0};
        float head_pos[3] = {0};
        BOOL ok1 = rpm(g_hProcess, pawn_ptr + g_m_iHealth, &health, sizeof(health));
        BOOL ok2 = rpm(g_hProcess, pawn_ptr + g_m_iTeamNum, &team, sizeof(team));
        BOOL ok3 = rpm(g_hProcess, pawn_ptr + g_m_vOldOrigin, pos, sizeof(pos));
        head_pos[0] = pos[0];
        head_pos[1] = pos[1];
        head_pos[2] = pos[2] + 64.0f;
        if (health > 0 && health <= 100 && ok1 && ok2 && ok3) {
            int found_prev = -1;
            for (int j = 0; j < g_entityCount; ++j) {
                if (g_entities[j].team == team &&
                    fabsf(g_entities[j].x - pos[0]) < 0.01f &&
                    fabsf(g_entities[j].y - pos[1]) < 0.01f &&
                    fabsf(g_entities[j].z - pos[2]) < 0.01f) {
                    found_prev = j;
                    break;
                }
            }
            if (found_prev >= 0) {
                g_entities[g_entityCount].prev_x = g_entities[found_prev].prev_x;
                g_entities[g_entityCount].prev_y = g_entities[found_prev].prev_y;
                g_entities[g_entityCount].prev_z = g_entities[found_prev].prev_z;
                g_entities[g_entityCount].prev_head_x = g_entities[found_prev].prev_head_x;
                g_entities[g_entityCount].prev_head_y = g_entities[found_prev].prev_head_y;
                g_entities[g_entityCount].prev_head_z = g_entities[found_prev].prev_head_z;
            } else {
                g_entities[g_entityCount].prev_x = pos[0];
                g_entities[g_entityCount].prev_y = pos[1];
                g_entities[g_entityCount].prev_z = pos[2];
                g_entities[g_entityCount].prev_head_x = head_pos[0];
                g_entities[g_entityCount].prev_head_y = head_pos[1];
                g_entities[g_entityCount].prev_head_z = head_pos[2];
            }
            g_entities[g_entityCount].x = pos[0];
            g_entities[g_entityCount].y = pos[1];
            g_entities[g_entityCount].z = pos[2];
            g_entities[g_entityCount].head_x = head_pos[0];
            g_entities[g_entityCount].head_y = head_pos[1];
            g_entities[g_entityCount].head_z = head_pos[2];
            g_entities[g_entityCount].health = health;
            g_entities[g_entityCount].team = team;
            g_entityCount++;
            found++;
        }
    }

    static int last_found = -1;
    if (found != last_found) {
        printf("[DEBUG] Entities found: %d\n", found);
        last_found = found;
    }
}

// w2s
bool world_to_screen_f(float x, float y, float z, float* sx, float* sy, int width, int height) {
    float clip_x = g_viewMatrix[0][0] * x + g_viewMatrix[0][1] * y + g_viewMatrix[0][2] * z + g_viewMatrix[0][3];
    float clip_y = g_viewMatrix[1][0] * x + g_viewMatrix[1][1] * y + g_viewMatrix[1][2] * z + g_viewMatrix[1][3];
    float clip_w = g_viewMatrix[3][0] * x + g_viewMatrix[3][1] * y + g_viewMatrix[3][2] * z + g_viewMatrix[3][3];
    if (clip_w < 0.1f) return false;
    float ndc_x = clip_x / clip_w;
    float ndc_y = clip_y / clip_w;
    *sx = (width / 2.0f) * (ndc_x + 1.0f);
    *sy = (height / 2.0f) * (1.0f - ndc_y);
    return true;
}

void DrawBox(float x, float y, float w, float h, D3DCOLOR color) {
    struct Vertex { float x, y, z, rhw; D3DCOLOR color; };
    Vertex verts[] = {
        { x,     y,     0.0f, 1.0f, color },
        { x+w,   y,     0.0f, 1.0f, color },
        { x+w,   y+h,   0.0f, 1.0f, color },
        { x,     y+h,   0.0f, 1.0f, color },
        { x,     y,     0.0f, 1.0f, color }
    };
    g_pd3dDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_LINESTRIP, 4, verts, sizeof(Vertex));
}

// interpolate
static float smoothstep(float t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

void RenderESP(int width, int height, float lerp_alpha) {
    EnterCriticalSection(&g_dataLock);
    int localEntityCount = g_entityCount;
    struct Entity localEntities[64];
    memcpy(localEntities, g_entities, sizeof(g_entities));
    LeaveCriticalSection(&g_dataLock);

    float smooth_alpha = smoothstep(lerp_alpha);
    for (int i = 0; i < localEntityCount; ++i) {
        if (localEntities[i].x == 0.0f && localEntities[i].y == 0.0f && localEntities[i].z == 0.0f)
            continue;
            
        // Interpolate positions
        float ix = localEntities[i].prev_x + (localEntities[i].x - localEntities[i].prev_x) * smooth_alpha;
        float iy = localEntities[i].prev_y + (localEntities[i].y - localEntities[i].prev_y) * smooth_alpha;
        float iz = localEntities[i].prev_z + (localEntities[i].z - localEntities[i].prev_z) * smooth_alpha;
        float ihx = localEntities[i].prev_head_x + (localEntities[i].head_x - localEntities[i].prev_head_x) * smooth_alpha;
        float ihy = localEntities[i].prev_head_y + (localEntities[i].head_y - localEntities[i].prev_head_y) * smooth_alpha;
        float ihz = localEntities[i].prev_head_z + (localEntities[i].head_z - localEntities[i].prev_head_z) * smooth_alpha;
        float sx_feet, sy_feet, sx_head, sy_head;
        bool feet_ok = world_to_screen_f(ix, iy, iz, &sx_feet, &sy_feet, width, height);
        bool head_ok = world_to_screen_f(ihx, ihy, ihz, &sx_head, &sy_head, width, height);
                                         
        if (!feet_ok || !head_ok)
            continue;
            
        // sanity check
        if (sx_feet < 0 || sx_feet > width || sy_feet < 0 || sy_feet > height ||
            sx_head < 0 || sx_head > width || sy_head < 0 || sy_head > height)
            continue;
            
        float box_height = fabsf(sy_feet - sy_head);
        float box_width = box_height / 2.0f;
        
        // santity check box dimensions
        if (box_height < 5.0f || box_height > height/1.5f) continue;
        
        float box_x = sx_head - box_width / 2.0f;
        float box_y = sy_head;
        
        // if you don't know what this does, you are slow
        DrawBox(box_x, box_y, box_width, box_height, D3DCOLOR_ARGB(255,0,255,0));
    }
}

void CleanupDeviceD3D() {
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = NULL; }
}

bool CreateDeviceD3D(HWND hWnd) {
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
        return false;
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
    g_d3dpp.BackBufferWidth = GetSystemMetrics(SM_CXSCREEN);
    g_d3dpp.BackBufferHeight = GetSystemMetrics(SM_CYSCREEN);
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    g_d3dpp.hDeviceWindow = hWnd;
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;
    return true;
}

void ResetDevice() {
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        ; // optional // me from the future! HI! // you can remove this line if you want, it does nothing
}


LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
            g_d3dpp.BackBufferWidth = LOWORD(lParam);
            g_d3dpp.BackBufferHeight = HIWORD(lParam);
            ResetDevice();
        }
        return 0;
    case WM_DESTROY:
        g_Running = false;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"OverlayClass", NULL };
    RegisterClassExW(&wc);
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        wc.lpszClassName, WINDOW_TITLE, WS_POPUP, 0, 0, width, height, NULL, NULL, wc.hInstance, NULL);
    g_hWnd = hwnd;
    SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 0, LWA_COLORKEY);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    InitializeCriticalSection(&g_dataLock);
    g_hProcess = find_cs2_process();
    if (!g_hProcess) return 1;
    MSG msg;
    DWORD lastTick = GetTickCount();
    DWORD lastUpdate = GetTickCount();
    const DWORD updateInterval = 1000 / 120; // 120 ! FPS
    while (g_Running) {
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) g_Running = false;
        }

        DWORD now = GetTickCount();
        DWORD frameTime = 1000 / 120; 
        DWORD elapsed = now - lastTick;
        if (elapsed < frameTime) {
            Sleep(frameTime - elapsed);
            continue;
        }
        float lerp_alpha = 1.0f;
        if (now - lastUpdate < updateInterval)
            lerp_alpha = (float)(now - lastUpdate) / (float)updateInterval;
        else
            lerp_alpha = 1.0f;

        // optimization stuff // this does not work i think
        if (now - lastUpdate >= updateInterval) {
            update_view_matrix();
            update_entities();
            lastUpdate = now;
        }
        lastTick = now;

        // FUCFK YOU IM GUI OMG!! thank god I don't have to use it. sorry if you want it, but honestely just kys idc
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        D3DCOLOR clear_col_dx = D3DCOLOR_ARGB(255,0,0,0); // Opaque black
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, clear_col_dx, 1.0f, 0);

        if (g_pd3dDevice->BeginScene() >= 0) {
            RenderESP(width, height, lerp_alpha);
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
        if (result == D3DERR_DEVICELOST && g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
            ResetDevice();
    }
    CleanupDeviceD3D();
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    DeleteCriticalSection(&g_dataLock);
    return 0;
}

// module base address retrieval
uintptr_t get_module_base(DWORD pid, const wchar_t* module_name) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        printf("[ERROR] Failed to create module snapshot\n");
        return 0;
    }

    MODULEENTRY32W me32;
    me32.dwSize = sizeof(MODULEENTRY32W);
    
    if (!Module32FirstW(hSnapshot, &me32)) {
        CloseHandle(hSnapshot);
        printf("[ERROR] Failed to get first module\n");
        return 0;
    }

    uintptr_t base_address = 0;
    do {
        if (_wcsicmp(me32.szModule, module_name) == 0) {
            base_address = (uintptr_t)me32.modBaseAddr;
            break;
        }
    } while (Module32NextW(hSnapshot, &me32));

    CloseHandle(hSnapshot);
    return base_address;
}

// uhh what?
HANDLE find_cs2_process(void) {
    HANDLE hProcessSnap;
    PROCESSENTRY32W pe32;
    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) return NULL;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    if (!Process32FirstW(hProcessSnap, &pe32)) { CloseHandle(hProcessSnap); return NULL; }
    DWORD pid = 0;
    do {
        if (_wcsicmp(pe32.szExeFile, TARGET_PROCESS_NAME) == 0) {
            pid = pe32.th32ProcessID;
            break;
        }
    } while (Process32NextW(hProcessSnap, &pe32));
    CloseHandle(hProcessSnap);
    if (!pid) {
        wprintf(L"[ERROR] CS2 process not found! Is the game running as '%ls'? Try running overlay as administrator.\n", TARGET_PROCESS_NAME);
        return NULL;
    }
    
    // why is there another comment here? I don't know, but I will leave it here
    uintptr_t module_base = get_module_base(pid, L"client.dll");
    if (module_base == 0) {
        printf("[ERROR] Failed to get client.dll base address\n");
        return NULL;
    }
    
    printf("[INFO] Found client.dll at 0x%llx\n", (unsigned long long)module_base);
    
    g_dwEntityList = module_base + ENTITYLIST_OFFSET;
    g_dwViewMatrix = module_base + VIEWMATRIX_OFFSET;
    g_dwLocalPlayerPawn = module_base + LOCALPLAYERPAWN_OFFSET;
    
    // bru
    dump_offsets(module_base);
    
    HANDLE hProc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) {
        printf("[ERROR] Failed to open CS2 process. Try running overlay as administrator.\n");
        return NULL;
    }
    
    // tests
    DWORD test_value = 0;
    if (!ReadProcessMemory(hProc, (LPCVOID)module_base, &test_value, sizeof(test_value), NULL)) {
        DWORD error = GetLastError();
        printf("[ERROR] Failed to read from process memory (Error: %lu). Try running as administrator.\n", error);
        CloseHandle(hProc);
        return NULL;
    }
    
    printf("[INFO] Successfully read from process memory\n");
    return hProc;
}

BOOL rpm(HANDLE hProc, uintptr_t addr, void *buf, SIZE_T size) {
    SIZE_T out;
    return ReadProcessMemory(hProc, (LPCVOID)addr, buf, size, &out);
}

void dump_offsets(uintptr_t module_base) {
    printf("[DEBUG] Module base: 0x%llx\n", (unsigned long long)module_base);
    printf("[DEBUG] Current offsets (relative to module base):\n");
    printf("  ENTITYLIST_OFFSET:      0x%llx (absolute: 0x%llx)\n", 
           (unsigned long long)ENTITYLIST_OFFSET, 
           (unsigned long long)(module_base + ENTITYLIST_OFFSET));
    printf("  VIEWMATRIX_OFFSET:      0x%llx (absolute: 0x%llx)\n", 
           (unsigned long long)VIEWMATRIX_OFFSET, 
           (unsigned long long)(module_base + VIEWMATRIX_OFFSET));
    printf("  LOCALPLAYERPAWN_OFFSET: 0x%llx (absolute: 0x%llx)\n", 
           (unsigned long long)LOCALPLAYERPAWN_OFFSET, 
           (unsigned long long)(module_base + LOCALPLAYERPAWN_OFFSET));
}