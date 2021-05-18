#pragma once
#include <stdbool.h>
#include <stdint.h>

void lcd__set_gpio(void);

void lcd__two_lines_8_bit_bus(void);

// void lcd__control(void);

// void lcd__entry_mode(void);

void lcd__write_data(void);

// void lcd__function_set(void);

void lcd__clear_display(void);

// void lcd__write_instr(uint8_t data);

void lcd__drive_data_pins(uint8_t data);

void lcd__set_cursor_to_next_line(void);

void lcd__set_cursor_home(void);

void lcd__write_string(char *str);

void lcd__initialize(void);