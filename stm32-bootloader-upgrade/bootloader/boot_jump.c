#include "boot_jump.h"
#include "main.h"
#include "fw_config.h"

int boot_app_valid(uint32_t app_addr)
{
    uint32_t sp = *(volatile uint32_t *)app_addr;
    uint32_t pc = *(volatile uint32_t *)(app_addr + 4U);

    /* 空片 */
    if ((sp == 0xFFFFFFFFU) || (pc == 0xFFFFFFFFU))
    {
        return 0;
    }
    /* 栈顶必须落在 RAM 区间内 */
    if ((sp < RAM_START_ADDR) || (sp > RAM_END_ADDR))
    {
        return 0;
    }
    /* 复位向量必须落在目标分区内部，且为 Thumb 地址（最低位为 1） */
    if ((pc < app_addr) || (pc >= (app_addr + FW_MAX_SIZE)))
    {
        return 0;
    }
    if ((pc & 0x1U) == 0U)
    {
        return 0;
    }
    return 1;
}

void boot_jump_to(uint32_t app_addr)
{
    uint32_t sp = *(volatile uint32_t *)app_addr;
    uint32_t pc = *(volatile uint32_t *)(app_addr + 4U);
    void (*app_entry)(void);
    uint32_t i;

    /* 1. 关总中断，清所有 NVIC 使能与挂起位，避免残留中断进 APP 后跑飞 */
    __disable_irq();
    for (i = 0U; i < 8U; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* 2. 关 SysTick（否则 APP 里 HAL_Init 可能会被旧计数值打断） */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    /* 3. 反初始化 Bootloader 用过的外设与时钟 */
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* 4. 重设栈顶与向量表基址。PRIMASK 不会因跳转复位，
     *    必须重新开中断，否则 APP 里所有中断永远不触发 */
    __set_MSP(sp);
    SCB->VTOR = app_addr;
    __enable_irq();

    app_entry = (void (*)(void))pc;
    app_entry();

    /* 正常不会返回；万一返回则死等，等待看门狗复位 */
    while (1)
    {
    }
}
