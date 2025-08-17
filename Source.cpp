// Win32 Snake 

#include <windows.h>
#include <stdint.h>

bool Running = false;

LRESULT CALLBACK Win32MainWindowCallback(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam) {

	LRESULT result = 0;

	switch (Message) {
		case WM_SIZE: {
			OutputDebugStringA("WM_SIZE\n");
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
			PAINTSTRUCT ps; BeginPaint(Window, &ps); EndPaint(Window, &ps);
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