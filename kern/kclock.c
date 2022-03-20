/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <kern/kclock.h>
#include <kern/timer.h>
#include <kern/trap.h>
#include <kern/picirq.h>

/* HINT: Note that selected CMOS
 * register is reset to the first one
 * after first access, i.e. it needs to be selected
 * on every access.
 *
 * Don't forget to disable NMI for the time of
 * operation (look up for the appropriate constant in kern/kclock.h)
 *
 * Why it is necessary?
 */

const uint8_t IO_RTC_CMND = 0x70;
const uint8_t IO_RTC_DATA = 0x71;
const uint8_t DISABLE_NMI_FLAG = 1 << 7;

uint8_t
cmos_read8(uint8_t reg) {
    /* MC146818A controller */
    // LAB 4: Your code here
    uint8_t res = 0;
    outb(IO_RTC_CMND, DISABLE_NMI_FLAG | reg);
    res = inb(IO_RTC_DATA);
    
    nmi_enable();
    return res;
}

void
cmos_write8(uint8_t reg, uint8_t value) {
    // LAB 4: Your code here
    outb(IO_RTC_CMND, DISABLE_NMI_FLAG | reg);
    outb(IO_RTC_DATA, value);
    
    nmi_enable();
}

uint16_t
cmos_read16(uint8_t reg) {
    return cmos_read8(reg) | (cmos_read8(reg + 1) << 8);
}

static void
rtc_timer_pic_interrupt(void) {
    // LAB 4: Your code here
    // Enable PIC interrupts.
    pic_irq_unmask(IRQ_CLOCK);
}

static void
rtc_timer_pic_handle(void) {
    rtc_check_status();
    pic_send_eoi(IRQ_CLOCK);
}

struct Timer timer_rtc = {
        .timer_name = "rtc",
        .timer_init = rtc_timer_init,
        .enable_interrupts = rtc_timer_pic_interrupt,
        .handle_interrupts = rtc_timer_pic_handle,
};

void
rtc_timer_init(void) {
    // LAB 4: Your code here
    // (use cmos_read8/cmos_write8)
    uint8_t prev_a = cmos_read8(RTC_AREG);
    cmos_write8(RTC_AREG, prev_a | 0x0F);
    uint8_t prev_b = cmos_read8(RTC_BREG);
    cmos_write8(RTC_BREG, prev_b | RTC_PIE);
}

uint8_t
rtc_check_status(void) {
    // LAB 4: Your code here
    // (use cmos_read8)
    return cmos_read8(RTC_CREG);
}
