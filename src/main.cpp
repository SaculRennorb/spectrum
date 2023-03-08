
#include "basetypes.h"
#include "text.cpp"
#include "platform_win32.cpp"

#undef global

#include <assert.h>
#include <complex.h>
#include <fftw3.h>

#define global static

struct FFTWData {
	fftw_complex* in;
	fftw_complex* out;
	fftw_plan     plan;
};

global const u32          MAX_CAPTURE_DEVICES  = 8;
global const u32          s_samples_per_second = 44100;
global u32                s_buffered_seconds   = 5;
global i32                s_device_count = 0;
global Win32CaptureDevice s_capture_devices[MAX_CAPTURE_DEVICES];
global u32*               s_max_spectrum_values;
global u32*               s_max_sample_values;
global u32                s_device_colors[MAX_CAPTURE_DEVICES] = { 0x000000ff, 0x0000ff00, 0x00ff0000, 0x000000ff, 0x0000ff00, 0x00ff0000, 0x000000ff, 0x0000ff00 };

global const u32 s_fft_buckets = s_samples_per_second / 4;
global FFTWData  s_fftw_buffers[MAX_CAPTURE_DEVICES];

global u32         s_computed_frequency_max = ((s_fft_buckets - 1.0f) / s_fft_buckets * s_samples_per_second) / 2;
global u32         s_src_frequency_min      = 0;
global ConfigValue s_src_frequency_max      = {
	.min     = 100,
	.current = (f32)s_computed_frequency_max,
	.max     = (f32)s_computed_frequency_max,
};
global ConfigValue s_spectrum_amplification = {
	.min     = 0.0001f,
	.current = 1.0f,
	.max     = 100.0f,
};

global u32*      s_waterfall_output_row_buffer;

global ConfigValue s_max_sample_abs = {
	.min     = 1.0f,
	.current = 32767.0f,
	.max     = 32767.0f,
};
global u32         s_topmost_spectrum = 0;

u32 limit(u32 value, u32 max)
{
	return value > max ? max : value;
}

void window_resized(u32 w, u32 h)
{
	for(u32 i = 0; i < MAX_CAPTURE_DEVICES; i++) {
		//if(!s_capture_devices[i].capture_buffer) continue; //TODO(Rennorb) @performance
		replace_memory((void**)&s_capture_devices[i].spectrum_buffer, w * sizeof(f32));
	}
	
	replace_memory((void**)&s_max_spectrum_values, w * sizeof(u32));
	replace_memory((void**)&s_max_sample_values, w * sizeof(u32));
	replace_memory((void**)&s_waterfall_output_row_buffer, w * sizeof(u32));
}

void key_down(u32 key_code)
{
	switch(key_code) {
		case VK_DOWN: {
			s_max_sample_abs.current = cf_double(s_max_sample_abs);
		} break;
		
		case VK_UP: {
			s_max_sample_abs.current = cf_halve(s_max_sample_abs);
		} break;

		case VK_LEFT: {
			s_topmost_spectrum = (s_topmost_spectrum - 1) % s_device_count;
		} break;

		case VK_RIGHT: {
			s_topmost_spectrum = (s_topmost_spectrum + 1) % s_device_count;
		} break;

		case 0x4E: { //N
			s_src_frequency_max.current = cf_halve(s_src_frequency_max);
		} break;

		case 0x4D: { // M
			s_src_frequency_max.current = cf_double(s_src_frequency_max);
		} break;

		case VK_OEM_COMMA: {
			s_spectrum_amplification.current = cf_halve(s_spectrum_amplification);
		} break;

		case VK_OEM_PERIOD: {
			s_spectrum_amplification.current = cf_double(s_spectrum_amplification);
		} break;
	}
}

void update()
{
	for(u32 d = 0; d < MAX_CAPTURE_DEVICES; d++) {
		Win32CaptureDevice& device = s_capture_devices[d];
		if(!device.capture_buffer) continue;

		FFTWData& fftw = s_fftw_buffers[d];
	
		DWORD capture_pos;
		DWORD read_pos;
		if(FAILED(device.capture_buffer->GetCurrentPosition(&capture_pos, &read_pos))) {
			return;
		}
		assert(read_pos % 2 == 0);
		device.current_capture_read_progress = read_pos;

		u32 block_size = s_fft_buckets * 2;
		if(read_pos % block_size != 0) {
			read_pos = read_pos / block_size * block_size;
		}

		static DWORD last_read_pos = -1;
		if(read_pos != last_read_pos) {
			last_read_pos = read_pos;
		
			i16* current_buffer_segment = device.samples_buffer + read_pos / 2;

			{
				u32 copy_size = s_fft_buckets * 2;
				LPVOID audio_memory_1;
				DWORD  audio_memory_1_len;
				LPVOID audio_memory_2;
				DWORD  audio_memory_2_len;
				if(FAILED(device.capture_buffer->Lock((read_pos - copy_size) % device.capture_buffer_size, copy_size, &audio_memory_1, &audio_memory_1_len, &audio_memory_2, &audio_memory_2_len, 0 /*DSCBLOCK_ENTIREBUFFER*/))) {
					OutputDebugString("lock error");
					return;
				}
				assert(audio_memory_1_len + audio_memory_2_len == copy_size);

				memcpy(current_buffer_segment, audio_memory_1, audio_memory_1_len);
				memcpy((u8*)current_buffer_segment + audio_memory_1_len, audio_memory_2, audio_memory_2_len);

				device.capture_buffer->Unlock(audio_memory_1, audio_memory_1_len, audio_memory_2, audio_memory_2_len);
			}

			for(u32 i = 0; i < s_fft_buckets; i++) {
				fftw.in[i][0] = current_buffer_segment[i];
			}

			fftw_execute(fftw.plan);
		}
	}
}

void render(RenderBuffer* buffer)
{
	memset(s_max_sample_values, 0, buffer->w * sizeof(u32));
	memset(s_max_spectrum_values, 0, buffer->w * sizeof(u32));
	memset(s_waterfall_output_row_buffer, 0, buffer->w * sizeof(u32));

	bool at_least_one = false;
	bool update_waterfall = false;
	for(u32 dd = 0; dd < s_device_count; dd++) {
		u32 d = (dd + s_topmost_spectrum) % s_device_count;
		Win32CaptureDevice device = s_capture_devices[d];
		if(!device.capture_buffer) continue;
		at_least_one = true;

		FFTWData fftw = s_fftw_buffers[d];

		static u32 last_read_pos[MAX_CAPTURE_DEVICES] = {};
		if(device.current_capture_read_progress != last_read_pos[d]) {
			last_read_pos[d] = device.current_capture_read_progress;

			{
				u32 buckets = s_fft_buckets / 2;
				f32 scale = (f32)(s_src_frequency_max.current - s_src_frequency_min) / s_computed_frequency_max;
				f32 offset = (f32)s_src_frequency_min / s_computed_frequency_max;
				f32 block_length = (f32)buckets / buffer->w * scale;
				for(u32 i = 0; i < buffer->w; i++) {
					u32 first_freq = buckets * offset + block_length * i;
					u32 last_freq  = buckets * offset + block_length * (i + 1);
					if(last_freq == first_freq) last_freq = first_freq + 1;
					f32 intensity_f = 0;
					for(u32 j = first_freq; j < last_freq; j++) {
						intensity_f += sqrt(fftw.out[j][0] * fftw.out[j][0] + fftw.out[j][1] * fftw.out[j][1]);
					}
					f32 new_value = intensity_f / (s_fft_buckets * block_length) * s_spectrum_amplification.current;
					// fade effect
					device.spectrum_buffer[i] = max(new_value, device.spectrum_buffer[i] * 0.95f);
				}
			}

			{
				for(u32 x = 0; x < buffer->w; x++) {
					u32 intensity = limit((u32)(device.spectrum_buffer[x] * 255), 255) << ((d * 8) % 16);
					s_waterfall_output_row_buffer[x] |= intensity;
				}
				update_waterfall = true;
			}
		}

		{
			u32 quad_height = buffer->h / 4;
			u8* spectrum_section = (u8*)buffer->memory + quad_height * 2 * buffer->stride;
			for(u32 x = 0; x < buffer->w; x++) {
				u32 loudness = limit(device.spectrum_buffer[x] * quad_height, quad_height);
				for(u32 y = s_max_spectrum_values[x]; y < loudness; y++) {
					((u32*)(spectrum_section + y * buffer->stride))[x] = s_device_colors[d];
				}
				s_max_spectrum_values[x] = max(s_max_spectrum_values[x], loudness);
			}
		}

		{
			u32 quad_height = buffer->h / 4;
			u8* upper_pixel_quad = (u8*)buffer->memory + quad_height * 3 * buffer->stride;
			f32 samples_per_pixel = device.capture_buffer_size / 2.0f / buffer->w;
			for(u32 x = 0; x < buffer->w; x++) {
				u32 loudness = limit(abs(device.samples_buffer[(u32)(x * samples_per_pixel)]) / s_max_sample_abs.current * quad_height, quad_height);
				for(u32 y = s_max_sample_values[x]; y < loudness; y++) {
					((u32*)(upper_pixel_quad + y * buffer->stride))[x] = s_device_colors[d];
				}
				s_max_sample_values[x] = max(s_max_sample_values[x], loudness);
			}
		}

		{
			u8* line_pixels = (u8*)buffer->memory + (2 + d * 5) * buffer->stride;
			u32 x = 0;
			u32 buffer_pos = (f32)device.current_capture_read_progress / device.capture_buffer_size * buffer->w;
			for(; x < buffer_pos; x++) {
				((u32*)line_pixels)[x] = s_device_colors[d];
			}
			for(; x < buffer->w; x++) {
				((u32*)line_pixels)[x] = 0;
			}
		}
	}

	//fill empty space after we are done with other stuff
	if(at_least_one) {
		u32 quad_height = buffer->h / 4;
		u8* upper_pixel_quad = (u8*)buffer->memory + quad_height * 3 * buffer->stride;
		u8* spectrum_section = (u8*)buffer->memory + quad_height * 2 * buffer->stride;
		for(u32 x = 0; x < buffer->w; x++) {
			for(u32 y = s_max_spectrum_values[x]; y < quad_height; y++) {
				((u32*)(spectrum_section + y * buffer->stride))[x] = 0x00ffffff;
			}
			for(u32 y = s_max_sample_values[x]; y < quad_height; y++) {
				((u32*)(upper_pixel_quad + y * buffer->stride))[x] = 0x00ffffff;
			}
		}

		//red block lines
		u32 slices = (s_samples_per_second * s_buffered_seconds) / s_fft_buckets;
		for(u32 i = 0; i < slices; i++) {
			u32 x = i * buffer->w / slices;
			for(u32 y = 0; y < quad_height; y++) {
				((u32*)(upper_pixel_quad + y * buffer->stride))[x] = 0x00ff0000;
			}
		}

		if(update_waterfall) {
			static u32 dst_row = 0;
			for(u32 x = 0; x < buffer->w; x++) {
				u32 color = s_waterfall_output_row_buffer[x];
				((u32*)((u8*)buffer->memory + dst_row * buffer->stride))[x] = color;
			}
			dst_row = (dst_row + 1) % (buffer->h / 2);
		}
	}
	else {
		u8* row = (u8*)buffer->memory;
		for(int y = 0; y < buffer->h; y++, row += buffer->stride) {
			u32* pixel = (u32*)row;
			for(int x = 0; x < buffer->w; x++, pixel++) {
				u8 b = (u8)(255.0f * x / buffer->w);
				u8 g = (u8)(255.0f * y / buffer->h);
				*pixel = (50 << 16) | (g << 8) | b;
			}
		}
	}

	char b[64]= {};
	s8 text = to_s(b);

	u32 spectrogram_end_height = buffer->h * 3 / 4; 
	if(s_mouse_pos.x && s_mouse_pos.y && s_mouse_pos.y < spectrogram_end_height) {
		f32 x_percent = (f32)s_mouse_pos.x / buffer->w;
		u32 hertz = (s_src_frequency_max.current - s_src_frequency_min) * x_percent;
		s8 text4 = format(to_s("%d Hz"), text, hertz);
		for(u32 y = buffer->h / 2; y < spectrogram_end_height; y++) {
			((u32*)((u8*)buffer->memory + y * buffer->stride))[s_mouse_pos.x] = 0x00ff0000;
		}
		render_text(buffer, s_mouse_pos.x, buffer->h / 2, text4);
	}

	render_text(buffer, 20, buffer->h - 20, to_s(R"x(
key binds:
	UP / DOWN : scale input wave form display
	LEFT / RIGHT : cycle topmost audio source
	N / M : decrease / increase spectrum width
	COMMA / DOT : scale spectrum width
)x"));
	
	{
		u32 line_pos = 0;
		s8 text1 = format(to_s("spectrum range min: %dHz"), text, (i32)s_src_frequency_min);
		render_text(buffer, 20, line_pos += 20, text1);
		s8 text2 = format(to_s("spectrum range max: %dHz"), text, (i32)s_src_frequency_max.current);
		render_text(buffer, 20, line_pos += 20, text2);
		s8 text3 = format(to_s("spectrum amplification: %d%%"), text, (i32)(s_spectrum_amplification.current * 100));
		render_text(buffer, 20, line_pos += 20, text3);
	}
}

BOOL CALLBACK DSEnumCallback(LPGUID guid, LPCTSTR description, LPCTSTR driver_name, LPVOID context)
{
	OutputDebugString(description);
	OutputDebugString(" | ");
	OutputDebugString(driver_name);
	OutputDebugString("\n");

	//NOTE(Rennorb): the first device is always the 'default 'device, so we just skip that one
	//TODO(Rennorb) @stability: check if this works with no mike attached
	if(s_device_count < 0) {
		s_device_count++;
		return true;
	}
	Win32CaptureDevice& device = s_capture_devices[s_device_count];
	FFTWData& fftw = s_fftw_buffers[s_device_count];

	s_device_count++;

	fftw.in   = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * s_fft_buckets);
	fftw.out  = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * s_fft_buckets);
	fftw.plan = fftw_plan_dft_1d(s_fft_buckets, fftw.in, fftw.out, FFTW_FORWARD, FFTW_ESTIMATE);
	memset(fftw.in, 0, sizeof(fftw_complex) * s_fft_buckets);

	LPDIRECTSOUNDCAPTURE capture_interface;
	if(FAILED(DirectSoundCaptureCreate(guid, &capture_interface, 0))) {
		exit(4);
	}

#if 0 // maybe later
	{
		DSCCAPS caps;
		if(FAILED(capture_interface->GetCaps(&caps)) {
			exit(5);
		}
		//...
	}
#endif

	
	WAVEFORMATEX  wfx = {};
	wfx.wFormatTag      = WAVE_FORMAT_PCM;
	wfx.nChannels       = 1;
	wfx.nSamplesPerSec  = s_samples_per_second;
	wfx.wBitsPerSample  = 16;
	wfx.nBlockAlign     = (wfx.nChannels * wfx.wBitsPerSample) / 8,
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
	DSCBUFFERDESC buffer_descriptor = {
		.dwSize        = sizeof(DSCBUFFERDESC),
		.dwBufferBytes = wfx.nAvgBytesPerSec * s_buffered_seconds,
		.lpwfxFormat   = &wfx,
	};

	LPDIRECTSOUNDCAPTUREBUFFER capture_buffer;
	if(FAILED(capture_interface->CreateCaptureBuffer(&buffer_descriptor, &capture_buffer, 0))) {
		exit(6);
	}

	capture_buffer->QueryInterface(IID_IDirectSoundCaptureBuffer, (LPVOID*)&device.capture_buffer);
	capture_buffer->Release();

	DSCBCAPS buffer_caps = { .dwSize = sizeof(buffer_caps) };
	device.capture_buffer->GetCaps(&buffer_caps);
	device.capture_buffer_size = buffer_caps.dwBufferBytes;

	device.samples_buffer = (i16*)r_allocate(buffer_caps.dwBufferBytes);

	device.capture_buffer->Start(DSCBSTART_LOOPING);

	return s_device_count < MAX_CAPTURE_DEVICES; // false = stop enumeration
}

void init()
{
	s_device_count = -1;
	if(FAILED(DirectSoundCaptureEnumerate(DSEnumCallback, 0))) {
		exit(3);
	}
}

void deinit()
{
	for(u32 i = 0; i < MAX_CAPTURE_DEVICES; i++)
		if(s_capture_devices[i].capture_buffer)
			s_capture_devices[i].capture_buffer->Stop();
}
