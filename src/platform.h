#pragma once
#include "basetypes.h"

void* r_allocate(u32 size_bytes);
void r_free(void* memory);
void replace_memory(void** target_var, u32 new_bytes_size) {
	void* new_memory = r_allocate(new_bytes_size);
	if(new_memory) {
		void* old_memory = *target_var;
		*target_var = new_memory;
		r_free(old_memory);
	}
}

struct FileMemory {
	void* memory;
	i64   size;
};
FileMemory read_entire_file(char* filename);
FileMemory read_entire_file(const char filename[]) { return read_entire_file((char*)filename); }
void free_file(FileMemory file);

struct RenderBuffer;

global p2 s_mouse_pos;
