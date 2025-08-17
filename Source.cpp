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

typedef struct RECTANGLE {
	int x;
	int y;
	int w;
	int h;
} RECTANGLE;



global_variable bool Running = false;
global_variable BITMAPINFO BitmapInfo;
global_variable void* BitmapMemory;
global_variable int BitmapWidth;
global_variable int BitmapHeight;
global_variable int BytesPerPixel = 4;

global_variable RECTANGLE gRect;

internal void Win32ResizeDIBSection(int width, int height) {
	if (BitmapMemory) {
		VirtualFree(BitmapMemory, 0, MEM_RELEASE);
		BitmapMemory = 0;
	}

	BitmapWidth = width;
	BitmapHeight = height;

	BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
	BitmapInfo.bmiHeader.biWidth = BitmapWidth;
	BitmapInfo.bmiHeader.biHeight = -BitmapHeight; // top-down
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
		// Start of this row
		uint32* row = (uint32*)BitmapMemory + y * BitmapWidth + left;

		// Fill the row segment
		for (int x = 0; x < (right - left); ++x) {
			row[x] = color;
		}
	}
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

			gRect.x = 50;
			gRect.y = 50;
			gRect.w = 50;
			gRect.h = 50;

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

				ClearBackbuffer(0x00202020);
				int left = gRect.x;
				int top = gRect.y;
				int right = (gRect.x + gRect.w);
				int bottom = (gRect.y + gRect.h);
				RenderRect(left, top, right, bottom, 0x000000FF);

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