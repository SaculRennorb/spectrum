#include <stdarg.h>
#include "basetypes.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "platform.h"
#include "platform_win32.cpp"

struct CharacterData {
	char character;
	u32* data;
	i32  w;
	i32  h;
	i32  offset_x;
	i32  offset_y;
};

global const u32     s_character_data_lut_size = 64;
global CharacterData s_character_data_lut[s_character_data_lut_size];
global u32           s_glyph_memory[1024*1024];

global const u32 LINE_HEIGHT = 20;
global const u32 FONT_HEIGHT = 16;

CharacterData GetOrLoadCharacterData(char c)
{
	static u32 next_entry = 0;
	for(u32 i = 0; i < s_character_data_lut_size; i++) {
		if(s_character_data_lut[i].character == c) 
			return s_character_data_lut[i];
	}

	FileMemory font_file = read_entire_file("C:/Windows/Fonts/arial.ttf");

	stbtt_fontinfo font;
	stbtt_InitFont(&font, (u8*)font_file.memory, stbtt_GetFontOffsetForIndex((u8*)font_file.memory, 0));

	CharacterData entry = {};
	u8* monochrome_bitmap = stbtt_GetCodepointBitmap(&font, 0, stbtt_ScaleForPixelHeight(&font, FONT_HEIGHT), c, &entry.w, &entry.h, &entry.offset_x, &entry.offset_y);
	
	static u32 next_glyph_offset = 0;
	entry.character = c;
	entry.data = s_glyph_memory + next_glyph_offset;
	u8* src = monochrome_bitmap;
	for(i32 y = entry.h - 1; y >= 0; y--) {
		u32* dst_row = entry.data + entry.w * y;
		for(i32 x = 0; x < entry.w; x++) {
			u8 alpha = 255 - *src++;
			*dst_row++ = (alpha << 24) | (alpha << 16) | (alpha << 8) | alpha;
		}
	}
	next_glyph_offset += entry.w * entry.h;

	s_character_data_lut[next_entry] = entry;
	next_entry = (next_entry + 1) % s_character_data_lut_size;

	stbtt_FreeBitmap(monochrome_bitmap, 0);
	free_file(font_file);
	return entry;
}

void render_text(Win32Buffer* buffer, u32 x, u32 y, s8 text)
{
	u32 x_offset = x;
	for(u32 pos = 0; pos < text.length; pos++) {
		if(text.data[pos] == '\n') {
			y -= LINE_HEIGHT;
			x_offset = x;
			continue;
		}
		if(text.data[pos] == '\t') {
			x_offset += 10;
			continue;
		}
		if(text.data[pos] == ' ') {
			x_offset += 8;
			continue;
		}

		CharacterData c = GetOrLoadCharacterData(text.data[pos]);
		u32* char_bitmap = c.data;
		u32 inter_bitmap_y_offset = FONT_HEIGHT - c.h - c.offset_y;
		u8* dst_memory_start = (u8*)buffer->memory + (y + inter_bitmap_y_offset) * buffer->stride + x_offset * 4;
		for(u32 cy = 0; cy < c.h; cy++) {
			u32* row = (u32*)(dst_memory_start + cy * buffer->stride);
			for(u32 cx = 0; cx < c.w; cx++) {
				*row++ = *c.data++;
			}
		}
		x_offset += c.w;
	}
}

global char s_characters_lut[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

s8 do_format(s8 format, s8 dst, va_list args)
{
	u32 dst_pos = 0;
	u32 format_pos = 0;
	while(dst_pos < dst.length && format_pos < format.length) {
		if(format.data[format_pos] == '%') {
			format_pos++;
			switch(format.data[format_pos]) {
				case 'd': {
					i32 arg = va_arg(args, i32);
					i32 mod = 10;
					i32 digits_required = 1;
					while(arg >= mod) {
						digits_required++;
						mod *= 10;
					}
					i32 write_offset = digits_required;
					do {
						dst.data[dst_pos + --write_offset] = s_characters_lut[arg % 10];
						arg /= 10;
					} while(write_offset > 0);
					dst_pos += digits_required;
				} break;

				case '%': {
					dst.data[dst_pos++] = '%';
				} break;

				default: {
					char tmp[2] = { format.data[format_pos] };
					OutputDebugString("unknown format option '");
					OutputDebugString(tmp);
					OutputDebugString("'\n");
				} break;
			}
			format_pos++;
		}
		else {
			dst.data[dst_pos++] = format.data[format_pos++];
		}
	}

	dst.length = dst_pos;
	return dst;
}
s8 format(s8 format, s8 dst, ...)
{
	va_list args;
	va_start(args, dst);
	s8 result = do_format(format, dst, args);
	va_end(args);
	return result;
}
