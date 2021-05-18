#include "FreeRTOS.h"
#include "acceleration.h"
#include "board_io.h"
#include "decoder.h"
#include "delay.h"
#include "ff.h"
#include "gpio.h"
#include "gpio_isr.h"
#include "gpio_lab.h"
#include "lcd.h"
#include "lpc_peripherals.h"
#include "queue.h"
#include "semphr.h"
#include "sj2_cli.h"
#include "sl_string.h"
#include "task.h"
#include <stdio.h>

#include <string.h>

#define NAME_SIZE 32

#define SONG_BLOCK_SIZE 512

typedef char songname_t[32];
typedef char songdata_t[SONG_BLOCK_SIZE];

static void read_mp3_file(songname_t name);

static void mp3_reader_task(void *p);

static void mp3_decoder_send_block(songdata_t data);

static void mp3_player_task(void *p);

typedef struct {
  char song_name[NAME_SIZE];
  int song_index;
} track_info_t;

/* Global Variables */
const gpio_s play_button = {0, 29};
const gpio_s cursor_button = {0, 30};
bool play_pause_toggle = false;
int lcd_cursor_index = 0;
track_info_t track_list[30];
char current_song[NAME_SIZE];

/* Queues*/
QueueHandle_t Q_songname;
QueueHandle_t Q_songdata;

/* Sempahore */
SemaphoreHandle_t sem_play_pause;

/* Mutex */
SemaphoreHandle_t mux_volume;

/* TASKS */
void lcd_task(void *p);
void volume_control(void *p);

TaskHandle_t mp3_player_handle;

/* ISR */
void play_button_isr(void);
void move_cursor_isr(void);

/* FUNCTIONS */
void mp3_list_songs(void);
void button_debouncer(uint8_t port_num, uint8_t pin_num);


int main(void) {
  gpio_light_off();
  sj2_cli__init();
  mp3_list_songs();
  lcd__initialize();
  initialize_decoder();
  acceleration__init();

  mux_volume = xSemaphoreCreateMutex();


  /**********************************************
   ***********PLAY PAUSE INTERRUPT***************
   **********************************************/
  sem_play_pause = xSemaphoreCreateBinary();
  lpc_peripheral__enable_interrupt(LPC_PERIPHERAL__GPIO, gpio0__interrupt_dispatcher, "pause_int");
  gpio0__attach_interrupt(play_button.pin_number, GPIO_INTR__FALLING_EDGE, play_button_isr);

  lpc_peripheral__enable_interrupt(LPC_PERIPHERAL__GPIO, gpio0__interrupt_dispatcher, "next_song_on_cursor_int");
  gpio0__attach_interrupt(cursor_button.pin_number, GPIO_INTR__FALLING_EDGE, move_cursor_isr);
  NVIC_EnableIRQ(GPIO_IRQn);

  /**********************************************
   ***********TASK IMPLEMENTATION***************
   **********************************************/
  Q_songname = xQueueCreate(1, sizeof(songname_t));
  Q_songdata = xQueueCreate(1, sizeof(songdata_t));
  xTaskCreate(mp3_reader_task, "mp3_reader", 2048 / sizeof(void *), NULL, PRIORITY_LOW, NULL);
  xTaskCreate(mp3_player_task, "mp3_player", 2048 / sizeof(void *), NULL, PRIORITY_MEDIUM, &mp3_player_handle);
  xTaskCreate(lcd_task, "lcd", 2048 / sizeof(void *), NULL, PRIORITY_HIGH, NULL);
  xTaskCreate(volume_control, "volume", 2048 / sizeof(void *), NULL, PRIORITY_MEDIUM, NULL);
  vTaskStartScheduler();
  return 0;
}


static void mp3_player_task(void *p) {
  songdata_t songdata;
  while (1) {
    if (xQueueReceive(Q_songdata, &songdata[0], portMAX_DELAY)) {
      mp3_decoder_send_block(songdata);
    }
  }
}

static void mp3_reader_task(void *p) {
  while (1) {
    songname_t name = {0};
    if (xQueueReceive(Q_songname, &name[0], portMAX_DELAY)) {
      if (play_pause_toggle) {
        read_mp3_file(name);
      } else {
      }
    }
  }
}

void lcd_task(void *p) {
  char indicator[] = "->";
  char empty_space[] = " ";
  while (1) {
    lcd__clear_display();
    lcd__write_string(indicator);
    lcd__write_string(track_list[lcd_cursor_index].song_name);
    lcd__set_cursor_to_next_line();
    lcd__write_string(empty_space);
    lcd__write_string(track_list[lcd_cursor_index + 1].song_name);
    lcd__set_cursor_home();
    if (!xQueueSend(Q_songname, track_list[lcd_cursor_index].song_name, 1)) {
      xQueueReset(Q_songname);
    }
    if (xSemaphoreTake(sem_play_pause, portMAX_DELAY)) {
      if (play_pause_toggle) {
        vTaskResume(mp3_player_handle);
      } else if (!play_pause_toggle) {
        vTaskSuspend(mp3_player_handle);
      }

      if (!xQueueSend(Q_songname, track_list[lcd_cursor_index].song_name, 1)) {
        xQueueReset(Q_songname);
      }
    }
  }
}

void volume_control(void *p) {
  acceleration__axis_data_s control;
  uint16_t volume_control = 0;
  uint16_t offset = 0x7F7F;
  uint8_t volume_reg = 0x0B;
  uint16_t total_silence = 0xFEFE;
  uint16_t max_volume = 0x0000;
  while (1) {
    if (xSemaphoreTake(mux_volume, portMAX_DELAY)) {
      control = acceleration__get_data();
      volume_control = control.x / 4;
      volume_control |= (volume_control << 8);
      if (volume_control > total_silence) {
        volume_control = total_silence;
      } else if (volume_control < max_volume) {
        volume_control = max_volume;
      }
      MP3_decoder__sci_write(volume_reg, volume_control);
      xSemaphoreGive(mux_volume);
    }
    vTaskDelay(1500);
  }
}

/* FUNCTION DECLARATION */

void populate_song_list(char *song_name, size_t index) {
  int length = strlen(song_name);
  length = length - 4;

  if (!(song_name[0] == '\0')) {
    strncpy(track_list[index].song_name, song_name, length);
    track_list[index].song_index = index + 1;
  }
}

void mp3_list_songs(void) {
  FRESULT f_result;
  DIR dir;
  FILINFO f_info;
  int song_index = 0;
  const char folder_name[5] = "/";
  f_result = f_opendir(&dir, folder_name);
  if (f_result == FR_OK) {
    f_result = f_readdir(&dir, &f_info);
    while (f_info.fname[0] && f_result == FR_OK) {
      f_result = f_readdir(&dir, &f_info);
      populate_song_list(f_info.fname, song_index);
      song_index++;
    }
  }
  f_closedir(&dir);
}

void button_debouncer(uint8_t port_num, uint8_t pin_num) {
  int debouncer = 20;
  while (gpio0__get_level(port_num, pin_num)) {
    debouncer = 20;
  }
  while (!gpio0__get_level(port_num, pin_num) && debouncer != 0) {
    debouncer--;
  }
}
int current_song_index(char *current_song_name) {
  int index = 0;
  while (strncmp(current_song_name, track_list[index].song_name, strlen(track_list[index].song_name))) {
    index++;
  }
  return index;
}

static void read_mp3_file(songname_t name) {
  FIL file;
  UINT num_of_bytes_read = 0;
  gpio_s sw1 = board_io__get_sw1(); // Next Song
  gpio_s sw0 = board_io__get_sw0(); // Previous Song
  char dot_mp3[] = ".mp3";
  int next_song_index = 0;
  int previous_song_index = 0;
  strncat(name, dot_mp3, strlen(dot_mp3) + 1);
  if (FR_OK == f_open(&file, name, FA_READ)) {
    while (f_eof(&file) == 0) {
      if (xSemaphoreTake(mux_volume, portMAX_DELAY)) {
        if (gpio__get(sw1)) {
          // Next Song
          f_close(&file);
          delay__ms(150);
          next_song_index = current_song_index(name);
          next_song_index++;
          strcpy(name, track_list[next_song_index].song_name);
          strncat(name, dot_mp3, strlen(dot_mp3) + 1);
          f_open(&file, name, FA_READ);
          lcd_cursor_index = next_song_index;
          delay__ms(5);
          lcd__clear_display();
          lcd__write_string("->");
          lcd__write_string(track_list[lcd_cursor_index].song_name);
          lcd__set_cursor_to_next_line();
          lcd__write_string(" ");
          lcd__write_string(track_list[lcd_cursor_index + 1].song_name);
          lcd__set_cursor_home();
        } else if (gpio__get(sw0)) {
          // Previous song
          f_close(&file);
          delay__ms(150);
          previous_song_index = current_song_index(name);
          previous_song_index--;
          strcpy(name, track_list[previous_song_index].song_name);
          strncat(name, dot_mp3, strlen(dot_mp3) + 1);
          f_open(&file, name, FA_READ);
          lcd_cursor_index = previous_song_index;
          delay__ms(5);
          lcd__clear_display();
          lcd__write_string("->");
          lcd__write_string(track_list[lcd_cursor_index].song_name);
          lcd__set_cursor_to_next_line();
          lcd__write_string(" ");
          lcd__write_string(track_list[lcd_cursor_index + 1].song_name);
          lcd__set_cursor_home();
        }
        songdata_t buffer = {0};
        f_read(&file, buffer, sizeof(buffer), &num_of_bytes_read);
        xQueueSend(Q_songdata, buffer, portMAX_DELAY);
        xSemaphoreGive(mux_volume);
      }
    }
    f_close(&file);
  } else {
    ;
  }
}

static void mp3_decoder_send_block(songdata_t data) {
  for (size_t index = 0; index < sizeof(songdata_t); index++) {
    while (!mp3_decoder_needs_data()) {
    }
    spi_send_to_mp3_decoder(data[index]);
  }
}


void move_cursor_isr(void) {
  button_debouncer(cursor_button.port_number, cursor_button.pin_number);
  lcd_cursor_index++;
  if (track_list[lcd_cursor_index].song_name[0] == '\0') {
    lcd_cursor_index = 0;
  }
  if (!xSemaphoreGiveFromISR(sem_play_pause, NULL)) {
    ;
  }
}

void play_button_isr(void) {
  button_debouncer(play_button.port_number, play_button.pin_number);
  play_pause_toggle = !play_pause_toggle;
  xSemaphoreGiveFromISR(sem_play_pause, NULL);
}
