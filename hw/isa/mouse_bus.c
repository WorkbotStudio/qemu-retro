/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Bus Mouse devices.
 *
 *          These devices were made by both Microsoft and Logitech. At
 *          first, Microsoft used the same protocol as Logitech, but did
 *          switch to their new protocol for their InPort interface. So,
 *          although alike enough to be handled in the same driver, they
 *          are not the same.
 *
 * NOTES:   Ported from Bochs with extensive modifications per testing
 *          of the real hardware, testing of drivers, and the old code.
 *
 *          Logitech Bus Mouse verified with:
 *            Linux Slackware 3.0
 *            Logitech LMouse.com 3.12
 *            Logitech LMouse.com 3.30
 *            Logitech LMouse.com 3.41
 *            Logitech LMouse.com 3.42
 *            Logitech LMouse.com 4.00
 *            Logitech LMouse.com 5.00
 *            Logitech LMouse.com 6.00
 *            Logitech LMouse.com 6.02 Beta
 *            Logitech LMouse.com 6.02
 *            Logitech LMouse.com 6.12
 *            Logitech LMouse.com 6.20
 *            Logitech LMouse.com 6.23
 *            Logitech LMouse.com 6.30
 *            Logitech LMouse.com 6.31E
 *            Logitech LMouse.com 6.34
 *            Logitech Mouse.exe 6.40
 *            Logitech Mouse.exe 6.41
 *            Logitech Mouse.exe 6.44
 *            Logitech Mouse.exe 6.46
 *            Logitech Mouse.exe 6.50
 *            Microsoft Mouse.com 2.00
 *            Microsoft Mouse.sys 3.00
 *            Microsoft Mouse.com 7.04
 *            Microsoft Mouse.com 8.21J
 *            Microsoft Windows 1.00 DR5
 *            Microsoft Windows 3.10.026
 *            Microsoft Windows 3.10.068 both MOUSE.DRV and LMOUSE.DRV
 *            Microsoft Windows NT 3.1
 *            Microsoft Windows 95
 *
 *          InPort verified with:
 *            Linux Slackware 3.0
 *            Logitech LMouse.com 6.12
 *            Logitech LMouse.com 6.41
 *            Microsoft Windows 3.10.068 both MOUSE.DRV and LMOUSE.DRV
 *            Microsoft Windows NT 3.1
 *            Microsoft Windows 98 SE
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 200?-2019 Bochs.
 *          Copyright 2017-2019 Miran Grca.
 *          Copyright 1989-2019 Fred N. van Kempen.
 */
// Ported by WorkbotStudio for qemu-retro

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "hw/isa/isa.h"
#include "hw/core/irq.h"
#include "ui/input.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "qapi/error.h"

#include "qemu/qemu-print.h"

#define TYPE_BUS_MOUSE "bus-mouse"

#define IRQ_MASK ((1 << 5) >> s->irq_num)

/* MS/Logictech Standard Bus Mouse Adapter */
#define BUSM_PORT_DATA      0x0000
#define BUSM_PORT_SIGNATURE 0x0001
#define BUSM_PORT_CONTROL   0x0002
#define BUSM_PORT_CONFIG    0x0003

#define HOLD_COUNTER        (1 << 7)
#define READ_X              (0 << 6)
#define READ_Y              (1 << 6)
#define READ_LOW            (0 << 5)
#define READ_HIGH           (1 << 5)
#define DISABLE_IRQ         (1 << 4)

#define DEVICE_ACTIVE       (1 << 7)

#define READ_X_LOW          (READ_X | READ_LOW)
#define READ_X_HIGH         (READ_X | READ_HIGH)
#define READ_Y_LOW          (READ_Y | READ_LOW)
#define READ_Y_HIGH         (READ_Y | READ_HIGH)

#define FLAG_INPORT         (1 << 0)
#define FLAG_ENABLED        (1 << 1)
#define FLAG_HOLD           (1 << 2)
#define FLAG_TIMER_INT      (1 << 3)
#define FLAG_DATA_INT       (1 << 4)

OBJECT_DECLARE_SIMPLE_TYPE(BusMouseState, BUS_MOUSE)

struct BusMouseState
{
    ISADevice parent_obj;

    uint8_t current_b;
    uint8_t control_val;
    uint8_t sig_val;

    int axis[INPUT_AXIS__MAX];
    bool btns[INPUT_BUTTON__MAX];
    int axis_current[INPUT_AXIS__MAX];

    uint16_t base;
    uint8_t irq_num;
    uint8_t bn;
    uint8_t flags;

    MemoryRegion io;

    qemu_irq irq;

    QemuInputHandlerState* hs;
};

static void busmouse_ioport_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    BusMouseState* s = BUS_MOUSE(opaque);

    switch (addr)
    {
    case BUSM_PORT_CONTROL:
        s->control_val = val | 0x0F;

        if(!(val & DISABLE_IRQ))
        {
            s->flags |= FLAG_TIMER_INT;
        }
        else
        {
            s->flags &= ~FLAG_TIMER_INT;
        }

        if(val & HOLD_COUNTER)
        {
            s->flags |= FLAG_HOLD;
        }
        else
        {
            s->flags &= ~FLAG_HOLD;
        }

        qemu_irq_lower(s->irq);
        break;
    case BUSM_PORT_SIGNATURE:
        s->sig_val = val;
        break;
    case BUSM_PORT_CONFIG:
        if(val & DEVICE_ACTIVE)
        {
            s->flags |= FLAG_ENABLED;
            s->control_val = 0x0F & ~IRQ_MASK;
        }
        else
        {
            uint8_t bit = 1 << ((val >> 1) & 0x07);
            if(val & 1)
            {
                s->control_val |= bit;
            }
            else
            {
                s->control_val &= ~bit;
            }
        }
        break;
    default:
        qemu_printf("UNIMPLEMENTED BUSMOUSE WRITE AT 0x%0lx (SIZE: 0x%0x, VALUE: 0x%0lx)\n", addr, size, val);
        break;
    }
}

static uint64_t busmouse_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    BusMouseState* s = BUS_MOUSE(opaque);
    
    switch (addr)
    {
    case BUSM_PORT_DATA:
        uint8_t message = s->control_val & 0x60;
        uint8_t result = 0;
        switch (message)
        {
        case READ_X_LOW:
            result = s->axis_current[0] & 0x0F;
            s->axis_current[0] &= ~0x0F;
            break;
        case READ_X_HIGH:
            result = (s->axis_current[0] >> 4) & 0x0F;
            s->axis_current[0] &= ~0xF0;
            break;
        case READ_Y_LOW:
            result = s->axis_current[1] & 0x0F;
            s->axis_current[1] &= ~0x0F;
            break;
        case READ_Y_HIGH:
            result = (s->axis_current[1] >> 4) & 0x0F;
            s->axis_current[1] &= ~0xF0;
            break;
        default:
            break;
        }
        result |= s->current_b;
        return result;
    case BUSM_PORT_CONTROL:
        uint8_t retval = s->control_val;
        s->control_val |= 0x0F;
        if(s->flags & FLAG_TIMER_INT)
        {
            s->control_val = (s->control_val & ~IRQ_MASK) | (g_random_int_range(0,255) & IRQ_MASK);
        }
        return retval;
    case BUSM_PORT_SIGNATURE:
        return s->sig_val;
    case BUSM_PORT_CONFIG:
        if(s->flags & FLAG_ENABLED)
        {
            return (s->control_val | 0x0F) & ~IRQ_MASK;
        }
        else
        {
            return 0xFF;
        }
    default:
        qemu_printf("UNIMPLEMENTED BUSMOUSE READ AT 0x%0lx (SIZE: 0x%0x)\n", addr, size);
        return 0;
    }
}

const MemoryRegionOps busmouse_io_ops = {
    .read = busmouse_ioport_read,
    .write = busmouse_ioport_write,
};

static void busmouse_input_event(DeviceState *dev, QemuConsole *src,
                                QemuInputEvent *evt)
{
    BusMouseState* s = BUS_MOUSE(dev);

    if(!(s->flags & FLAG_ENABLED))
    {
        return;
    }

    switch (evt->type) {
        case INPUT_EVENT_KIND_REL:
            s->axis[evt->rel.axis] += evt->rel.value;
            break;
        case INPUT_EVENT_KIND_BTN:
            s->btns[evt->btn.button] = evt->btn.down;
            break;
        default:
            break;
    }
}

static void busmouse_input_sync(DeviceState *dev)
{
    BusMouseState* s = BUS_MOUSE(dev);

    if(!(s->flags & FLAG_ENABLED))
    {
        return;
    }

    if(!(s->flags & FLAG_HOLD))
    {
        memcpy(&s->axis_current, &s->axis, 2*4);
        memset(&s->axis, 0, 2*4);
        s->current_b = ((s->btns[0] * 1) << 2) | ((s->btns[2] * 2) >> 1);
        if(s->bn == 3)
        {
            s->current_b |= ((s->btns[1] * 4) >> 1);
        }
        s->current_b = ((s->current_b ^ 7) << 5);
    }

    qemu_irq_raise(s->irq);
}

static const QemuInputHandler busmouse_handler = {
    .name  = "Microsoft Bus Mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = busmouse_input_event,
    .sync  = busmouse_input_sync,
};

static void busmouse_realizefn(DeviceState *dev, Error **errp)
{
    BusMouseState* s = BUS_MOUSE(dev);

    if(s->bn > 3)
    {
        error_setg(errp, "Can't support more than three buttons.");
        return;
    }

    if(s->bn < 2)
    {
        error_setg(errp, "Can't support less than two buttons.");
        return;
    }

    if(s->irq < 2 || s->irq > 5)
    {
        error_setg(errp, "Irq must be between 2 and 5.");
        return;
    }

    if(s->base != 0x230 && s->base != 0x234 && s->base != 0x238 && s->base != 0x23c)
    {
        error_setg(errp, "Base address must be either 0x230, 0x234, 0x238 or 0x23c.");
        return;
    }

    memory_region_init_io(&s->io, OBJECT(s), &busmouse_io_ops, s, "ioports", 4);
    isa_register_ioport(&s->parent_obj, &s->io, s->base);

    s->irq = isa_get_irq(&s->parent_obj, s->irq_num);

    s->hs = qemu_input_handler_register((DeviceState *)s, &busmouse_handler);
    qemu_input_handler_activate(s->hs);
}

static const Property parallel_isa_properties[] = {
    DEFINE_PROP_UINT8("buttons", BusMouseState, bn, 2),
    DEFINE_PROP_UINT8("irq",   BusMouseState, irq_num,  5),
    DEFINE_PROP_UINT16("base", BusMouseState, base, 0x23c),
};

static void busmouse_reset(DeviceState *dev)
{
    BusMouseState* s = BUS_MOUSE(dev);

    s->sig_val = 0;
    s->flags = 0;
    s->control_val = 0x0f;
}

static void busmouse_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = busmouse_realizefn;
    device_class_set_props(dc, parallel_isa_properties);
    device_class_set_legacy_reset(dc, busmouse_reset);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo busmouse_info = {
    .name = TYPE_BUS_MOUSE,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(BusMouseState),
    .class_init = busmouse_class_init
};

static void busmouse_register_types (void)
{
    type_register_static (&busmouse_info);
}

type_init (busmouse_register_types)