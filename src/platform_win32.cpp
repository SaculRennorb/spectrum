#pragma once
#include <windows.h>
#include <initguid.h>
#include <dsound.h>
#include "basetypes.h"

void* r_allocate(u32 size_bytes) { return VirtualAlloc(0, size_bytes, MEM_COMMIT, PAGE_READWRITE); }
void r_free(void* memory) { VirtualFree(memory, 0, MEM_RELEASE); }

struct Win32Buffer {
	BITMAPINFO info;
	void*      memory;
	u32        w;
	u32        h;
	u32        stride;
};
#define RenderBuffer Win32Buffer

struct Win32CaptureDevice {
	LPDIRECTSOUNDCAPTUREBUFFER capture_buffer;
	u32                        capture_buffer_size;
	u32                        current_capture_read_progress;
	i16*                       samples_buffer;
	f32*                       spectrum_buffer;
};

FileMemory read_entire_file(char* filename)
{

	HANDLE file = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if(file != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER size;
		if(GetFileSizeEx(file, &size)) {
			FileMemory result = {};
			result.size = size.QuadPart;
			result.memory = VirtualAlloc(0, result.size, MEM_COMMIT, PAGE_READWRITE);
			//TODO(Rennorb): loop for actual 64 bit read
			if(ReadFile(file, result.memory, size.QuadPart, 0, 0)) {
				return result;
			}
		}

		CloseHandle(file);
	}

	return {};
}

void free_file(FileMemory file)
{
	VirtualFree(file.memory, 0, MEM_RELEASE);
}

///////////////////////////////////////////////////////////
//                    Platform Main                      //
///////////////////////////////////////////////////////////

global u8          s_running;
global Win32Buffer s_backbuffer = {
	.info = {
		.bmiHeader = {
			.biSize        = sizeof(s_backbuffer.info.bmiHeader),
			.biPlanes      = 1,
			.biBitCount    = 32,
			.biCompression = BI_RGB,
		}
	}
};

void init();
void window_resized(u32 w, u32 h);
void key_down(u32 key_code);
void update();
void render(RenderBuffer* buffer);
void deinit();

void redraw_window(HDC device_context, RECT window_rect, u32 x, u32 y, u32 w, u32 h)
{
	StretchDIBits(device_context,
		0, 0, s_backbuffer.w, s_backbuffer.h, // src
		0, 0, window_rect.right - window_rect.left, window_rect.bottom - window_rect.top, //dst
		s_backbuffer.memory,
		&s_backbuffer.info,
		DIB_RGB_COLORS, SRCCOPY
	);
}

void draw(HWND window)
{
	render(&s_backbuffer);

	HDC device_context = GetDC(window);
	RECT client_rect;
	GetClientRect(window, &client_rect);
	u32 w = client_rect.right - client_rect.left;
	u32 h = client_rect.bottom - client_rect.top;
	redraw_window(device_context, client_rect, 0, 0, w, h);
	ReleaseDC(window, device_context);
}

void resize_dib_section(Win32Buffer* buffer, u32 w, u32 h)
{
	u32 bytes_per_pixel = buffer->info.bmiHeader.biBitCount / 8;

	buffer->info.bmiHeader.biWidth  = w;
	buffer->info.bmiHeader.biHeight = h;
	buffer->w = w;
	buffer->h = h;
	buffer->stride = w * bytes_per_pixel;

	u32 bitmap_memory_size = w * h * bytes_per_pixel;
	replace_memory(&buffer->memory, bitmap_memory_size);

	window_resized(w, h);
}

LRESULT CALLBACK MainWindowCallback(
	HWND   window,
	UINT   message,
	WPARAM wParam,
	LPARAM lParam
) {
	LRESULT result = 0;
	switch(message) {
		case WM_SIZE: {
			RECT client_rect;
			GetClientRect(window, &client_rect);
			
			u32 w = client_rect.right - client_rect.left;
			u32 h = client_rect.bottom - client_rect.top;
			resize_dib_section(&s_backbuffer, w, h);

			render(&s_backbuffer);
		} break;

		case WM_DESTROY: {
			s_running = false;
		} break;

		case WM_CLOSE: {
			s_running = false;
		} break;

		case WM_ACTIVATEAPP: {
			result = DefWindowProc(window, message, wParam, lParam);
		} break;

		case WM_KEYDOWN: {
			

			result = DefWindowProc(window, message, wParam, lParam);
		} break;

		case WM_MOUSEMOVE: {
			p2 p {
				.x = (u32)(lParam & 0x0000ffff),
				.y = s_backbuffer.h - (u32)(lParam >> 16),
			};
			s_mouse_pos = p;
		} break;

		case WM_PAINT: {
			PAINTSTRUCT paint;
			HDC device_context = BeginPaint(window, &paint);

			u32 x = paint.rcPaint.left;
			u32 y = paint.rcPaint.top;
			u32 height = paint.rcPaint.bottom - paint.rcPaint.top;
			u32 width  = paint.rcPaint.right - paint.rcPaint.left;

			RECT client_rect;
			GetClientRect(window, &client_rect);

			redraw_window(device_context, client_rect, x, y, width, height);

			EndPaint(window, &paint);
		} break;

		default: {
			result = DefWindowProc(window, message, wParam, lParam);
		} break;
	}

	return result;
}

int CALLBACK WinMain(
	HINSTANCE instance,
	HINSTANCE previous_instance,
	PSTR      command_line,
	INT       show_command
) {
	WNDCLASS window_class = {
		.style         = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc   = MainWindowCallback,
		.hInstance     = instance,
		//.hIcon         = ,
		//.lpszMenuName  = ,
		.lpszClassName = "SpectrumMainWindow",
	};
	

	if(!RegisterClass(&window_class)) {
		/**debug*/
		return 1;
	}

	HWND window = CreateWindowEx(0, window_class.lpszClassName,
		"Spectrum",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		0, 0, instance, 0
	);
	if(!window) {
		/*debug*/
		DWORD error = GetLastError();
		return 2;
	}

	init();
	
	s_running = true;
	MSG message;
	while(s_running) {
		while(s_running && PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
			if(message.message == WM_QUIT)
				s_running = false;
			TranslateMessage(&message);
			DispatchMessage(&message);
		}

		update();
		draw(window);
	}

	deinit();

	return 0;
}