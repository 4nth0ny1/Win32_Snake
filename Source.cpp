// Win32 tile game with player + food respawn on collision + auto-step movement (Snake-style)
// Adds growing snake body on food
// Drop-in single file

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> // rand, srand
#include <time.h>   // time for srand
#include <string.h> // memmove

#define internal static 
#define local_persist static 
#define global_variable static 

typedef int8_t  int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

enum { TILE_SIZE = 32, MAP_W = 12, MAP_H = 8 };

// Colors for a top-down 32-bit DIB (0x00BBGGRR)
#define COLOR_BG        0x00202020   // dark gray
#define COLOR_FLOOR     0x00303030   // gray
#define COLOR_WALL      0x006666AA   // blue (pattern only)
#define COLOR_PLAYER    0x0000AA00   // head green
#define COLOR_BODY      0x00008800   // body green (darker)
#define COLOR_FOOD      0x00FFA500   // orange

// ==================== Globals ====================

global_variable bool Running;

global_variable BITMAPINFO BitmapInfo;
global_variable void* BitmapMemory;
global_variable int BitmapWidth;
global_variable int BitmapHeight;
global_variable int BytesPerPixel = 4;

global_variable int gMap[MAP_H][MAP_W] = {
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
    {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0},
    {0, 1, 0, 2, 0, 1, 0, 1, 0, 1, 0, 1}, // '2' marks starting player; we'll restore to 0/1
    {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 3, 1, 0, 1}, // '3' marks starting food; we'll restore to 0/1
    {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
    {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0},
};

// --- Snake state ---
typedef struct { int r, c; } SNAKE_SEGMENT;
enum { SNAKE_MAX = MAP_W * MAP_H };

global_variable SNAKE_SEGMENT gSnake[SNAKE_MAX];
global_variable int gSnakeLen = 1; // starts as 1 tile

// For convenience (head mirror of snake[0])
global_variable int gPlayerRow = 0;
global_variable int gPlayerCol = 0;

// Food position in TILE COORDS (row/col)
global_variable int gFoodRow = 1;
global_variable int gFoodCol = 1;

// Snake-style auto movement state
global_variable int gDirRow = 0;   // -1 up, +1 down
global_variable int gDirCol = 1;   // -1 left, +1 right (start moving right)
global_variable DWORD gLastStepMs = 0;
global_variable const DWORD STEP_MS = 400; // move every 400 ms

// ==================== Backbuffer ====================

internal void Win32ResizeDIBSection(int Width, int Height) {
    if (BitmapMemory) {
        VirtualFree(BitmapMemory, 0, MEM_RELEASE);
        BitmapMemory = 0;
    }

    BitmapWidth = Width;
    BitmapHeight = Height;

    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth = BitmapWidth;
    BitmapInfo.bmiHeader.biHeight = -BitmapHeight; // top-down
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = (BitmapWidth * BitmapHeight) * BytesPerPixel;
    BitmapMemory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
}

internal void ClearBackBuffer(uint32 color_one) {
    if (!BitmapMemory) return;
    uint32* pixels = (uint32*)BitmapMemory;
    int totalPixels = BitmapWidth * BitmapHeight;
    for (int i = 0; i < totalPixels; ++i) {
        pixels[i] = color_one;
    }
}

internal void RenderRect(int left, int top, int right, int bottom, uint32 color) {
    if (!BitmapMemory) return;

    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > BitmapWidth)  right = BitmapWidth;
    if (bottom > BitmapHeight) bottom = BitmapHeight;
    if (right <= left || bottom <= top) return;

    for (int y = top; y < bottom; ++y) {
        uint32* row = (uint32*)BitmapMemory + y * BitmapWidth + left;
        for (int x = 0; x < (right - left); ++x) {
            row[x] = color;
        }
    }
}

internal void ComputeTileLayout(int* outTilePx, int* outOffsetX, int* outOffsetY) {
    // Fit as many whole, square tiles as possible
    int tileW = BitmapWidth / MAP_W;
    int tileH = BitmapHeight / MAP_H;
    int tile = (tileW < tileH) ? tileW : tileH;
    if (tile < 1) tile = 1; // safety for tiny windows

    int usedW = tile * MAP_W;
    int usedH = tile * MAP_H;

    *outTilePx = tile;
    *outOffsetX = (BitmapWidth - usedW) / 2;  // center horizontally
    *outOffsetY = (BitmapHeight - usedH) / 2; // center vertically
}

internal void RenderTileMap(const int map[MAP_H][MAP_W]) {
    int tile, offX, offY;
    ComputeTileLayout(&tile, &offX, &offY);

    for (int row = 0; row < MAP_H; ++row) {
        for (int col = 0; col < MAP_W; ++col) {
            int left = offX + col * tile;
            int top = offY + row * tile;
            int right = left + tile;
            int bottom = top + tile;

            uint32 color = COLOR_FLOOR;
            if (map[row][col] == 1)      color = COLOR_WALL;
            else if (map[row][col] == 2) color = COLOR_PLAYER; // legacy preview if left in map
            else if (map[row][col] == 3) color = COLOR_FOOD;   // legacy preview if left in map

            RenderRect(left, top, right, bottom, color);
        }
    }
}

internal void Win32UpdateWindow(HDC DeviceContext, RECT* ClientRect, int X, int Y, int Width, int Height) {
    int WindowWidth = ClientRect->right - ClientRect->left;
    int WindowHeight = ClientRect->bottom - ClientRect->top;

    StretchDIBits(
        DeviceContext,
        X, Y, WindowWidth, WindowHeight,
        X, Y, BitmapWidth, BitmapHeight,
        BitmapMemory,
        &BitmapInfo,
        DIB_RGB_COLORS, SRCCOPY
    );
}

// ==================== Helpers ====================

internal void FindPlayerOnMap(void) {
    for (int r = 0; r < MAP_H; ++r) {
        for (int c = 0; c < MAP_W; ++c) {
            if (gMap[r][c] == 2) {
                gPlayerRow = r;
                gPlayerCol = c;
                // restore the checkerboard value under the player
                gMap[r][c] = ((r + c) & 1);  // 0/1 alternating pattern
                // seed snake
                gSnake[0].r = r;
                gSnake[0].c = c;
                gSnakeLen = 1;
                return;
            }
        }
    }
    // fallback if no '2' in map
    gPlayerRow = 0; gPlayerCol = 0;
    gSnake[0].r = 0; gSnake[0].c = 0; gSnakeLen = 1;
}

internal void FindFoodOnMap(void) {
    for (int r = 0; r < MAP_H; ++r) {
        for (int c = 0; c < MAP_W; ++c) {
            if (gMap[r][c] == 3) {
                gFoodRow = r;
                gFoodCol = c;
                // restore the checkerboard value under the food
                gMap[r][c] = ((r + c) & 1);
                return;
            }
        }
    }
    // fallback if no '3' in map
    gFoodRow = 1; gFoodCol = 1;
}

internal int RandRange(int lo, int hi) { // inclusive
    return lo + (rand() % (hi - lo + 1));
}

internal bool SnakeOccupies(int r, int c) {
    for (int i = 0; i < gSnakeLen; ++i) {
        if (gSnake[i].r == r && gSnake[i].c == c) return true;
    }
    return false;
}

internal void PlaceFoodRandomly(void) {
    int r, c;
    // keep picking until it's not on the snake
    do {
        r = RandRange(0, MAP_H - 1);
        c = RandRange(0, MAP_W - 1);
    } while (SnakeOccupies(r, c));
    gFoodRow = r;
    gFoodCol = c;
}

// Return false if move would hit walls (edges); true otherwise.
// Handles growth if food is eaten.
internal bool StepSnake(void) {
    int nextR = gSnake[0].r + gDirRow;
    int nextC = gSnake[0].c + gDirCol;

    // clamp to map bounds (treat edges as blocking)
    if (nextR < 0 || nextR >= MAP_H || nextC < 0 || nextC >= MAP_W) {
        return false; // stop on edge; you can choose to end game or ignore
    }

    bool ate = (nextR == gFoodRow && nextC == gFoodCol);

    // shift body backward to make room for new head
    // If eating: increase length first so we don't overwrite tail removal
    if (ate && gSnakeLen < SNAKE_MAX) {
        gSnakeLen += 1;
    }

    // move segments back (from tail toward head)
    if (gSnakeLen > 1) {
        memmove(&gSnake[1], &gSnake[0], sizeof(SNAKE_SEGMENT) * (gSnakeLen - 1));
    }

    // place new head
    gSnake[0].r = nextR;
    gSnake[0].c = nextC;

    // update convenience mirrors
    gPlayerRow = nextR;
    gPlayerCol = nextC;

    if (ate) {
        PlaceFoodRandomly();
    }
    else {
        // If not eaten, we already shifted—this effectively popped the tail
        // because we did NOT increase length.
        // (The memmove + unchanged gSnakeLen is enough.)
    }

    return true;
}

// ==================== Rendering ====================

internal void RenderSnake(void) {
    int tile, offX, offY;
    ComputeTileLayout(&tile, &offX, &offY);

    // Draw body first (index 1..len-1)
    for (int i = 1; i < gSnakeLen; ++i) {
        int left = offX + gSnake[i].c * tile;
        int top = offY + gSnake[i].r * tile;
        RenderRect(left, top, left + tile, top + tile, COLOR_BODY);
    }

    // Draw head on top
    int left = offX + gSnake[0].c * tile;
    int top = offY + gSnake[0].r * tile;
    RenderRect(left, top, left + tile, top + tile, COLOR_PLAYER);
}

internal void RenderFood(void) {
    int tile, offX, offY;
    ComputeTileLayout(&tile, &offX, &offY);

    int left = offX + gFoodCol * tile;
    int top = offY + gFoodRow * tile;
    int right = left + tile;
    int bottom = top + tile;

    RenderRect(left, top, right, bottom, COLOR_FOOD); // orange food
}

// ==================== Window Proc ====================
LRESULT CALLBACK
Win32MainWindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) {
    LRESULT Result = 0;

    switch (Message) {
    case WM_SIZE: {
        RECT ClientRect;
        GetClientRect(Window, &ClientRect);
        int Width = ClientRect.right - ClientRect.left;
        int Height = ClientRect.bottom - ClientRect.top;
        Win32ResizeDIBSection(Width, Height);
    } break;

    case WM_CLOSE: {
        Running = false;
    } break;

    case WM_ACTIVATEAPP: {
        OutputDebugStringA("ACTIVATE\n");
    } break;

    case WM_DESTROY: {
        Running = false;
    } break;

    case WM_KEYDOWN: {
        // Direction changes only; ignore auto-repeat
        bool wasDown = (LParam & (1 << 30)) != 0;
        if (!wasDown) {
            switch (WParam) {
            case 'W': if (gDirRow != 1) { gDirRow = -1; gDirCol = 0; } break;
            case 'S': if (gDirRow != -1) { gDirRow = +1; gDirCol = 0; } break;
            case 'A': if (gDirCol != 1) { gDirRow = 0;  gDirCol = -1; } break;
            case 'D': if (gDirCol != -1) { gDirRow = 0;  gDirCol = +1; } break;
            case VK_ESCAPE: PostQuitMessage(0); break;
            }
        }
    } break;

    case WM_PAINT: {
        PAINTSTRUCT Paint;
        HDC DeviceContext = BeginPaint(Window, &Paint);
        int X = Paint.rcPaint.left;
        int Y = Paint.rcPaint.top;
        int Width = Paint.rcPaint.right - Paint.rcPaint.left;
        int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;

        RECT ClientRect;
        GetClientRect(Window, &ClientRect);
        Win32UpdateWindow(DeviceContext, &ClientRect, X, Y, Width, Height);

        EndPaint(Window, &Paint);
    } break;

    default: {
        Result = DefWindowProc(Window, Message, WParam, LParam);
    } break;
    }
    return(Result);
}

// ==================== Entry ====================
int CALLBACK WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CommandLine, int ShowCode) {
    WNDCLASS WindowClass = {};
    WindowClass.lpfnWndProc = Win32MainWindowCallback;
    WindowClass.hInstance = Instance;
    WindowClass.lpszClassName = "Win32WindowClass";

    if (RegisterClassA(&WindowClass)) {
        HWND Window = CreateWindowExA(
            0,
            WindowClass.lpszClassName,
            "Win32 Snake",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            0, 0, Instance, 0
        );

        if (Window) {
            // Init backbuffer to current client size
            RECT cr; GetClientRect(Window, &cr);
            Win32ResizeDIBSection(cr.right - cr.left, cr.bottom - cr.top);

            // Seed RNG once
            srand((unsigned)time(NULL));

            // Locate initial player/food markers from the map and restore tile underneath
            FindPlayerOnMap();
            FindFoodOnMap();

            // If they happen to start overlapped, move food away
            if (gPlayerRow == gFoodRow && gPlayerCol == gFoodCol) {
                PlaceFoodRandomly();
            }

            // Start moving immediately
            gLastStepMs = GetTickCount64();

            Running = true;
            while (Running) {
                // Pump messages
                MSG Message;
                while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE)) {
                    if (Message.message == WM_QUIT) {
                        Running = false;
                    }
                    TranslateMessage(&Message);
                    DispatchMessageA(&Message);
                }

                // Auto-step movement on a fixed cadence
                DWORD now = GetTickCount64();
                if ((now - gLastStepMs) >= STEP_MS) {
                    // advance snake; ignore return for now (edge stops)
                    StepSnake();
                    gLastStepMs = now;
                }

                // Render
                ClearBackBuffer(COLOR_BG);
                RenderTileMap(gMap); // draws floor/walls (checkerboard)
                RenderSnake();       // draw head + body
                RenderFood();        // draw food on top

                HDC DeviceContext = GetDC(Window);
                RECT ClientRect;
                GetClientRect(Window, &ClientRect);
                int WindowWidth = ClientRect.right - ClientRect.left;
                int WindowHeight = ClientRect.bottom - ClientRect.top;
                Win32UpdateWindow(DeviceContext, &ClientRect, 0, 0, WindowWidth, WindowHeight);
                ReleaseDC(Window, DeviceContext);
            }
        }
    }

    return(0);
}
