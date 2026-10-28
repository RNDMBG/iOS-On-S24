#ifndef E1S_MENU_CONFIG_H
#define E1S_MENU_CONFIG_H

#include <stdint.h>

#define MENU_CONFIG_MAGIC "E1SMENU1"
#define MENU_CONFIG_VERSION 1U
#define MENU_CONFIG_OFFSET 0x20000UL
#define MENU_FLAG_KEY 1U

struct menu_config {
	char magic[8];
	uint32_t version;
	uint32_t flags;
	uint64_t stock_off;
	uint64_t stock_size;
	uint64_t stock_image_size;
	uint64_t xnu_off;
	uint64_t xnu_size;
	uint64_t total_size;
	uint64_t memory_size;
	uint32_t default_ms;
	uint32_t menu_ms;
	uint8_t reserved[8];
};

#endif
