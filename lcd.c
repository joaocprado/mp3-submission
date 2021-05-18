#include "lcd.h"
#include "FreeRTOS.h"
#include "delay.h"
#include "gpio.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/**
 * Port on LCD                  |   SJTWO Port
 * ----------------------------------------------------------------
 * 4    Reg Select              |   P0.11
 * 6    Enable                  |   P0.0
 * 7    Data                    |   P0.22
 * 8    Data                    |   P0.17
 * 9    Data                    |   P0.16
 * 10   Data                    |   P2.8
 * 11   Data                    |   P2.6
 * 12   Data                    |   P2.4
 * 13   Data                    |   P2.1
 * 14   Data                    |   P1.29
 **/

static gpio_s reg_sel = {1, 23};
static gpio_s enable = {0, 0};
static gpio_s db0 = {0, 22};
static gpio_s db1 = {0, 17};
static gpio_s db2 = {0, 16};
static gpio_s db3 = {2, 8};
static gpio_s db4 = {2, 6};
static gpio_s db5 = {2, 4};
static gpio_s db6 = {2, 1};
static gpio_s db7 = {1, 29};

static const uint8_t eight_bit_db_2_lines = 0b00111000;
static const uint8_t display_control = 0b00001100;
static const uint8_t char_increment_right = 0b00000110;
static const uint8_t return_cursor_home = 0b00000010;
static const uint8_t clear_display = 0b00000001;
static const uint8_t set_cursor_to_next_line = 0b11000000;

/* DDRAM Adress
  beginning first line = 0x00
  beginning second line = 0x40
*/

static gpio_s data_bus[] = {[0].port_number = 0, [0].pin_number = 22, [1].port_number = 0, [1].pin_number = 17,
                            [2].port_number = 0, [2].pin_number = 16, [3].port_number = 2, [3].pin_number = 8,
                            [4].port_number = 2, [4].pin_number = 6,  [5].port_number = 2, [5].pin_number = 4,
                            [6].port_number = 2, [6].pin_number = 1,  [7].port_number = 1, [7].pin_number = 29};

static void lcd__init_pins(void) {
  gpio__construct_as_output(reg_sel.port_number, reg_sel.pin_number);
  gpio__construct_as_output(enable.port_number, enable.pin_number);
  gpio__construct_as_output(db0.port_number, db0.pin_number);
  gpio__construct_as_output(db1.port_number, db1.pin_number);
  gpio__construct_as_output(db2.port_number, db2.pin_number);
  gpio__construct_as_output(db3.port_number, db3.pin_number);
  gpio__construct_as_output(db4.port_number, db4.pin_number);
  gpio__construct_as_output(db5.port_number, db5.pin_number);
  gpio__construct_as_output(db6.port_number, db6.pin_number);
  gpio__construct_as_output(db7.port_number, db7.pin_number);
}

static void enable_high(void) { gpio__set(enable); }

static void enable_low(void) { gpio__reset(enable); }

static void lcd__write_char(uint8_t data) {
  enable_high();
  gpio__set(reg_sel);
  lcd__drive_data_pins(data);
  enable_low();
}

static void lcd__write_instr(uint8_t data) {
  enable_high();
  gpio__reset(reg_sel);
  lcd__drive_data_pins(data);
  enable_low();
}

static uint8_t ascii_to_bin(char c) {
  uint8_t compare;
  uint8_t binary_value = 0;
  for (int i = 7; i >= 0; i--) {
    compare = (c & (1 << i));
    if (compare) {
      binary_value |= (1 << i);
    } else {
      binary_value &= ~(1 << i);
    }
  }
  return binary_value;
}

static void lcd__function_set(void) { lcd__write_instr(eight_bit_db_2_lines); }

static void lcd__control(void) { lcd__write_instr(display_control); }

static void lcd__entry_mode(void) { lcd__write_instr(char_increment_right); }

/********************
 * PUBLIC FUNCTIONS *
 ********************/

void lcd__initialize(void) {
  lcd__init_pins();
  delay__ms(2);
  lcd__function_set();
  delay__ms(2);
  lcd__control();
  delay__ms(2);
  lcd__entry_mode();
  delay__ms(2);
  lcd__clear_display();
  delay__ms(2);
}

void lcd__clear_display(void) {
  lcd__write_instr(clear_display);
  delay__ms(2);
}

void lcd__write_string(char *str) {
  int size = strlen(str);

  for (size_t i = 0; i < size; i++) {
    lcd__write_char(ascii_to_bin(str[i]));
  }
}

void lcd__drive_data_pins(uint8_t data) {
  uint8_t level;
  for (int i = 7; i >= 0; i--) {
    level = (data & (1 << i));
    if (level) {
      gpio__set(data_bus[i]);
    } else {
      gpio__reset(data_bus[i]);
    }
  }
  delay__us(50);
}

void lcd__set_cursor_to_next_line(void) { lcd__write_instr(set_cursor_to_next_line); }

void lcd__set_cursor_home(void) { lcd__write_instr(return_cursor_home); }