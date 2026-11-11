#ifndef BOOT_MAIN_H
#define BOOT_MAIN_H

/* Bootloader 主流程：上电判定 -> 固件搬运 / 回滚 -> 跳转 APP 或 Reset */
void boot_run(void);

#endif /* BOOT_MAIN_H */
