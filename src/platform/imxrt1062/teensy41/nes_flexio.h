// Included by peripherals.cpp through small_monitor.h; all calls serialized
// by its sampling guard. No per-edge interrupts, heap, or busy waits in FlexIO.
#pragma once
#include <FlexIO_t4.h>
#include <DMAChannel.h>
#include <initializer_list>
#include "nes_waveform.h"

#ifndef SK_NES_CONTROLLER_DATA_PIN
#define SK_NES_CONTROLLER_DATA_PIN -1
#endif
#ifndef SK_NES_FLEXIO
#define SK_NES_FLEXIO 1
#endif
namespace nes_reader {
static FlexIOHandler *flex;
static DMAChannel dma(false);
// DMA owns a whole cache line, never a stack/DTCM or shared cache-line buffer.
DMAMEM static uint32_t sample[8] __attribute__((aligned(32)));
static uint8_t timer=255, tx_clock=255, tx_latch=255, rx=255;
static bool running, stopped;
static uint32_t sampled, faults, scans;
static const char *mode="gpio";
#if SK_NES_CONTROLLER_DATA_PIN >= 0
static FlexIOHandler *second_flex;
static DMAChannel second_dma(false);
DMAMEM static uint32_t second_sample[8] __attribute__((aligned(32)));
static uint8_t second_timer=255,second_rx=255,second_trigger=255;
#endif

static void gpio_pins() {
    digitalWrite(superkeyboard::shift_clock,LOW);
    digitalWrite(superkeyboard::nes_latch,LOW);
    pinMode(superkeyboard::shift_clock,OUTPUT);
    pinMode(superkeyboard::nes_latch,OUTPUT);
    pinMode(superkeyboard::nes_data,INPUT_PULLUP);
#if SK_NES_CONTROLLER_DATA_PIN >= 0
    pinMode(superkeyboard::nes_controller_data,INPUT_PULLUP);
#endif
}
static void release() {
#if SK_NES_CONTROLLER_DATA_PIN >= 0
    if(second_dma.TCD)second_dma.disable();
    if(second_flex) {
        auto &p=second_flex->port();
        if(second_flex!=flex && second_timer!=255)p.TIMCTL[second_timer]=0;
        for(uint8_t index:{second_rx,second_trigger})if(index!=255) {
            p.SHIFTSDEN &= ~(1UL<<index);p.SHIFTCTL[index]=0;
        }
    }
    second_flex=nullptr;second_timer=second_rx=second_trigger=255;
#endif
    if(dma.TCD) dma.disable();
    if(flex) {
        auto &p=flex->port();
        if(timer!=255) p.TIMCTL[timer]=0;
        for(uint8_t index:{tx_clock,tx_latch,rx}) if(index!=255) {
            p.SHIFTSDEN &= ~(1UL<<index);
            p.SHIFTCTL[index]=0;
        }
    }
    // Reservations live until reboot. The bundled FlexIO_t4 freeTimers/freeShifter
    // incorrectly clear bit zero for nonzero indices; never release another
    // driver's resource through those helpers. These static resources are not
    // reallocated after fallback or shutdown.
    timer=tx_clock=tx_latch=rx=255;
    flex=nullptr;
    running=false;
    gpio_pins();
}
#if SK_NES_CONTROLLER_DATA_PIN >= 0
static bool second_begin(unsigned primary_div) {
    uint8_t pin;
    second_flex=FlexIOHandler::mapIOPinToFlexIOHandler(superkeyboard::nes_controller_data,pin);
    if(!second_flex)return false;
    auto &p=second_flex->port();
    if(second_flex==flex)second_timer=timer;
    else {
        bool unused=!(p.CTRL & FLEXIO_CTRL_FLEXEN);
        for(unsigned i=0;i<8;++i)if(p.TIMCTL[i] || p.SHIFTCTL[i])unused=false;
        if(unused)second_flex->setClockSettings(3,3,3);
        const unsigned hz=second_flex->computeClockRate(),div=nes_waveform::divider(hz);
        // Two FlexIO blocks must have EXACTLY equal slot periods. Never retune
        // a busy block; fallback reads both pads using the same physical clocks.
        if(!div || div>256 || uint64_t(primary_div)*hz!=uint64_t(div)*flex->computeClockRate())return false;
        second_timer=second_flex->requestTimers();
        second_trigger=second_flex->requestShifter();
        if(second_timer==255 || second_trigger==255)return false;
        p.TIMCTL[second_timer]=0;
        p.SHIFTCFG[second_trigger]=0;
        p.SHIFTCTL[second_trigger]=FLEXIO_SHIFTCTL_MODE_TRANSMIT |
            FLEXIO_SHIFTCTL_SHIFT_ON_FALLING_EDGE | FLEXIO_SHIFTCTL_TIMSEL(second_timer);
        // Dummy TX only triggers the internal sampling timer; no output pin.
        p.TIMCMP[second_timer]=((nes_waveform::slots*2-1)<<8)|(div-1);
        p.TIMCFG[second_timer]=FLEXIO_TIMCFG_OUTPUT_LOW_WHEN_ENABLED |
            FLEXIO_TIMCFG_DISABLE_ON_8BIT_MATCH | FLEXIO_TIMCFG_ENABLE_WHEN_TRIGGER_HIGH;
        p.TIMCTL[second_timer]=FLEXIO_TIMCTL_MODE_8BIT_BAUD |
            FLEXIO_TIMCTL_TRIGGER_SHIFTER(second_trigger) | FLEXIO_TIMCTL_TRIGGER_ACTIVE_LOW;
    }
    for(unsigned i=0;i<4;++i)
        if(second_flex->shiftersDMAChannel(i)!=255 && second_flex->claimShifter(i)) {second_rx=i;break;}
    if(second_rx==255)return false;
    second_dma.begin(true);
    if(!second_dma.TCD)return false;
    p.SHIFTCFG[second_rx]=0;
    p.SHIFTCTL[second_rx]=FLEXIO_SHIFTCTL_MODE_RECEIVE |
        FLEXIO_SHIFTCTL_TIMSEL(second_timer) | FLEXIO_SHIFTCTL_PINSEL(pin);
    p.CTRL |= FLEXIO_CTRL_FLEXEN;
    second_flex->setIOPinToFlexMode(superkeyboard::nes_controller_data);
    return true;
}
#endif
static void begin() {
    gpio_pins();
    sampled=millis()-nes_waveform::interval_ms;
    if(!SK_NES_FLEXIO) return;
    uint8_t clock_pin;
    flex=FlexIOHandler::mapIOPinToFlexIOHandler(superkeyboard::shift_clock,clock_pin);
    if(!flex) { mode="gpio-resource"; return; }
    const uint8_t latch_pin=flex->mapIOPinToFlexPin(superkeyboard::nes_latch);
    const uint8_t data_pin=flex->mapIOPinToFlexPin(superkeyboard::nes_data);
    // Only initialize the root when the block is unused. Select USB PLL / 16
    // (30 MHz), independent of the audio PLL configured later at startup.
    bool unused=!(flex->port().CTRL & FLEXIO_CTRL_FLEXEN);
    for(unsigned i=0;i<8;++i)
        if(flex->port().TIMCTL[i] || flex->port().SHIFTCTL[i]) unused=false;
    if(unused) flex->setClockSettings(3,3,3);
    const unsigned div=nes_waveform::divider(flex->computeClockRate());
    // Never retune a shared FlexIO clock (which may be used by another driver).
    if(latch_pin==255 || data_pin==255 || !div || div>256) {
        release(); mode="gpio-clock/pins"; return;
    }
    timer=flex->requestTimers();
    // Reserve a DMA-capable receive shifter before the two transmit shifters.
    for(unsigned i=0;i<4;++i)
        if(flex->shiftersDMAChannel(i)!=255 && flex->claimShifter(i)) { rx=i; break; }
    tx_clock=flex->requestShifter();
    tx_latch=flex->requestShifter();
    if(timer==255 || rx==255 || tx_clock==255 || tx_latch==255) {
        release(); mode="gpio-resource"; return;
    }
    dma.begin(true);
    if(!dma.TCD) { release(); mode="gpio-dma"; return; }
    auto &p=flex->port();
    p.TIMCTL[timer]=0;
    p.SHIFTCFG[rx]=p.SHIFTCFG[tx_clock]=p.SHIFTCFG[tx_latch]=0;
    p.SHIFTCTL[rx]=FLEXIO_SHIFTCTL_MODE_RECEIVE |
        FLEXIO_SHIFTCTL_TIMSEL(timer) | FLEXIO_SHIFTCTL_PINSEL(data_pin);
    p.SHIFTCTL[tx_clock]=FLEXIO_SHIFTCTL_MODE_TRANSMIT |
        FLEXIO_SHIFTCTL_SHIFT_ON_FALLING_EDGE | FLEXIO_SHIFTCTL_PINMODE_OUTPUT |
        FLEXIO_SHIFTCTL_TIMSEL(timer) | FLEXIO_SHIFTCTL_PINSEL(clock_pin);
    p.SHIFTCTL[tx_latch]=FLEXIO_SHIFTCTL_MODE_TRANSMIT |
        FLEXIO_SHIFTCTL_SHIFT_ON_FALLING_EDGE | FLEXIO_SHIFTCTL_PINMODE_OUTPUT |
        FLEXIO_SHIFTCTL_TIMSEL(timer) | FLEXIO_SHIFTCTL_PINSEL(latch_pin);
    p.TIMCMP[timer]=((nes_waveform::slots*2-1)<<8)|(div-1);
    p.TIMCFG[timer]=FLEXIO_TIMCFG_OUTPUT_LOW_WHEN_ENABLED |
        FLEXIO_TIMCFG_DISABLE_ON_8BIT_MATCH | FLEXIO_TIMCFG_ENABLE_WHEN_TRIGGER_HIGH;
    // Writing the clock word last starts both TX words and RX together.
    p.TIMCTL[timer]=FLEXIO_TIMCTL_MODE_8BIT_BAUD |
        FLEXIO_TIMCTL_TRIGGER_SHIFTER(tx_clock) | FLEXIO_TIMCTL_TRIGGER_ACTIVE_LOW;
    p.CTRL |= FLEXIO_CTRL_FLEXEN;
#if SK_NES_CONTROLLER_DATA_PIN >= 0
    if(!second_begin(div)) {release();mode="gpio-dual-resource";return;}
#endif
    flex->setIOPinToFlexMode(superkeyboard::shift_clock);
    flex->setIOPinToFlexMode(superkeyboard::nes_latch);
    flex->setIOPinToFlexMode(superkeyboard::nes_data);
#if SK_NES_CONTROLLER_DATA_PIN >= 0
    mode="flexio-dual-dma";
#else
    mode="flexio-dma";
#endif
}
static uint8_t gpio_read(uint8_t &external) {
    external=0;
    digitalWrite(superkeyboard::nes_latch,HIGH); delayMicroseconds(12);
    digitalWrite(superkeyboard::nes_latch,LOW); delayMicroseconds(6);
    uint8_t raw=0;
    for(unsigned bit=0;bit<8;++bit) {
        if(!digitalRead(superkeyboard::nes_data)) raw|=1U<<bit;
#if SK_NES_CONTROLLER_DATA_PIN >= 0
        if(!digitalRead(superkeyboard::nes_controller_data))external|=1U<<bit;
#endif
        digitalWrite(superkeyboard::shift_clock,HIGH); delayMicroseconds(6);
        digitalWrite(superkeyboard::shift_clock,LOW); delayMicroseconds(6);
    }
    return raw;
}
static bool poll(uint32_t now,uint8_t &raw,uint8_t &external,uint32_t &at) {
    if(stopped) return false;
    bool ready=false;
    if(running) {
        const bool complete=dma.complete()
#if SK_NES_CONTROLLER_DATA_PIN >= 0
            && second_dma.complete()
#endif
            ;
        if(complete) {
            auto &p=flex->port();
            dma.disable(); p.SHIFTSDEN &= ~(1UL<<rx);
            const uint32_t mask=(1UL<<rx)|(1UL<<tx_clock)|(1UL<<tx_latch);
#if SK_NES_CONTROLLER_DATA_PIN >= 0
            auto &q=second_flex->port();
            second_dma.disable();q.SHIFTSDEN &= ~(1UL<<second_rx);
            const uint32_t second_mask=(1UL<<second_rx)|(second_trigger==255?0:1UL<<second_trigger);
            const bool second_ok=!(q.SHIFTERR & second_mask);
#else
            const bool second_ok=true;
#endif
            if(!(p.SHIFTERR & mask) && second_ok) {
                arm_dcache_delete(sample,sizeof(sample));
                raw=nes_waveform::decode(sample[0]); at=sampled;
                external=0;
#if SK_NES_CONTROLLER_DATA_PIN >= 0
                arm_dcache_delete(second_sample,sizeof(second_sample));
                external=nes_waveform::decode(second_sample[0]);
#endif
                ready=true; ++scans;
            } else {
                ++faults; release(); mode="gpio-error";
            }
            running=false;
        } else if(uint32_t(now-sampled)>=5) {
            ++faults; release(); mode="gpio-timeout";
        } else return false;
    }
    if(uint32_t(now-sampled)<nes_waveform::interval_ms) return ready;
    sampled=now;
    if(!flex) {
        // Keep a completed sample rather than replacing it with a second scan.
        if(ready) return true;
        raw=gpio_read(external); at=now; ++scans; return true;
    }
    auto &p=flex->port();
    (void)p.SHIFTBUF[rx]; // Drain stale receive status before enabling DMA.
    p.SHIFTERR=(1UL<<rx)|(1UL<<tx_clock)|(1UL<<tx_latch);
    p.TIMSTAT=1UL<<timer;
    arm_dcache_flush_delete(sample,sizeof(sample));
    dma.clearComplete();
    dma.source(p.SHIFTBUF[rx]); dma.destination(sample[0]);
    dma.transferSize(4); dma.transferCount(1); dma.disableOnCompletion();
    dma.triggerAtHardwareEvent(flex->shiftersDMAChannel(rx));
    dma.enable(); p.SHIFTSDEN |= 1UL<<rx;
#if SK_NES_CONTROLLER_DATA_PIN >= 0
    auto &q=second_flex->port();
    (void)q.SHIFTBUF[second_rx];
    q.SHIFTERR=(1UL<<second_rx)|(second_trigger==255?0:1UL<<second_trigger);
    q.TIMSTAT=1UL<<second_timer;
    arm_dcache_flush_delete(second_sample,sizeof(second_sample));
    second_dma.clearComplete();
    second_dma.source(q.SHIFTBUF[second_rx]);second_dma.destination(second_sample[0]);
    second_dma.transferSize(4);second_dma.transferCount(1);second_dma.disableOnCompletion();
    second_dma.triggerAtHardwareEvent(second_flex->shiftersDMAChannel(second_rx));
    second_dma.enable();q.SHIFTSDEN |= 1UL<<second_rx;
#endif
    running=true;
    // Both inputs see the one physical latch/clock waveform. On separate FlexIO
    // blocks, launch equal-rate timers with just adjacent peripheral writes;
    // IRQs cannot stretch their phase separation across a sample boundary.
    const uint32_t irq=__get_PRIMASK();__disable_irq();
    p.SHIFTBUF[tx_latch]=nes_waveform::latch;
#if SK_NES_CONTROLLER_DATA_PIN >= 0
    if(second_trigger!=255)q.SHIFTBUF[second_trigger]=0;
#endif
    p.SHIFTBUF[tx_clock]=nes_waveform::clock;
    if(!irq)__enable_irq();
    return ready;
}
static void stop() { stopped=true; release(); mode="off"; }
}
