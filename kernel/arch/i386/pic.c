/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/timer.h>

#include "pic.h"

#define PIC1                0x20     /* IO base address for master PIC */
#define PIC2                0xA0     /* IO base address for slave PIC */
#define PIC1_COMMAND        PIC1     /* PIC1's Command register */
#define PIC1_IMR            (PIC1+1) /* PIC1's Interrupt Mask Register */
#define PIC2_COMMAND        PIC2     /* PIC2's Command register */
#define PIC2_IMR            (PIC2+1) /* PIC2's Interrupt Mask Register */

#define PIC_EOI             0x20     /* End-of-interrupt command code */
#define PIC_SPEC_EOI        0x60     /* Specific End-of-interrupt command */
#define PIC_READ_IRR        0x0a     /* OCW3 irq ready next CMD read */
#define PIC_READ_ISR        0x0b     /* OCW3 irq service next CMD read */
#define PIC_CASCADE         0x02     /* IR in the master for slave IRQs */

#define ICW1_ICW4           0x01     /* ICW4 (not) needed */
#define ICW1_SINGLE         0x02     /* Single (cascade) mode */
#define ICW1_INTERVAL4      0x04     /* Call address interval 4 (8) */
#define ICW1_LEVEL          0x08     /* Level triggered (edge) mode */
#define ICW1_INIT           0x10     /* Initialization - required! */

#define ICW4_8086           0x01     /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO           0x02     /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE      0x08     /* Buffered mode/slave */
#define ICW4_BUF_MASTER     0x0C     /* Buffered mode/master */
#define ICW4_SFNM           0x10     /* Special fully nested (not) */

static NO_INLINE void pic_io_wait(void)
{
   if (in_hypervisor())
      return;

   delay_us(2);
}


/*
 * Initialize the legacy PIT and remap IRQs
 *
 * By default, on boot, IRQs 0 to 7 are mapped to IDT entires 8 to 15. This
 * is a problem in protected mode, because IDT entry 8 is a Double Fault!
 * Without remapping, every time IRQ0 fires, we'll get a Double Fault, which
 * is NOT actually what's happening. We send commands to the PIT in order to
 * make IRQ0 to 15 be remapped to IDT entries 32 to 47.
 *
 * NOTE: it leaves all the IRQs masked.
 */

void init_pic_8259(u8 offset1, u8 offset2)
{
   ASSERT(!are_interrupts_enabled());

   outb(PIC1_IMR, 0xff);     /* mask everything */
   outb(PIC2_IMR, 0xff);     /* mask everything */
   pic_io_wait();

   /* start the initialization sequence - master */
   outb(PIC1_COMMAND, ICW1_INIT + ICW1_ICW4);
   pic_io_wait();

   /* set master PIC vector offset */
   outb(PIC1_IMR, offset1);
   pic_io_wait();

   /* tell master PIC that there is a slave PIC at IRQ2 */
   outb(PIC1_IMR, 1u << PIC_CASCADE);
   pic_io_wait();

   /* set master PIC in default mode */
   outb(PIC1_IMR, ICW4_8086);
   pic_io_wait();

   /* start the initialization sequence - slave */
   outb(PIC2_COMMAND, ICW1_INIT + ICW1_ICW4);
   pic_io_wait();

   /* set slave PIC vector offset */
   outb(PIC2_IMR, offset2);
   pic_io_wait();

   /* tell slave PIC its cascade number */
   outb(PIC2_IMR, PIC_CASCADE);
   pic_io_wait();

   /* set slave PIC in default mode */
   outb(PIC2_IMR, ICW4_8086);
   pic_io_wait();

   /* wait a lot for the PIC to initialize */
   if (!in_hypervisor()) {
      for (int i = 0; i < 50; i++)
         pic_io_wait();
   }
}

void pic_send_eoi(int __irq)
{
   ulong var;
   u8 irq = (u8)__irq;
   ASSERT(IN_RANGE_INC(__irq, 0, 32));

   disable_interrupts(&var);
   {
      if (irq < 8) {

         outb(PIC1_COMMAND, PIC_SPEC_EOI | irq);

      } else {

         outb(PIC2_COMMAND, PIC_SPEC_EOI | (irq - 8));
         outb(PIC1_COMMAND, PIC_SPEC_EOI | PIC_CASCADE);
      }
   }
   enable_interrupts(&var);
}

void pic_mask_and_send_eoi(int __irq)
{
   ulong var;
   u8 irq = (u8)__irq;
   u8 irq_mask;
   ASSERT(IN_RANGE_INC(__irq, 0, 32));

   disable_interrupts(&var);
   {
      if (irq < 8) {

         irq_mask = inb(PIC1_IMR);
         irq_mask |= (1 << irq);
         outb(PIC1_IMR, irq_mask);
         outb(PIC1_COMMAND, PIC_SPEC_EOI | irq);

      } else {

         const u8 ir = irq - 8;
         irq_mask = inb(PIC2_IMR);
         irq_mask |= (1 << ir);
         outb(PIC2_IMR, irq_mask);
         outb(PIC2_COMMAND, PIC_SPEC_EOI | ir);
         outb(PIC1_COMMAND, PIC_SPEC_EOI | PIC_CASCADE);
      }
   }
   enable_interrupts(&var);
}

void irq_set_mask(int irq)
{
   u16 port;
   ulong var;
   u8 irq_mask;
   ASSERT(IN_RANGE_INC(irq, 0, 32));

   if (irq < 8) {
      port = PIC1_IMR;
   } else {
      port = PIC2_IMR;
      irq -= 8;
   }

   disable_interrupts(&var);
   {
      irq_mask = inb(port);
      irq_mask |= (1 << irq);
      outb(port, irq_mask);
   }
   enable_interrupts(&var);
}

void irq_clear_mask(int irq)
{
   u16 port;
   ulong var;
   u8 irq_mask;
   ASSERT(IN_RANGE_INC(irq, 0, 32));

   if (irq < 8) {
      port = PIC1_IMR;
   } else {
      port = PIC2_IMR;
      irq -= 8;
   }

   disable_interrupts(&var);
   {
      irq_mask = inb(port);
      irq_mask &= ~(1 << irq);
      outb(port, irq_mask);
   }
   enable_interrupts(&var);
}

bool irq_is_masked(int irq)
{
   ulong var;
   bool res;
   ASSERT(IN_RANGE_INC(irq, 0, 32));

   disable_interrupts(&var);
   {
      if (irq < 8)
         res = inb(PIC1_IMR) & (1 << irq);
      else
         res = inb(PIC2_IMR) & (1 << (irq - 8));
   }
   enable_interrupts(&var);
   return res;
}

bool pic_is_spur_irq(int irq)
{
   ASSERT(!are_interrupts_enabled());

   /*
    * Check for a spurious wake-up.
    *
    * Source: https://wiki.osdev.org/8259_PIC, with some editing.
    *
    * When an IRQ occurs, the PIC chip tells the CPU (via. the PIC's INTR
    * line) that there's an interrupt, and the CPU acknowledges this and
    * waits for the PIC to send the interrupt vector. This creates a race
    * condition: if the IRQ disappears after the PIC has told the CPU there's
    * an interrupt but before the PIC has sent the interrupt vector to the
    * CPU, then the CPU will be waiting for the PIC to tell it which
    * interrupt vector but the PIC won't have a valid interrupt vector to
    * tell the CPU.
    *
    * To get around this, the PIC tells the CPU a fake interrupt number.
    * This is a spurious IRQ. The fake interrupt number is the lowest
    * priority interrupt number for the corresponding PIC chip (IRQ 7 for the
    * master PIC, and IRQ 15 for the slave PIC).
    *
    * Handling Spurious IRQs
    * -------------------------
    *
    * For a spurious IRQ, there is no real IRQ and the PIC chip's ISR
    * (In Service Register) flag for the corresponding IRQ will NOT be set.
    * This means that the interrupt handler must not send an EOI back to the
    * PIC to reset the ISR flag, EXCEPT when the spurious IRQ comes from the
    * 2nd PIC: in that case an EOI must be sent to the master PIC, but NOT
    * to the slave PIC.
    */

   if (irq == 7) {

      outb(PIC1_COMMAND, PIC_READ_ISR);
      u8 isr = inb(PIC1_COMMAND);
      return !(isr & (1 << 7));

   } else if (irq == 15) {

      outb(PIC2_COMMAND, PIC_READ_ISR);
      u8 isr = inb(PIC2_COMMAND);

      if (!(isr & (1 << 7))) {
         pic_send_eoi(PIC_CASCADE);
         return true;
      }
   }

   return false;
}
