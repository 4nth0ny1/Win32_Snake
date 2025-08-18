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
global_variable bool gGameOver = false;

global_variable BITMAPINFO BitmapInfo;
global_variable void* BitmapMemory;
global_variable int BitmapWidth;
global_variable int BitmapHeight;
global_variable int BytesPerPixel = 4;

// Backbuffer as a DIBSection + memory DC (so we can TextOut into it)
global_variable HDC      gMemDC = 0;
global_variable HBITMAP  gDIBSection = 0;
global_variable HBITMAP  gOldBitmap = 0;

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
global_variable ULONGLONG gLastStepMs = 0;
global_variable const ULONGLONG STEP_MS = 200; // move every 200 ms

// ==================== Backbuffer ====================

internal void ReleaseBackbuffer(void) {
    if (gMemDC) {
        if (gDIBSection) {
            SelectObject(gMemDC, gOldBitmap);
            DeleteObject(gDIBSection);
            gDIBSection = 0;
        }
        DeleteDC(gMemDC);
        gMemDC = 0;
    }
    BitmapMemory = 0;
}

internal void Win32ResizeDIBSection(int Width, int Height) {
    // Dispose old resources
    ReleaseBackbuffer();

    BitmapWidth = Width;
    BitmapHeight = Height;
    if (BitmapWidth <= 0)  BitmapWidth = 1;
    if (BitmapHeight <= 0) BitmapHeight = 1;

    // Describe a top-down 32bpp DIBSection
    ZeroMemory(&BitmapInfo, sizeof(BitmapInfo));
    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth = BitmapWidth;
    BitmapInfo.bmiHeader.biHeight = -BitmapHeight; // top-down (negative)
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;

    // Create memory DC and DIBSection
    gMemDC = CreateCompatibleDC(NULL);
    gDIBSection = CreateDIBSection(gMemDC, &BitmapInfo, DIB_RGB_COLORS, &BitmapMemory, NULL, 0);
    gOldBitmap = (HBITMAP)SelectObject(gMemDC, gDIBSection);
}

internal void ClearBackBuffer(uint32 color_one) {
    if (!BitmapMemory) return;
    uint32* pixels = (uint32*)BitmapMemory;
    int totalPixels = BitmapWidth * BitmapHeight;
    for (int i = 0; i < totalPixels; ++i) pixels[i] = color_one;
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
        for (int x = 0; x < (right - left); ++x) row[x] = color;
    }
}

internal void ComputeTileLayout(int* outTilePx, int* outOffsetX, int* outOffsetY) {
    int tileW = BitmapWidth / MAP_W;
    int tileH = BitmapHeight / MAP_H;
    int tile = (tileW < tileH) ? tileW : tileH;
    if (tile < 1) tile = 1;

    int usedW = tile * MAP_W;
    int usedH = tile * MAP_H;

    *outTilePx = tile;
    *outOffsetX = (BitmapWidth - usedW) / 2;
    *outOffsetY = (BitmapHeight - usedH) / 2;
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

    // Blit the memory DC (which holds the DIBSection) to the window DC in one go
    StretchBlt(
        DeviceContext,
        X, Y, WindowWidth, WindowHeight,
        gMemDC,
        0, 0, BitmapWidth, BitmapHeight,
        SRCCOPY
    );
}

// ==================== Helpers ====================

internal void FindPlayerOnMap(void) {
    for (int r = 0; r < MAP_H; ++r) {
        for (int c = 0; c < MAP_W; ++c) {
            if (gMap[r][c] == 2) {
                gPlayerRow = r;
                gPlayerCol = c;
                gMap[r][c] = ((r + c) & 1);  // restore checkerboard under player
                gSnake[0].r = r;
                gSnake[0].c = c;
                gSnakeLen = 1;
                return;
            }
        }
    }
    gPlayerRow = 0; gPlayerCol = 0;
    gSnake[0].r = 0; gSnake[0].c = 0; gSnakeLen = 1;
}

internal void FindFoodOnMap(void) {
    for (int r = 0; r < MAP_H; ++r) {
        for (int c = 0; c < MAP_W; ++c) {
            if (gMap[r][c] == 3) {
                gFoodRow = r;
                gFoodCol = c;
                gMap[r][c] = ((r + c) & 1); // restore checkerboard under food
                return;
            }
        }
    }
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
    do { // keep picking until it's not on the snake
        r = RandRange(0, MAP_H - 1);
        c = RandRange(0, MAP_W - 1);
    } while (SnakeOccupies(r, c));
    gFoodRow = r;
    gFoodCol = c;
}

// Return false if move hits walls/body; true otherwise.
// Handles growth if food is eaten and sets gGameOver on fatal collisions.
internal bool StepSnake(void) {
    if (gGameOver) return false;

    int nextR = gSnake[0].r + gDirRow;
    int nextC = gSnake[0].c + gDirCol;

    // Edge collision -> game over
    if (nextR < 0 || nextR >= MAP_H || nextC < 0 || nextC >= MAP_W) {
        gGameOver = true;
        return false;
    }

    bool ate = (nextR == gFoodRow && nextC == gFoodCol);

    // Self-collision check:
    // If NOT eating, the tail will vacate, so stepping into the current tail cell is allowed.
    // If eating, tail stays, so check all segments.
    int ignoreIndex = (!ate && gSnakeLen > 0) ? (gSnakeLen - 1) : -1;
    for (int i = 0; i < gSnakeLen; ++i) {
        if (i == ignoreIndex) continue;
        if (gSnake[i].r == nextR && gSnake[i].c == nextC) {
            gGameOver = true;
            return false;
        }
    }

    // Growth first (if needed) so memmove range includes the new length
    if (ate && gSnakeLen < SNAKE_MAX) {
        gSnakeLen += 1;
    }

    // Shift body back, open slot for new head
    if (gSnakeLen > 1) {
        memmove(&gSnake[1], &gSnake[0], sizeof(SNAKE_SEGMENT) * (gSnakeLen - 1));
    }

    // Place new head
    gSnake[0].r = nextR;
    gSnake[0].c = nextC;

    // Mirrors
    gPlayerRow = nextR;
    gPlayerCol = nextC;

    if (ate) {
        PlaceFoodRandomly();
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
    RenderRect(left, top, left + tile, top + tile, COLOR_FOOD);
}

internal void RenderGameOverOverlayToBackbuffer(void) {
    if (!gMemDC) return;

    const char* text = "GAME OVER";
    SIZE sz = { 0 };

    // Font (same as before)
    int dpiY = GetDeviceCaps(gMemDC, LOGPIXELSY);
    int ptSize = 48;
    int height = -MulDiv(ptSize, dpiY, 72);
    HFONT font = CreateFontA(
        height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT oldFont = font ? (HFONT)SelectObject(gMemDC, font) : 0;

    // Transparent text background (no filled box)
    SetBkMode(gMemDC, TRANSPARENT);

    // Optional: tiny drop shadow for legibility (comment out if you want pure text only)
    // Draw shadow first, 1px offset
    SetTextColor(gMemDC, RGB(0, 0, 0));
    GetTextExtentPoint32A(gMemDC, text, (int)lstrlenA(text), &sz);
    int cx = (BitmapWidth - sz.cx) / 2;
    int cy = (BitmapHeight - sz.cy) / 2;
    TextOutA(gMemDC, cx + 1, cy + 1, text, (int)lstrlenA(text));

    // Draw main white text on top (no background)
    SetTextColor(gMemDC, RGB(255, 255, 255));
    TextOutA(gMemDC, cx, cy, text, (int)lstrlenA(text));

    if (font) {
        SelectObject(gMemDC, oldFont);
        DeleteObject(font);
    }
}


// ==================== Window Proc ====================
LRESULT CALLBACK
Win32MainWindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) {
    switch (Message) {
    case WM_SIZE: {
        RECT ClientRect;
        GetClientRect(Window, &ClientRect);
        int Width = ClientRect.right - ClientRect.left;
        int Height = ClientRect.bottom - ClientRect.top;
        Win32ResizeDIBSection(Width, Height);
    } break;

    case WM_CLOSE: Running = false; break;
    case WM_DESTROY: Running = false; break;

    case WM_KEYDOWN: {
        // Direction changes only; ignore auto-repeat
        bool wasDown = (LParam & (1 << 30)) != 0;
        if (!wasDown && !gGameOver) {
            switch (WParam) {
            case 'W': if (gDirRow != 1) { gDirRow = -1; gDirCol = 0; } break;
            case 'S': if (gDirRow != -1) { gDirRow = +1; gDirCol = 0; } break;
            case 'A': if (gDirCol != 1) { gDirRow = 0;  gDirCol = -1; } break;
            case 'D': if (gDirCol != -1) { gDirRow = 0;  gDirCol = +1; } break;
            case VK_ESCAPE: PostQuitMessage(0); break;
            }
        }
        else if (!wasDown && gGameOver) {
            if (WParam == VK_ESCAPE) PostQuitMessage(0);
        }
    } break;

    case WM_ERASEBKGND:
        return 1; // prevent background erase to avoid flicker

    case WM_PAINT: {
        PAINTSTRUCT Paint;
        HDC dc = BeginPaint(Window, &Paint);
        RECT cr; GetClientRect(Window, &cr);
        Win32UpdateWindow(dc, &cr, 0, 0, cr.right - cr.left, cr.bottom - cr.top);
        EndPaint(Window, &Paint);
    } return 0;

    default:
        return DefWindowProc(Window, Message, WParam, LParam);
    }
    return 0;
}

// ==================== Entry ====================
int CALLBACK WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CommandLine, int ShowCode) {
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = Win32MainWindowCallback;
    wc.hInstance = Instance;
    wc.lpszClassName = "Win32WindowClass";

    if (!RegisterClassA(&wc)) return 0;

    HWND Window = CreateWindowExA(
        0, wc.lpszClassName, "Win32 Snake",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        0, 0, Instance, 0);

    if (!Window) return 0;

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
            if (Message.message == WM_QUIT) Running = false;
            TranslateMessage(&Message);
            DispatchMessageA(&Message);
        }

        // Auto-step movement on a fixed cadence (disable when game over)
        ULONGLONG now = GetTickCount64();
        if (!gGameOver && (now - gLastStepMs) >= STEP_MS) {
            StepSnake(); // will set gGameOver on collision
            gLastStepMs = now;
        }

        // Render into backbuffer
        ClearBackBuffer(COLOR_BG);
        RenderTileMap(gMap); // draws floor/walls (checkerboard)
        RenderSnake();       // draw head + body
        if (!gGameOver) {
            RenderFood();    // keep food off once game is over (optional)
        }
        else {
            RenderGameOverOverlayToBackbuffer(); // draw text into backbuffer (no flicker)
        }

        // Blit once per frame
        HDC dc = GetDC(Window);
        RECT client; GetClientRect(Window, &client);
        Win32UpdateWindow(dc, &client, 0, 0, client.right - client.left, client.bottom - client.top);
        ReleaseDC(Window, dc);
    }

    ReleaseBackbuffer();
    return 0;
}
