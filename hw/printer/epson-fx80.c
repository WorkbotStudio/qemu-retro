#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "hw/char/parallel-isa.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "system/system.h"
#include "hw/printer/esc_p.h"

//#include "qemu/qemu-print.h"

#define TYPE_EPSON_FX80 "epson-fx80"

// NOTE: A few things i've noticed so far:
// 1: It starts in text mode.
// 2: message 0x12 means it shouldn't print?
// 3: message 0x0f means begin printing?
// 4: the 0x40 (@) command is to reset
// 5: the 0x0c (FF) command is apparently to output the paper
// 6: We only read data every time the strobe command is passed

// NOTE: Will probably need the epson fx 80's rom (for character sets)

OBJECT_DECLARE_SIMPLE_TYPE(Fx80State, EPSON_FX80)

/*
 * These are the definitions for the Printer Status Register
 */
#define PARA_STS_BUSY   0x80    /* Busy complement */
#define PARA_STS_ACK    0x40    /* Acknowledge */
#define PARA_STS_PAPER  0x20    /* Out of paper */
#define PARA_STS_ONLINE 0x10    /* Online */
#define PARA_STS_ERROR  0x08    /* Error complement */
#define PARA_STS_TMOUT  0x01    /* EPP timeout */

/*
 * These are the definitions for the Printer Control Register
 */
#define PARA_CTR_INTEN  0x10    /* IRQ Enable */
#define PARA_CTR_SELECT 0x08    /* Select In complement */
#define PARA_CTR_INIT   0x04    /* Initialize Printer complement */
#define PARA_CTR_STROBE 0x01    /* Strobe complement */

#define PARA_REG_DATA 0
#define PARA_REG_STS 1
#define PARA_REG_CTR 2

struct Fx80State
{
    ISADevice parent_obj;
    MemoryRegion io;
    uint8_t data;
    uint8_t control;
    uint8_t status;

    EscpState esc;
};

typedef struct Fx80State Fx80State;

static uint64_t fx80_read(void *opaque, hwaddr addr, unsigned size)
{
    Fx80State *s = opaque;

    switch (addr) {
    case PARA_REG_DATA:
        return s->data;
    case PARA_REG_STS:
        uint8_t ret = s->status;
        if ((s->status & PARA_STS_BUSY) == 0 && (s->control & PARA_CTR_STROBE) == 0) {
            /* XXX Fixme: wait 5 microseconds */
            if (s->status & PARA_STS_ACK) {
                s->status &= ~PARA_STS_ACK;
            }
            else {
                /* XXX Fixme: wait 5 microseconds */
                s->status |= PARA_STS_ACK;
                s->status |= PARA_STS_BUSY;
            }
        }
        return ret;
    case PARA_REG_CTR:
        return s->control;
    default:
        return 0xFF;
    }
}

static void fx80_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    Fx80State *s = opaque;

    switch (addr) {
    case PARA_REG_DATA:
        s->data = val;
        break;
    case PARA_REG_CTR:
        val |= 0xc0;
        if ((val & PARA_CTR_INIT) == 0 ) {
            s->status = PARA_STS_BUSY;
            s->status |= PARA_STS_ACK;
            s->status |= PARA_STS_ONLINE;
            s->status |= PARA_STS_ERROR;
        }
        else if (val & PARA_CTR_SELECT) {
            if (val & PARA_CTR_STROBE) {
                s->status &= ~PARA_STS_BUSY;
                if ((s->control & PARA_CTR_STROBE) == 0)
                {
                    escp_read_byte(&s->esc, s->data);
                }
            }
        }
        s->control = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps fx80_ops = {
    .read = fx80_read,
    .write = fx80_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const EscCommand fx80_commands[] = {
    {
        .is_marker = true,
        .sequence_size = 1,
        .sequence = (uint8_t[]) {0x1B}
    },
    {
        .sequence_size = 1,
        .sequence = (uint8_t[]) {0x0D},
        .on_command = esc_cmd_return
    },
    {
        .sequence_size = 1,
        .sequence = (uint8_t[]) {0x0A},
        .on_command = esc_cmd_newline
    },
    {
        .sequence_size = 1,
        .sequence = (uint8_t[]) {0x0E},
        .extra_data = (uint8_t[]) {0x01},
        .on_command = esc_cmd_set_doublewidth
    },
    {
        .sequence_size = 1,
        .sequence = (uint8_t[]) {0x14},
        .extra_data = (uint8_t[]) {0x00},
        .on_command = esc_cmd_set_doublewidth
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x47},
        .extra_data = (uint8_t[]) {0x01},
        .on_command = esc_cmd_set_doublestrike
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x48},
        .extra_data = (uint8_t[]) {0x00},
        .on_command = esc_cmd_set_doublestrike
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x34},
        .extra_data = (uint8_t[]) {0x01},
        .on_command = esc_cmd_set_italic
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x35},
        .extra_data = (uint8_t[]) {0x00},
        .on_command = esc_cmd_set_italic
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x4D},
        .extra_data = (uint8_t[]) {0x00},
        .on_command = esc_cmd_select_cpi
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x50},
        .extra_data = (uint8_t[]) {0x01},
        .on_command = esc_cmd_select_cpi
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x67},
        .extra_data = (uint8_t[]) {0x02},
        .on_command = esc_cmd_select_cpi
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x4B},
        .num_params = 2,
        .on_params = esc_data_graphics,
        .extra_data = (uint8_t[]) {0x00},
        .on_command = esc_cmd_graphics
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x4C},
        .num_params = 2,
        .on_params = esc_data_graphics,
        .extra_data = (uint8_t[]) {0x01},
        .on_command = esc_cmd_graphics
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x59},
        .num_params = 2,
        .on_params = esc_data_graphics,
        .extra_data = (uint8_t[]) {0x02},
        .on_command = esc_cmd_graphics
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x5A},
        .num_params = 2,
        .on_params = esc_data_graphics,
        .extra_data = (uint8_t[]) {0x03},
        .on_command = esc_cmd_graphics
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x30},
        .extra_data = (uint8_t[]) {0x00},
        .on_command = esc_cmd_set_line_spacing
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x31},
        .extra_data = (uint8_t[]) {0x01},
        .on_command = esc_cmd_set_line_spacing
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x32},
        .extra_data = (uint8_t[]) {0x02},
        .on_command = esc_cmd_set_line_spacing
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x33},
        .num_params = 1,
        .extra_data = (uint8_t[]) {0x04},
        .on_command = esc_cmd_set_line_spacing
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x43},
        .num_params = 1,
        .on_command = esc_cmd_page_length_lines
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x74},
        .num_params = 1,
        .on_command = esc_cmd_charset
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x52},
        .num_params = 1,
        .on_command = esc_cmd_charset_international
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x36},
        .extra_data = (uint8_t[]) {0x01},
        .on_command = esc_cmd_set_upperctrl_printing
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x36},
        .extra_data = (uint8_t[]) {0x00},
        .on_command = esc_cmd_set_upperctrl_printing
    },
    {
        .sequence_size = 1,
        .sequence = (uint8_t[]) {0x0C},
        .on_command = esc_cmd_dump_paper
    },
    {
        .sequence_size = 1,
        .sequence = (uint8_t[]) {0x12},
        .extra_data = (uint8_t[]) {0x00},
        .on_command = esc_cmd_set_condensed        
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x0F},
        .extra_data = (uint8_t[]) {0x01},
        .on_command = esc_cmd_set_condensed        
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x0F},
        .extra_data = (uint8_t[]) {0x01},
        .on_command = esc_cmd_set_condensed        
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x4A},
        .num_params = 1,
        .on_command = esc_cmd_move_paper
    },
    {
        .sequence_size = 2,
        .sequence = (uint8_t[]) {0x1B, 0x40},
        .on_command = esc_cmd_reset
    }
};

static void fx80_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    Fx80State *s = EPSON_FX80(dev);
    
    escp_init(&s->esc,fx80_commands, VAR_9PIN);

    memory_region_init_io(&s->io, OBJECT(s), &fx80_ops, s, "custom-lpt", 3);
    isa_register_ioport(isadev, &s->io, 0x378);
}

static void fx80_reset(DeviceState *dev)
{
    Fx80State *s = EPSON_FX80(dev);

    s->status = PARA_STS_BUSY;
    s->status |= PARA_STS_ACK;
    s->status |= PARA_STS_ONLINE;
    s->status |= PARA_STS_ERROR;
    s->status |= PARA_STS_TMOUT;
    s->control = PARA_CTR_SELECT;
    s->control |= PARA_CTR_INIT;
    s->control |= 0xc0;

    escp_reset(&s->esc);
}

static void fx80_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = fx80_realizefn;
    device_class_set_legacy_reset(dc, fx80_reset);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo fx80_type_info = {
    .name = TYPE_EPSON_FX80,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(Fx80State),
    .class_init = fx80_class_init
};

static void register_types(void)
{
    type_register_static(&fx80_type_info);
}

type_init(register_types);