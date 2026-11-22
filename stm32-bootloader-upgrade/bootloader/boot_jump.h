#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdint.h>

/* 检查目标分区的固件合法性（栈顶指针 + 复位向量） */
int  boot_app_valid(uint32_t app_addr);

/* 跳转到指定分区，不返回 */
void boot_jump_to(uint32_t app_addr);

#endif /* BOOT_JUMP_H */
