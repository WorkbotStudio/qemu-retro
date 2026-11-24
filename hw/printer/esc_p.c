#include "hw/printer/esc_p.h"
#include "qemu/qemu-print.h"

// NOTE: For research purposes "Command list by ascii order" can be searched for a list of all available commands

// TODO: Good news is: https://www.yout-ube.com/watch?v=inhZC75_mdU has a reference for the Windows XP test page,

// Is this entire file over-engineered? Probably.
// Do i care? No, not really.

const char* esc_to_string(char c);
const char* esc_to_string(char c)
{
    // NOTE: Got these names from https://www.ascii-code.com/characters/control-characters?view=grid
    switch (c)
    {
        case 0x00:
            return "NUL";
        case 0x01:
            return "SOH";
        case 0x02:
            return "STX";
        case 0x03:
            return "ETX";
        case 0x04:
            return "EOT";
        case 0x05:
            return "ENQ";
        case 0x06:
            return "ACK";
        case 0x07:
            return "BEL";
        case 0x08:
            return "BS";
        case 0x09:
            return "HT";
        case 0x0A:
            return "LF";
        case 0x0B:
            return "VT";
        case 0x0C:
            return "FF";
        case 0x0D:
            return "CR";
        case 0x0E:
            return "SO";
        case 0x0F:
            return "SI";
        case 0x10:
            return "DLE";
        case 0x11:
            return "DC1";
        case 0x12:
            return "DC2";
        case 0x13:
            return "DC3";
        case 0x14:
            return "DC4";
        case 0x15:
            return "NAK";
        case 0x16:
            return "SYN";
        case 0x17:
            return "ETB";
        case 0x18:
            return "CAN";
        case 0x19:
            return "EM";
        case 0x1A:
            return "SUB";
        case 0x1B:
            return "ESC";
        case 0x1C:
            return "FS";
        case 0x1D:
            return "GS";
        case 0x1E:
            return "RS";
        case 0x1F:
            return "US";
        case 0x20:
            return "SP";
        case 0x7F:
            return "DEL";
        default:
            return "UNKNOWN";
    }
}

void reset_cmd_sequence(EscpState* esc);
void reset_cmd_sequence(EscpState* esc)
{
    esc->sequence_size = 0;
    if(esc->current_sequence != NULL)
    {
        free(esc->current_sequence);
        esc->current_sequence = NULL;
    }
}

void escp_reset(EscpState* esc)
{
    esc->current_read = 0;
    esc->current_data.parameter_count = 0;
    esc->current_data.data_count = 0;
    esc->read_state = READ_CMD;
    esc->current_data.caller_params = NULL;
    if(esc->current_data.parameters != NULL)
    {
        free(esc->current_data.parameters);
        esc->current_data.parameters = NULL;
    }
    if(esc->current_data.data != NULL)
    {
        free(esc->current_data.data);
        esc->current_data.data = NULL;
    }
    reset_cmd_sequence(esc);
}

void escp_sequence_add(EscpState* esc, uint8_t data);
void escp_sequence_add(EscpState* esc, uint8_t data)
{
    if(esc->current_sequence == NULL)
    {
        esc->sequence_size++;
        esc->current_sequence = malloc(esc->sequence_size);
        esc->current_sequence[0] = data;
    }
    else
    {
        esc->sequence_size++;
        esc->current_sequence = realloc(esc->current_sequence, esc->sequence_size);
        esc->current_sequence[esc->sequence_size - 1] = data;
    }
}


// NOTE: The implementation details are based on https://github.com/RealDeuce/epsonfx80/blob/master/docs/fx80_emulator_notes.md
void escp_print_character(EscpState* esc, uint8_t data);
void escp_print_character(EscpState* esc, uint8_t data)
{
    uint8_t* glyph = &esc->printable_glyphs[(data + (esc->italic ? 128 : 0)) * 12];

    bool top = ((glyph[0] >> 7) & 0x01);

    uint8_t start = ((glyph[0] >> 4) & 0x7);

    uint8_t end = (glyph[0] & 0xF);
    uint8_t passes_width = esc->doublewidth ? 2 : 1;
    uint8_t passes_height = esc->doublestrike ? 2 : 1;

    for(int i = start; i < end; i++)
    {
        for(int k = 0; k < passes_width; k++)
        {
            uint8_t byte = glyph[1 + i];
            for(int j = 0; j < 8; j++)
            {
                bool bit = ((byte >> j) & 0x01);
                uint8_t pos = (7 - j) * 3;
                if(!top)
                {
                    pos += 2 * 3;
                }
                uint8_t val = bit ? 0xFF : 0x00;
                for(int h = 0; h < passes_height; h++)
                {
                    paper_set_block(&esc->paper, -1,pos - 1 + h,3,3,val,val,val);
                }
            }
            paper_advance(&esc->paper, 1,0);
        }
    }

    // TODO: Condensed multiplier is probably not entirely correct, but it's good enough for now.
    uint8_t spacing = esc->condensed ? (esc->pixels_per_character * 0.6) : esc->pixels_per_character;
    spacing -= (end - start);

    paper_advance(&esc->paper,spacing,0);
}

void escp_read_byte(EscpState* esc, uint8_t data)
{
    switch (esc->read_state)
    {
    case READ_CMD:
    case READ_NESTED_CMD:
        escp_sequence_add(esc, data);
        GBytes* key = g_bytes_new(esc->current_sequence, esc->sequence_size);
        EscCommand* cmd = g_hash_table_lookup(esc->commands, key);
        g_bytes_unref(key);
        if(cmd != NULL)
        {
            if(cmd->is_marker)
            {
                esc->read_state = READ_NESTED_CMD;
            }
            else
            {
                reset_cmd_sequence(esc);
                esc->current_data.caller_params = cmd->extra_data;
                esc->current_data.parameter_count = cmd->num_params;
                if(esc->current_data.parameter_count > 0)
                {
                    esc->current_cmd = cmd;
                    esc->current_read = 0;
                    esc->current_data.parameters = malloc(esc->current_data.parameter_count);
                    esc->read_state = READ_PARAMS;
                }
                else
                {
                    cmd->on_command(esc,esc->current_data);
                    escp_reset(esc);
                }
            }
        }
        else
        {
            if(esc->read_state == READ_CMD)
            {
                if(data < ' ' || data == 0x7F)
                {
                    qemu_printf("PRINTABLE DATA: 0x%02x ('%s')\n", data, esc_to_string(data));
                }
                else
                {
                    qemu_printf("PRINTABLE DATA: 0x%02x ('%c')\n", data, data);
                }
                escp_print_character(esc, data);
            }
            else
            {
                qemu_printf("UNKNOWN COMMAND SEQUENCE: ");
                for(int i = 0; i < esc->sequence_size - 1; i++)
                {
                    uint8_t c = esc->current_sequence[i];
                    if(c < ' ' || c == 0x7F)
                    {
                        qemu_printf("0x%02x ('%s') ", c, esc_to_string(c));
                    }
                    else
                    {
                        qemu_printf("0x%02x ('%c') ", c, c);
                    }
                }
                qemu_printf("\n");
            }
            escp_reset(esc);
        }
        break;
    case READ_PARAMS:
        esc->current_data.parameters[esc->current_read] = data;
        esc->current_read++;
        if(esc->current_read == esc->current_data.parameter_count)
        {
            if(esc->current_cmd->on_params != NULL)
            {
                esc->current_data.data_count = esc->current_cmd->on_params(esc, esc->current_data);
            }
            else
            {
                esc->current_data.data_count = 0;
            }
            if(esc->current_data.data_count > 0)
            {
                esc->current_read = 0;
                esc->current_data.data = malloc(esc->current_data.data_count);
                esc->read_state = READ_DATA;
            }
            else
            {
                esc->current_cmd->on_command(esc,esc->current_data);
                escp_reset(esc);
            }
        }
        break;
    case READ_DATA:
        esc->current_data.data[esc->current_read] = data;
        esc->current_read++;
        if(esc->current_read == esc->current_data.data_count)
        {
            esc->current_cmd->on_command(esc,esc->current_data);
            escp_reset(esc);
        }
    default:
        break;
    }
}

void escp_init_n(EscpState* esc, const EscCommand* commands, size_t n, escvariant_t variant)
{
    esc->api_variant = variant;

    esc->commands = g_hash_table_new(g_bytes_hash, g_bytes_equal);

    for(int i = 0; i < n; i++)
    {
        EscCommand* cmd = malloc(sizeof(EscCommand));
        memcpy(cmd, &commands[i], sizeof(EscCommand));
        GBytes* key = g_bytes_new(cmd->sequence, cmd->sequence_size);
        g_hash_table_insert(esc->commands, key, cmd);
    }
}

// EXTRA_DATA:
// (BYTE 0), 0 = 12 CPI
// (BYTE 0), 1 = 10 CPI
// (BYTE 0), 2 = 15 CPI
// TODO: Still haven't figured out a clean way to calculate how many pixels this would require
// TODO: Apparently the calculation for inches per character is 1/{SELECTED CPI}
void esc_cmd_select_cpi(EscpState* esc, EscData params)
{
    if(params.caller_params == NULL)
    {
        qemu_printf("esc_cmd_select_cpi requires extra data!\n");
        return;
    }

    switch (params.caller_params[0])
    {
    case 0:
        qemu_printf("12 CPI (20 CPI condensed) command called!\n");
        esc->pixels_per_character = 20;
        break;
    case 1:
        qemu_printf("10 CPI (17.1 CPI condensed) command called!\n");
        esc->pixels_per_character = 24;
        break;
    case 2:
        qemu_printf("15 CPI command called!\n");
        esc->pixels_per_character = 16;
        break;
    default:
        break;
    }
}

uint64_t esc_data_graphics(EscpState* esc, EscData params)
{
    uint16_t data_count = params.parameters[0] | params.parameters[1] << 8;
    return data_count;
}

// EXTRA_DATA:
// (BYTE 0), 0 = 60 DPI
// (BYTE 0), 1 = 120 DPI
// (BYTE 0), 2 = 120 DPI AT DOUBLE SPEED
// (BYTE 0), 3 = 240 DPI
void esc_cmd_graphics(EscpState* esc, EscData params)
{
    if(params.caller_params == NULL)
    {
        qemu_printf("esc_cmd_graphics requires extra data!\n");
        return;
    }

    uint8_t movement_per_row = 0;

    switch (params.caller_params[0])
    {
    case 0:
        movement_per_row = 4;
        qemu_printf("60 DPI graphics command called!\n");
        break;
    case 1:
        movement_per_row = 2;
        qemu_printf("120 DPI graphics command called!\n");
        break;
    case 2:
        movement_per_row = 2;
        qemu_printf("120 DPI double speed graphics command called!\n");
        break;
    case 3:
        movement_per_row = 1;
        qemu_printf("240 DPI graphics command called!\n");
        break;
    default:
        break;
    }

    for(int i = 0; i < params.data_count; i++)
    {
        for(int j = 7; j >= 0; j--)
        {
            bool bit = ((params.data[i] >> j) & 0x01);
            uint8_t pos = (7 - j) * 3;
            uint8_t val = bit ? 0x00 : 0xff;
            paper_set_block(&esc->paper, -1,pos - 1,3,3,val,val,val);
        }
        paper_advance(&esc->paper, movement_per_row,0);
    }
}

// EXTRA_DATA:
// (BYTE 0), 0 = 1/8-inch
// (BYTE 0), 1 = 7/72-inch
// (BYTE 0), 2 = 1/6-inch
// (BYTE 0), 3 = n/180-inch
// (BYTE 0), 4 = n/216-inch

// NOTE: To calculate relative to absolute max
// you do {ABSOLUTE MAX}/{DESIRED NUMBER} (result called RES)
// then to calculate it relative to the absolute max, it's {DESIRED NUMBER DIVIDER}*{RES}
// So for instance for 1/8:
// 216/8 is 27
// 27 * 1 = 27
// so 1/8 is equal to 27/216
// and everything is in inches,
// so it would be 27 pixels
// TODO: these calculations won't work for printers with 1/180-inch steps
void esc_cmd_set_line_spacing(EscpState* esc, EscData params)
{
    if(params.caller_params == NULL)
    {
        qemu_printf("esc_cmd_set_line_spacing requires extra data!\n");
        return;
    }

    switch (params.caller_params[0])
    {
    case 0:
        qemu_printf("1/8-inch line spacing (27 pixels) command called!\n");
        esc->pixels_per_newline = 27;
        break;
    case 1:
        qemu_printf("7/72-inch line spacing (21 pixels) command called!\n");
        esc->pixels_per_newline = 21;
        break;
    case 2:
        qemu_printf("1/6-inch line spacing (36 pixels) command called!\n");
        esc->pixels_per_newline = 36;
        break;
    case 3:
        qemu_printf("n/180-inch line spacing (%i pixels) command called with n of (%0x)!\n", params.parameters[0], params.parameters[0]);
        esc->pixels_per_newline = params.parameters[0];
        break;
    case 4:
        qemu_printf("n/216-inch line spacing (%i pixels) command called with n of (%0x)!\n", params.parameters[0], params.parameters[0]);
        esc->pixels_per_newline = params.parameters[0];
        break;
    default:
        break;
    }
}

void esc_cmd_page_length_lines(EscpState* esc, EscData params)
{
    qemu_printf("Select page length in lines command called with the choice (%0x)!\n", params.parameters[0]);
}

void esc_cmd_charset(EscpState* esc, EscData params)
{
    qemu_printf("Character set selection command called with choise of (%0x)!\n", params.parameters[0]);
}

void esc_cmd_charset_international(EscpState* esc, EscData params)
{
    qemu_printf("International character set selection command called with choise of (%0x)!\n", params.parameters[0]);
}

void esc_cmd_set_upperctrl_printing(EscpState* esc, EscData params)
{
    qemu_printf("%s upper control code printing command called!\n", (params.caller_params[0] == 1) ? "Enable" : "Disable");
}

void esc_cmd_return(EscpState* esc, EscData params)
{
    qemu_printf("Carriage return command called!\n");
    paper_reset_horizontal(&esc->paper);
}

void esc_cmd_newline(EscpState* esc, EscData params)
{
    qemu_printf("Newline command called!\n");
    paper_advance(&esc->paper, 0, esc->pixels_per_newline);
    paper_reset_horizontal(&esc->paper);
}

// EXTRA_DATA:
// (BYTE 0), 0 = Disabled
// (BYTE 0), 1 = Enabled
void esc_cmd_set_doublewidth(EscpState* esc, EscData params)
{
    if(params.caller_params == NULL)
    {
        qemu_printf("esc_cmd_set_doublewidth requires extra data!\n");
        return;
    }

    esc->doublewidth = (params.caller_params[0] == 1);

    qemu_printf("%s double width command called!\n", (params.caller_params[0] == 1) ? "Enable" : "Disable");
}

// EXTRA_DATA:
// (BYTE 0), 0 = Disabled
// (BYTE 0), 1 = Enabled
void esc_cmd_set_doublestrike(EscpState* esc, EscData params)
{
    if(params.caller_params == NULL)
    {
        qemu_printf("esc_cmd_set_doublestrike requires extra data!\n");
        return;
    }

    esc->doublestrike = (params.caller_params[0] == 1);
    
    qemu_printf("%s double strike command called!\n", (params.caller_params[0] == 1) ? "Enable" : "Disable");
}

// EXTRA_DATA:
// (BYTE 0), 0 = Disabled
// (BYTE 0), 1 = Enabled
void esc_cmd_set_italic(EscpState* esc, EscData params)
{
    if(params.caller_params == NULL)
    {
        qemu_printf("esc_cmd_set_italic requires extra data!\n");
        return;
    }

    esc->italic = (params.caller_params[0] == 1);

    qemu_printf("%s italic command called!\n", (params.caller_params[0] == 1) ? "Enable" : "Disable");
}

// EXTRA_DATA:
// (BYTE 0), 0 = Cancel
// (BYTE 0), 1 = Select
void esc_cmd_set_condensed(EscpState* esc, EscData params)
{
    if(params.caller_params == NULL)
    {
        qemu_printf("esc_cmd_set_condensed requires extra data!\n");
        return;
    }

    esc->condensed = (params.caller_params[0] == 1);

    qemu_printf("%s condensed command called!\n", (params.caller_params[0] == 1) ? "Select" : "Cancel");
}

void esc_cmd_move_paper(EscpState* esc, EscData params)
{
    // NOTE it's n/216 (so just pixel space)
    paper_advance(&esc->paper, 0, params.parameters[0]);
    qemu_printf("Move paper command called with n of (%0x)!\n", params.parameters[0]);
}

void esc_cmd_dump_paper(EscpState* esc, EscData params)
{
    qemu_printf("Dump paper command called!\n");

    char* directory = getenv("PRINTER_OUTPUT_DIR");

    time_t time_h = time(NULL);
    struct tm *info = localtime(&time_h);

    char* filename;
    if(directory)
    {
        size_t size = snprintf(NULL, 0,"%s/%d-%02d-%02d_%02d-%02d-%02d.bmp",
            directory, info->tm_year + 1900, info->tm_mon + 1,
            info->tm_mday, info->tm_hour, info->tm_min,
            info->tm_sec) + 1;
        filename = malloc(size);
        sprintf(filename,"%s/%d-%02d-%02d_%02d-%02d-%02d.bmp",
            directory, info->tm_year + 1900, info->tm_mon + 1,
            info->tm_mday, info->tm_hour, info->tm_min,
            info->tm_sec);
    }
    else
    {
        size_t size = snprintf(NULL, 0,"%d-%02d-%02d_%02d-%02d-%02d.bmp",
            info->tm_year + 1900, info->tm_mon + 1,
            info->tm_mday, info->tm_hour, info->tm_min,
            info->tm_sec) + 1;
        filename = malloc(size);
        sprintf(filename,"%d-%02d-%02d_%02d-%02d-%02d.bmp",
            info->tm_year + 1900, info->tm_mon + 1,
            info->tm_mday, info->tm_hour, info->tm_min,
            info->tm_sec);
    }

    paper_finish(&esc->paper, filename);

    free(filename);
}

void esc_cmd_reset(EscpState* esc, EscData params)
{
    qemu_printf("Reset command called!\n");

    // TODO: Haven't completely nailed down the size/margins yet,
    // currently it's worked out from multiplying the maximum steps by the machine's
    // average paper size, so (horizontal: 8 * 240, vertical: 11 * 216)
    // NOTE: In reality it's more like (horizontal: 8.5 * 240, vertical: 11 * 216),
    // but so far i haven't been able to get the margins down accurately enough.
    paper_init(&esc->paper, 1920, 2376);

    esc->pixels_per_newline = 36;
}