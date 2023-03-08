#pragma once
#include <stdint.h>

typedef uint8_t  u8;

typedef uint16_t u16;
typedef  int16_t i16;

typedef uint32_t u32;
typedef  int32_t i32;
typedef float    f32;

typedef uint32_t u32;
typedef  int64_t i64;
typedef double   f64;

#define global static;

struct s8 {
	u32   length;
	char* data;
};

template<u32 length>
inline s8 to_s(const char (& characters)[length]) { return {length - 1, (char *)&characters[0]}; }

struct p2 {
	u32 x, y;
};

struct ConfigValue {
	f32 min, current, max;
};

f32 cf_double(ConfigValue config)
{
	f32 new_value = config.current * 2;
	if(new_value > config.max) return config.max;
	return new_value;
}
f32 cf_halve(ConfigValue config)
{
	f32 new_value = config.current / 2;
	if(new_value < config.min) return config.min;
	return new_value;
}
