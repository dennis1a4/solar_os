#if SK_UPSTREAM_SHELL && SK_HW_RESOURCES && SK_LCD_CONSOLE
#include <Arduino.h>
extern "C" void sk_power_button_event();
static void button_isr() {
    if(SNVS_HPSR & 0x40) {
        SNVS_HPCOMR |= 1UL<<31;
        SNVS_LPSR = (1UL<<18)|(1UL<<17);
        sk_power_button_event();
    }
    asm volatile("dsb" ::: "memory");
}
extern "C" void sk_power_button_init() {
    // Preserve the hardware's emergency long-hold and wake timing settings.
    SNVS_LPSR=(1UL<<18)|(1UL<<17);
    NVIC_CLEAR_PENDING(IRQ_SNVS_ONOFF);
    attachInterruptVector(IRQ_SNVS_ONOFF,button_isr);
    NVIC_SET_PRIORITY(IRQ_SNVS_ONOFF,255);
    asm volatile("dsb" ::: "memory");
    NVIC_ENABLE_IRQ(IRQ_SNVS_ONOFF);
}
extern "C" void sk_power_cut() {
#if defined(SK_AMPLIFIER_SHUTDOWN_PIN) && SK_AMPLIFIER_SHUTDOWN_PIN >= 0
    digitalWrite(SK_AMPLIFIER_SHUTDOWN_PIN,LOW);
#endif
    SNVS_LPCR |= SNVS_LPCR_TOP;
    asm volatile("dsb" ::: "memory");
    for(;;)asm volatile("wfi");
}
#endif
