/* Win32 Snake */

#include <windows.h>
#include <stdint.h>

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

global_variable bool Running = false;
global_variable BITMAPINFO BitmapInfo;
global_variable void* BitmapMemory;
global_variable int BitmapWidth;
global_variable int BitmapHeight;
global_variable int BytesPerPixel = 4;

enum { TILE_SIZE = 32, MAP_W = 12, MAP_H = 8 };

#define COLOR_BG     0x00202020
#define COLOR_FLOOR  0x00303030
#define COLOR_WALL   0x006666AA
#define COLOR_PLAYER  0x0000AA00
#define COLOR_FOOD	  0x00FFA500	

global_variable int gMap[MAP_H][MAP_W] = {
	{0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
	{1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0},
	{0, 1, 0, 2, 0, 1, 0, 1, 0, 1, 0, 1},
	{1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0},
	{0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
	{1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0},
	{0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
	{1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0},
};

global_variable int gPlayerRow = 0;
global_variable int gPlayerCol = 0;

global_variable int gFoodRow = 1;
global_variable int gFoodCol = 1;

internal void Win32ResizeDIBSection(int width, int height) {
	if (BitmapMemory) {
		VirtualFree(BitmapMemory, 0, MEM_RELEASE);
		BitmapMemory = 0;
	}

	BitmapWidth = width;
	BitmapHeight = height;

	BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
	BitmapInfo.bmiHeader.biWidth = BitmapWidth;
	BitmapInfo.bmiHeader.biHeight = -BitmapHeight;
	BitmapInfo.bmiHeader.biPlanes = 1;
	BitmapInfo.bmiHeader.biBitCount = 32;
	BitmapInfo.bmiHeader.biCompression = BI_RGB;

	int BitmapMemorySize = (BitmapWidth * BitmapHeight) * BytesPerPixel;
	BitmapMemory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
}

internal void ClearBackbuffer(uint32 color) {
	if (!BitmapMemory) return;

	uint32* pixels = (uint32*)BitmapMemory;
	uint32 totalPixels = BitmapWidth * BitmapHeight;

	for (int i = 0; i < totalPixels; ++i) {
		pixels[i] = color;
	}
}

internal void RenderRect(int left, int top, int right, int bottom, uint32 color) {
	if (!BitmapMemory) return;

	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > BitmapWidth)  right = BitmapWidth;
	if (bottom > BitmapHeight) bottom = BitmapHeight;
	if (right <= left || bottom <= top) return;

	uint8* base = (uint8*)BitmapMemory;
	int pitch = BitmapWidth * BytesPerPixel;

	for (int y = top; y < bottom; ++y) {
		uint32* row = (uint32*)BitmapMemory + y * BitmapWidth + left;

		for (int x = 0; x < (right - left); ++x) {
			row[x] = color;
		}
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
			if (map[row][col] == 1) color = COLOR_WALL;
			else if (map[row][col] == 2) color = COLOR_PLAYER;

			RenderRect(left, top, right, bottom, color);
		}
	} 
}

internal void FindPlayerOnMap(void) {
	for (int r = 0; r < MAP_H; ++r) {
		for (int c = 0; c < MAP_W; ++c) {
			if (gMap[r][c] == 2) {
				gPlayerRow = r;
				gPlayerCol = c;

				gMap[r][c] = ((r + c) & 1);
				return;
			}
		}
	}
	gPlayerRow = 0; gPlayerCol = 0;
}

internal void TryMovePlayer(int dRow, int dCol) {
	int newR = gPlayerRow + dRow;
	int newC = gPlayerCol + dCol;

	if (newR < 0 || newR >= MAP_H || newC < 0 || newC >= MAP_W) return;

	gPlayerRow = newR;
	gPlayerCol = newC;
}

internal void RenderPlayer(void) {
	int tile, offX, offY;
	ComputeTileLayout(&tile, &offX, &offY);

	int left = offX + gPlayerCol * tile;
	int top = offY + gPlayerRow * tile;
	int right = left + tile;
	int bottom = top + tile;

	RenderRect(left, top, right, bottom, COLOR_PLAYER); 
}

internal void FindFoodOnMap(void) {
	for (int r = 0; r < MAP_H; ++r) {
		for (int c = 0; c < MAP_H; ++c) {
			if (gMap[r][c] == 3) {
				gFoodRow = r;
				gFoodCol = c;
				gMap[r][c] = ((r + c) & 1);
				return;
			}
		}
	}
	gFoodRow = 1; gFoodCol = 1;
}

internal int RandRange(int lo, int hi) {
	return lo + (rand() % (hi - lo + 1));
}

internal void PlaceFoodRandomly(void) {
	int r, c;
	do {
		r = RandRange(0, MAP_H - 1);
		c = RandRange(0, MAP_W - 1);
	} while (r == gPlayerRow && c == gPlayerCol);
	gFoodRow = r;
	gFoodCol = c;
}

internal void CheckPlayerFoodCollision(void) {
	if (gPlayerRow == gFoodRow && gPlayerCol == gFoodCol) {
		PlaceFoodRandomly();
	}
}

internal void RenderFood(void) {
	int tile, offX, offY;
	ComputeTileLayout(&tile, &offX, &offY);

	int left = offX + gFoodCol * tile;
	int top = offY + gFoodRow * tile;
	int right = left + tile;
	int bottom = top + tile;

	RenderRect(left, top, right, bottom, COLOR_FOOD);
}

internal void Win32UpdateWindow(HDC DeviceContext, RECT *ClientRect, int x, int y, int width, int height) {

	int WindowWidth = ClientRect->right - ClientRect->left;
	int WindowHeight = ClientRect->bottom - ClientRect->top;

	StretchDIBits(
		DeviceContext,
		x, y, WindowWidth, WindowHeight,
		x, y, BitmapWidth, BitmapHeight,
		BitmapMemory,
		&BitmapInfo,
		DIB_RGB_COLORS, SRCCOPY
	);
}

LRESULT CALLBACK Win32MainWindowCallback(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam) {

	LRESULT result = 0;

	switch (Message) {
		case WM_SIZE: {
			RECT ClientRect;
			GetClientRect(Window, &ClientRect);
			int Width = ClientRect.right - ClientRect.left;
			int Height = ClientRect.bottom - ClientRect.top;
			Win32ResizeDIBSection(Width, Height);
		} break;
		case WM_CLOSE: {
			DestroyWindow(Window);               
		} break;
		case WM_ACTIVATEAPP: {
			OutputDebugStringA("WM_ACTIVATE\n");
		} break;
		case WM_DESTROY: {
			PostQuitMessage(0);                  
		} break;
		case WM_KEYDOWN: {
			bool wasDown = (lParam & (1 << 30)) != 0;
			if (!wasDown) {
				switch (wParam) {
				case 'W': TryMovePlayer(-1, 0); CheckPlayerFoodCollision(); break;
				case 'S': TryMovePlayer(1, 0);  CheckPlayerFoodCollision(); break;
				case 'A': TryMovePlayer(0, -1); CheckPlayerFoodCollision(); break;
				case 'D': TryMovePlayer(0, 1); CheckPlayerFoodCollision(); break;
				case VK_ESCAPE: PostQuitMessage(0); break;
				}
			}
		} break;
		case WM_PAINT: {
			PAINTSTRUCT ps; 
			HDC DeviceContext = BeginPaint(Window, &ps); 
			int x = ps.rcPaint.left;
			int y = ps.rcPaint.top;
			int width = ps.rcPaint.right - ps.rcPaint.right;
			int height = ps.rcPaint.bottom - ps.rcPaint.top;

			RECT ClientRect;
			GetClientRect(Window, &ClientRect);
			Win32UpdateWindow(DeviceContext, &ClientRect, x, y, width, height);
			
			EndPaint(Window, &ps);
			OutputDebugStringA("WM_PAINT\n");
		} break;
		default: {
			OutputDebugStringA("default\n");
			result = DefWindowProc(Window, Message, wParam, lParam);
		} break;
	}
	return (result);
}

int CALLBACK WinMain(
	HINSTANCE Instance,
	HINSTANCE PrevInstance,
	LPSTR CommandLine,
	int ShowCode
) {
	WNDCLASS WindowClass = {};

	WindowClass.lpfnWndProc = Win32MainWindowCallback;
	WindowClass.hInstance = Instance;
	WindowClass.lpszClassName = "Win32 Snake";

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

			RECT cr;
			GetClientRect(Window, &cr);
			Win32ResizeDIBSection(cr.right - cr.left, cr.bottom - cr.top);

			FindPlayerOnMap();

			Running = true;
			while (Running) {
				MSG Message;
				while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE)) {
					if (Message.message == WM_QUIT) {
						Running = false;
					}
					TranslateMessage(&Message);
					DispatchMessageA(&Message);
				}

				ClearBackbuffer(COLOR_BG);
				RenderTileMap(gMap);
				RenderPlayer();
				RenderFood();

				HDC DeviceContext = GetDC(Window);
				RECT ClientRect;
				GetClientRect(Window, &ClientRect);
				int WindowWidth = ClientRect.right - ClientRect.left;
				int WindowHeight = ClientRect.bottom - ClientRect.top;
				Win32UpdateWindow(DeviceContext, &ClientRect, 0, 0, WindowWidth, WindowHeight);
				ReleaseDC(Window, DeviceContext);
			}
		}
		else {
			// logging
		}
	}
	else {
		// logging
	}

	return(0);

}