#ifndef ESC_P_H
#define ESC_P_H

#include "qemu/osdep.h"
#include "hw/printer/paper.h"

enum escpstate_t
{
    READ_CMD,
    READ_NESTED_CMD,
    READ_PARAMS,
    READ_DATA
};

typedef enum escpstate_t escpstate_t;

enum escvariant_t
{
    // The 9-Pin ESC/P API
    VAR_9PIN,
    // The ESC/P API
    VAR_V1,
    // The ESC/P 2 API
    VAR_V2,
};

typedef enum escvariant_t escvariant_t;

typedef struct EscpState EscpState;

typedef struct EscCommand EscCommand;

typedef struct EscData EscData;

struct EscData
{
    uint8_t* caller_params;
    uint64_t parameter_count;
    uint8_t* parameters;
    uint64_t data_count;
    uint8_t* data;
};

struct EscpState
{
    escvariant_t api_variant;
    escpstate_t read_state;
    GHashTable *commands;
    uint64_t sequence_size;
    uint8_t* current_sequence;

    uint8_t* printable_glyphs;
    
    uint64_t current_read;
    EscCommand* current_cmd;
    EscData current_data;

    PaperState paper;

    uint8_t pixels_per_newline;
    uint8_t pixels_per_character;
    bool condensed;
    bool italic;
    bool doublestrike;
    bool doublewidth;
};

struct EscCommand
{
    // If true, this EscCommand won't be seen as an executable command,
    // but rather as a hint that other commands come after this symbol.
    bool is_marker;
    // The size of the sequence used to detect this command
    uint64_t sequence_size;
    // The sequence used to detect this command.
    uint8_t* sequence;
    // Optional extra data to send to the command.
    uint8_t* extra_data;
    // The number of parameters to read before executing the command.
    uint64_t num_params;
    // An optional function indicating how many extra bytes of data to read.
    uint64_t (*on_params)(EscpState* esc, EscData params);
    // The executable command itself.
    void (*on_command)(EscpState* esc, EscData params);
};

// Defines the sequence used to detect an EscCommand entry.
#define CMD_SEQUENCE(...) \
.sequence_size = (sizeof((uint8_t[]){__VA_ARGS__}) / sizeof(((uint8_t[]){__VA_ARGS__})[0])), \
.sequence = (uint8_t[]){__VA_ARGS__}

#define escp_init(esc, commands, variant) \
size_t commands_count_ = ARRAY_SIZE(commands); \
escp_init_n((esc), (commands), commands_count_, variant);

void escp_init_n(EscpState* esc, const EscCommand* commands, size_t n, escvariant_t variant);
void escp_read_byte(EscpState* esc, uint8_t data);
void escp_reset(EscpState* esc);

// EXTRA_DATA:
// (BYTE 0), 0 = 12 CPI
// (BYTE 0), 1 = 10 CPI
// (BYTE 0), 2 = 15 CPI
void esc_cmd_select_cpi(EscpState* esc, EscData params);
uint64_t esc_data_graphics(EscpState* esc, EscData params);
// EXTRA_DATA:
// (BYTE 0), 0 = 60 DPI
// (BYTE 0), 1 = 120 DPI
// (BYTE 0), 2 = 120 DPI AT DOUBLE SPEED
// (BYTE 0), 3 = 240 DPI
void esc_cmd_graphics(EscpState* esc, EscData params);
// EXTRA_DATA:
// (BYTE 0), 0 = 1/8-inch
// (BYTE 0), 1 = 7/72-inch
// (BYTE 0), 2 = 1/6-inch
// (BYTE 0), 3 = n/180-inch
// (BYTE 0), 4 = n/216-inch
void esc_cmd_set_line_spacing(EscpState* esc, EscData params);
void esc_cmd_page_length_lines(EscpState* esc, EscData params);
void esc_cmd_charset(EscpState* esc, EscData params);
void esc_cmd_charset_international(EscpState* esc, EscData params);
// EXTRA_DATA:
// (BYTE 0), 0 = Disabled
// (BYTE 0), 1 = Enabled
void esc_cmd_set_upperctrl_printing(EscpState* esc, EscData params);
void esc_cmd_return(EscpState* esc, EscData params);
void esc_cmd_newline(EscpState* esc, EscData params);
// EXTRA_DATA:
// (BYTE 0), 0 = Disabled
// (BYTE 0), 1 = Enabled
void esc_cmd_set_doublewidth(EscpState* esc, EscData params);
// EXTRA_DATA:
// (BYTE 0), 0 = Disabled
// (BYTE 0), 1 = Enabled
void esc_cmd_set_doublestrike(EscpState* esc, EscData params);
// EXTRA_DATA:
// (BYTE 0), 0 = Disabled
// (BYTE 0), 1 = Enabled
void esc_cmd_set_italic(EscpState* esc, EscData params);
// EXTRA_DATA:
// (BYTE 0), 0 = Cancel
// (BYTE 0), 1 = Select
void esc_cmd_set_condensed(EscpState* esc, EscData params);
void esc_cmd_move_paper(EscpState* esc, EscData params);
void esc_cmd_dump_paper(EscpState* esc, EscData params);
void esc_cmd_reset(EscpState* esc, EscData params);

#endif