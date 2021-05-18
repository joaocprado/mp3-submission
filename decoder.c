#include "decoder.h"
#include "delay.h"
#include "gpio.h"
// Pins chosen in order of pins on SJ2 board for easy circuit building
static const gpio_s SCLK = {0, 1}; // slave clock pin 0.1
static const gpio_s SI = {1, 1};   // MOSI pin 1.1
static const gpio_s SO = {1, 4};   // MISO pin 1.4

static const gpio_s RST = {0, 8};   // Reset pin 0.8
static const gpio_s CS = {0, 26};   // Chip select pin 0.26
static const gpio_s XDCS = {1, 31}; // Data Chip Select pin 1.31
static const gpio_s DREQ = {1, 20}; // Data Request pin 1.20

static void enable_decoder_CS(void) { gpio__reset(CS); } // CS is active low reset sets CS to 0

static void disable_decoder_CS(void) { gpio__set(CS); } // CS set to 1 to disable the decoder

void MP3_decoder__sci_write(uint8_t address, uint16_t data) {
  enable_decoder_CS();
  ssp2__exchange_byte(0x2);
  ssp2__exchange_byte(address);
  ssp2__exchange_byte((data >> 8) & 0xFF);
  ssp2__exchange_byte((data >> 0) & 0xFF);
  disable_decoder_CS();
}
void MP3_decoder__sci_read(uint8_t address, uint16_t data) {
  uint16_t read_data;
  enable_decoder_CS();
  read_data = ssp2__exchange_byte(0x3);
  read_data = ssp2__exchange_byte(address);
  fprintf(stderr, "read_data = 0x%04X\n", read_data);
  read_data = ssp2__exchange_byte((data >> 8) & 0xFF);
  fprintf(stderr, "read_data = 0x%04X\n", read_data);
  read_data = ssp2__exchange_byte((data >> 0) & 0xFF);
  fprintf(stderr, "read_data = 0x%04X\n", read_data);
  disable_decoder_CS();
}

static void MP3_decoder__reset(void) {
  if (!gpio__get(RST)) {
    gpio__reset(RST);
    delay__ms(100);
    gpio__set(RST);
  }
  gpio__set(CS);
  gpio__set(XDCS);
  delay__ms(100);
  MP3_decoder__sci_write(0x0, 0x800 | 0x4);
  delay__ms(100);
  MP3_decoder__sci_write(0x3, 0x6000);
  delay__ms(100);
}

void initialize_decoder(void) {

  // Setting SPI pins for data transfer
  gpio__set_function(SI, GPIO__FUNCTION_4);
  gpio__set_function(SO, GPIO__FUNCTION_4);
  gpio__set_function(SCLK, GPIO__FUNCTION_4);

  // Setting GPIO pins
  gpio__set_as_output(RST);
  gpio__set_as_output(CS);
  gpio__set_as_output(XDCS);
  gpio__set_as_input(DREQ);

  ssp2__initialize(3000); // Change frequency
  MP3_decoder__reset();
}

bool mp3_decoder_needs_data(void) {
  // DREQ is high when it is ready to receive data
  return gpio__get(DREQ);
}

void spi_send_to_mp3_decoder(char data) { // From Figure 12
  gpio__reset(XDCS);         // Set Data Chip Select to 0. . XCS should be low for the full duration of the operation
  ssp2__exchange_byte(data); // Send data through SPI
  gpio__set(XDCS);           // Set Data Chip Select to 1 to signal end of data transmission
}
