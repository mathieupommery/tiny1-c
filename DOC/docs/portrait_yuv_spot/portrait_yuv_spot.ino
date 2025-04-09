/*
 * YUV image display @ 384x288 pixesl (1.5X) with ported Infiray CCI interface and spot display
 */
extern "C" {
#include "all_config.h"
#include "data_rw.h"
#include "falcon_cmd.h"
#include "i2c.h"
#include "vdcmd.h"
}
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Wire.h>


// =======================================================================================
// Constants
//

// Be sure vospi Buffers are big enough for largest of VOSPI_TX_DUMMY_LEN or VOSPI_ROW_LEN
#define VOSPI_TX_DUMMY_LEN  512
#define VOSPI_WIDTH 256
#define VOSPI_HEIGHT 192
#define VOSPI_ROW_LEN (VOSPI_WIDTH*2)

#define DISP_WIDTH (VOSPI_WIDTH * 3 / 2)
#define DISP_HEIGHT (VOSPI_HEIGHT * 3 / 2)

// Frames per second requested of camera
#define VOSPI_FPS   25

// Lines per LCD update - 16-bit pixels, using 2 buffers
//   Must be a multiple of 3
#define DISP_DMA_LINES 33

// eSPI_TFT DMA buffer size (must be able to be allocated 2X in local memory)
#define DMA_BUF_LEN (DISP_WIDTH * DISP_DMA_LINES * 2)



// Notifications
#define NOTIFY_BUF_1 0x00000001
#define NOTIFY_BUF_2 0x00000002

// Macros
#define RANGE_LIMIT(x) (x > 255 ? 255 : (x < 0 ? 0 : x))
#define Notification(var, mask) ((var & mask) == mask)



// =======================================================================================
// Global variables
//

// Hardware Objects
TFT_eSPI tft = TFT_eSPI();

// SPI Interface
static spi_device_handle_t spi;
static spi_transaction_t spi_trans;


// Pins
const int pin_rst = 19;
const int pin_csn = 26;
const int pin_sclk = 25;
const int pin_mosi = 32;
const int pin_miso = 34;

//const int pin_trigger = 33;
const int pin_btn = 35;

// SPI clock rate
const int spi_hz = 20000000;

// I2C buffers
uint8_t tx_buf[64];
uint8_t rx_buf[64];

// SPI buffer
uint8_t vospiTxBuf[VOSPI_TX_DUMMY_LEN];
uint8_t vospiRxBuf1[VOSPI_TX_DUMMY_LEN];
uint8_t vospiRxBuf2[VOSPI_TX_DUMMY_LEN];

// RGB image buffers for use with eTFT DMA transfers
bool printImage = false;
uint16_t* rgbBuf[2];     // Allocated in PSRAM (holds original size image - double buffered)
uint16_t* rgbBufDMA[2];  // Allocated in internal (DMA) mem (holds partial magnified image sections)
uint16_t* rgbDumpBuf;    // Allocated in PSRAM (holds original image size coped from rgbBuf for dumping to external program)
SemaphoreHandle_t rgbMutex[2];

// Tasks to run on different CPUs
TaskHandle_t read_task_handle;
TaskHandle_t draw_task_handle;

// Palettes
#define NUM_PALETTES 9
bool change_palette = false;
uint8_t curPaletteIndex = 0;
uint8_t paletteList[] = {1, 3, 4, 5, 6, 7, 8, 9, 11};

// Preview setup
PreviewStartParam_t preview_param = {
  0, /* Path */
  0, /* 0: Camera, 1: Fixed pattern */
  VOSPI_WIDTH,
  VOSPI_HEIGHT,
  VOSPI_FPS,
  8 /* SPI Interface */
};

// Spot setup
IrPoint_t spot_param = {
  DISP_WIDTH / 2,
  DISP_HEIGHT / 2
};

// Info
float fps = 0;
float spot_temp = 0;



// =======================================================================================
// Arduino entry points
//
void setup() {
  Serial.begin(115200);
//  pinMode(pin_trigger, OUTPUT);
//  digitalWrite(pin_trigger, LOW);
  pinMode(pin_btn, INPUT);

  // Initialize the CCI interface
  if (vdcmd_init_by_type(VDCMD_I2C_VDCMD) != IR_SUCCESS) {
    Serial.println("CCI Init failed");
    while (1) delay(100);
  }

  // Initialize the VoSPI interface
  spi_bus_config_t spi_buscfg;
  memset(&spi_buscfg, 0, sizeof(spi_bus_config_t));
  spi_buscfg.miso_io_num=pin_miso;
  spi_buscfg.mosi_io_num=pin_mosi;
  spi_buscfg.sclk_io_num=pin_sclk;
  spi_buscfg.max_transfer_sz=VOSPI_TX_DUMMY_LEN;
  spi_buscfg.quadwp_io_num=-1;
  spi_buscfg.quadhd_io_num=-1;

  spi_device_interface_config_t devcfg;
  memset(&devcfg, 0, sizeof(spi_device_interface_config_t));
  devcfg.command_bits = 0;
  devcfg.address_bits = 0;
  devcfg.clock_speed_hz = spi_hz;
  devcfg.mode = 3;
  devcfg.spics_io_num = pin_csn;
  devcfg.queue_size = 1;
  devcfg.flags = 0;
  devcfg.cs_ena_pretrans = 2;

  // Setup our SPI transaction
  memset(&spi_trans, 0, sizeof(spi_transaction_t));
  spi_trans.tx_buffer = vospiTxBuf;
  spi_trans.rx_buffer = vospiRxBuf1;  
  
  if (spi_bus_initialize(HSPI_HOST, &spi_buscfg, 2) != ESP_OK) {
    Serial.println("spi_bus_init failed");
    while (1) delay(100);
  }
  if (spi_bus_add_device(HSPI_HOST, &devcfg, &spi) != ESP_OK) {
    Serial.println("spi_bus_add_device failed");
    while (1) delay(100);
  }

  // Initialize the LCD driver
  tft.init();
  tft.setSwapBytes(true);
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.initDMA();

  // Allocated shared raw image buffers
  rgbBuf[0] = (uint16_t*) heap_caps_malloc(VOSPI_WIDTH * VOSPI_HEIGHT * 2, MALLOC_CAP_SPIRAM);
  rgbBuf[1] = (uint16_t*) heap_caps_malloc(VOSPI_WIDTH * VOSPI_HEIGHT * 2, MALLOC_CAP_SPIRAM);
  rgbDumpBuf = (uint16_t*) heap_caps_malloc(VOSPI_WIDTH * VOSPI_HEIGHT * 2, MALLOC_CAP_SPIRAM);

  // Create mutex
  rgbMutex[0] = xSemaphoreCreateMutex();
  rgbMutex[1] = xSemaphoreCreateMutex();

  // Allocate eSPI_TFT DMA RGB buffer in local (DMA-capable) RAM
  rgbBufDMA[0] = (uint16_t*) heap_caps_malloc(DMA_BUF_LEN, MALLOC_CAP_DMA);
  if (rgbBufDMA[0] == NULL) {
    Serial.println("Allocate rgbBufDMA[0] failed");
    while (1) delay(100);
  }
  rgbBufDMA[1] = (uint16_t*) heap_caps_malloc(DMA_BUF_LEN, MALLOC_CAP_DMA);
  if (rgbBufDMA[1] == NULL) {
    Serial.println("Allocate rgbBufDMA[1] failed");
    while (1) delay(100);
  }

  // Delay for reset
  reset_tiny1c();
  delay(5000);

  // Start the Tiny1C video stream
  if (preview_start(&preview_param) != IR_SUCCESS) {
    Serial.println("preview_start failed");
    while (1) delay(100);
  }

  // Finally start tasks
  xTaskCreatePinnedToCore(read_task, "read_task", 3072, NULL, 1, &read_task_handle, 0);
  xTaskCreatePinnedToCore(draw_task, "draw_task", 3072, NULL, 1, &draw_task_handle, 1);
}


// Main loop (on CPU 1) handles input
void loop() {
  delay(1000);
}


// =======================================================================================
// Subroutines
//
void read_task(void * parameter)
{
  int count = 0;
  int row;
  int writerIndex = 0;
  uint16_t* bufP;
  uint16_t spot_raw;
  bool cur_btn;
  static bool prev_btn = false;
  static bool btn_down = false;
  char c;

  while (1) {
    // Grab the semaphore for the current writing buffer
    xSemaphoreTake(rgbMutex[writerIndex], portMAX_DELAY);

    // Start acquiring frame
    vospiTxBuf[0]= 0xAA;
    spi_trans.length = VOSPI_TX_DUMMY_LEN*8;
    spi_trans.rxlength = spi_trans.length;
    (void) spi_device_polling_transmit(spi, &spi_trans);
    delay(1);

    // Read a frame into the current buffer
    row = 0;
    bufP = rgbBuf[writerIndex];
    vospiTxBuf[0]= 0x55;
    spi_trans.length = VOSPI_ROW_LEN*8;
    spi_trans.rxlength = spi_trans.length;
    spi_trans.rx_buffer = ((row % 2) == 0) ? vospiRxBuf1 : vospiRxBuf2;
    (void) spi_device_polling_start(spi, &spi_trans, portMAX_DELAY);  // Prime pipeline
    while (row<VOSPI_HEIGHT) {
      // Wait for previous transfer
      (void) spi_device_polling_end(spi, portMAX_DELAY);

      if (row < (VOSPI_HEIGHT-1)) {
        // Start next transfer on all but last row
        spi_trans.rx_buffer = (((row+1) % 2) == 0) ? vospiRxBuf1 : vospiRxBuf2;
        (void) spi_device_polling_start(spi, &spi_trans, portMAX_DELAY);
      }

      // Convert and store previous transfer
      YUV422ToRGB565(((row % 2) == 0) ? vospiRxBuf1 : vospiRxBuf2, bufP, VOSPI_WIDTH);
      bufP += VOSPI_WIDTH;
      row += 1;
    }

    // Setup for next frame
    xSemaphoreGive(rgbMutex[writerIndex]);
    if (writerIndex == 0) {
      xTaskNotify(draw_task_handle, NOTIFY_BUF_1, eSetBits);
      writerIndex = 1;
    } else {
      xTaskNotify(draw_task_handle, NOTIFY_BUF_2, eSetBits);
      writerIndex = 0;
    }
    // Process commands
    while (Serial.available()) {
      c = Serial.read();
      process_rx_data(c);
    }

    // Look for palette changes
    cur_btn = (digitalRead(pin_btn) == LOW);
    if (cur_btn && prev_btn && !btn_down) {
      if (++curPaletteIndex == NUM_PALETTES) curPaletteIndex = 0;
      set_palette(paletteList[curPaletteIndex]);
    }
    btn_down = cur_btn && prev_btn;
    prev_btn = cur_btn;

    // Get the spot temp
    if ((count++ % 20) == 0) {
      if (tpd_get_point_temp_info(spot_param, &spot_raw) == IR_SUCCESS) {
        spot_temp = ((float) spot_raw) / 16.0 - 273.15;
      }
    }
  }
}


void draw_task(void * parameter)
{
  bool image_1 = false;
  bool image_2 = false;
  int draw_count;
  int frames = 1;
  uint32_t notification_value;
  unsigned long t1, t2;

  t1 = millis();

  while (1) {
    // Look for notifications
    notification_value = 0;
    if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
      if (Notification(notification_value, NOTIFY_BUF_1)) {
        image_1 = true;
      }
      if (Notification(notification_value, NOTIFY_BUF_2)) {
        image_2 = true;
      }
    }
    
    // Handle images
    draw_count = 0;
    if (image_1) {
      image_1 = false;
      draw_count += 1;
      draw_image(0);
      if ((++frames % 20) == 0) {
        t2 = millis();
        updateFPS((int) ((t2 - t1)/20));
        t1 = t2;
      }
      draw_spot_temp();
      draw_fps();
    }
    if (image_2) {
      image_2 = false;
      draw_count += 1;
      draw_image(1);
      if ((++frames % 20) == 0) {
        t2 = millis();
        updateFPS((int) ((t2 - t1)/20));
        t1 = t2;
      }
      draw_spot_temp();
      draw_fps();
    }
    if (draw_count == 2) {
      Serial.printf("2 frames/eval\n");
    }

  }
}


void draw_image(int n)
{
  int dmaIndex = 0;
  uint16_t row = 0;
  uint16_t col = 0;
  uint16_t dmaRow = 0;
  uint16_t* rgbP1;
  uint16_t* rgbP2;
  uint16_t* dmaP;
//  uint32_t s;
  uint16_t s1, s2, s3, s4;

  // Screendump is a special case (read "hack")
  if (printImage) {
    printImage = false;
    xSemaphoreTake(rgbMutex[n], portMAX_DELAY);
    
    // Copy the image to the screendump buffer
    memcpy((uint8_t*) rgbDumpBuf, (uint8_t*) rgbBuf[n], 2*VOSPI_WIDTH*VOSPI_HEIGHT);

    // Dump the image
    Serial.printf("\n");
    Serial.printf("W: %d\n", VOSPI_WIDTH);
    Serial.printf("H: %d\n", VOSPI_HEIGHT);
    for (int i=0; i<(VOSPI_WIDTH*VOSPI_HEIGHT); i++) {
      if ((i%32) == 0) {
        Serial.printf("\nFB: ");
      }
      Serial.printf("%x ", rgbDumpBuf[i]);
    }
    Serial.printf("\n");
    
    xSemaphoreGive(rgbMutex[n]);
    return;
  }

  tft.startWrite();
  
  // Get access
  xSemaphoreTake(rgbMutex[n], portMAX_DELAY);

  // Scale the image 1.5X with an unrolled loop
  // Send the image to the display a section at a time
  rgbP1 = rgbBuf[n];
  dmaP = rgbBufDMA[dmaIndex];
  while (row<DISP_HEIGHT) {
    // Row 0, 3, 6, ... {DISP_HEIGHT-3}
    for (col=0; col<DISP_WIDTH; col += 3) {
      // Col 0, 3, 6, ... {DISP_WIDTH-3}
      *dmaP++ = *rgbP1;

      // Col 1, 4, 7, ... {DISP_WIDTH-2}
//      s = *rgbP1++;
//      s += *rgbP1;
//      *dmaP++ = s / 2;
      s1 = *rgbP1++;
      s2 = *rgbP1;
      *dmaP++ = sumRGB2(s1, s2);

      // Col 2, 5, 8, ... {DISP_WIDTH-1}
      *dmaP++ = *rgbP1++;
    }

    // Row 1, 4, 7, ... {DISP_HEIGHT-2}
    rgbP2 = rgbP1;
    rgbP1 = rgbP1 - VOSPI_WIDTH;
    for (col=0; col<DISP_WIDTH; col += 3) {
//      s = *rgbP1;
//      s += *rgbP2;
//      *dmaP++ = s / 2;
      s1 = *rgbP1;
      s2 = *rgbP2;
      *dmaP++ = sumRGB2(s1, s2);

//      s = *rgbP1++;
//      s += *rgbP2++;
//      s += *rgbP1;
//      s += *rgbP2;
//      *dmaP++ = s / 4;
      s1 = *rgbP1++;
      s2 = *rgbP2++;
      s3 = *rgbP1;
      s4 = *rgbP2;
      *dmaP++ = sumRGB4(s1, s2, s3, s4);

//      s = *rgbP1++;
//      s += *rgbP2++;
//      *dmaP++ = s / 2;
      s1 = *rgbP1++;
      s2 = *rgbP2++;
      *dmaP++ = sumRGB2(s1, s2);
    }

    // Row 2, 5, 8, ... {DISP_HEIGHT-1}
    for (col=0; col<DISP_WIDTH; col += 3) {
      *dmaP++ = *rgbP1;

//      s = *rgbP1++;
//      s += *rgbP1;
//      *dmaP++ = s / 2;
      s1 = *rgbP1++;
      s2 = *rgbP1;
      *dmaP++ = sumRGB2(s1, s2);

      *dmaP++ = *rgbP1++;
    }
    row += 3;

    // Initiate an update when the DMA buffer is full
    if ((row % DISP_DMA_LINES) == 0) {
      tft.dmaWait();
      tft.pushImageDMA(0, dmaRow, DISP_WIDTH, DISP_DMA_LINES, rgbBufDMA[dmaIndex]);
      if (++dmaIndex == 2) dmaIndex = 0;
      dmaP = rgbBufDMA[dmaIndex];
      dmaRow += DISP_DMA_LINES;
    } else if (row == DISP_HEIGHT) {
      tft.dmaWait();
      tft.pushImageDMA(0, dmaRow, DISP_WIDTH, row % DISP_DMA_LINES, rgbBufDMA[dmaIndex]);
    }
  }

  xSemaphoreGive(rgbMutex[n]);
  tft.dmaWait();
  tft.endWrite();
}


void draw_fps()
{
  tft.fillRect(440, 310, 30, 10, TFT_BLACK);
  tft.setCursor(440, 310);
  tft.print(fps);
}


void draw_spot_temp()
{
  tft.fillRect(220, 305, 60, 15, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(220, 305);
  tft.print(spot_temp);
  tft.setTextSize(1);
  tft.fillRect(spot_param.x, spot_param.y, 2, 2, TFT_WHITE);
}


void reset_tiny1c()
{
  pinMode(pin_rst, OUTPUT);
  digitalWrite(pin_rst, LOW);
  delay(100);
  digitalWrite(pin_rst, HIGH);
  pinMode(pin_rst, INPUT_PULLUP);
}


void set_palette(uint8_t p)
{
  if ((p > 0) && (p < 12)) {
    (void) pseudo_color_set(PREVIEW_PATH0, (pseudo_color_types) p);
  }
}


void YUV422ToRGB565(const uint8_t *yuv_buf, uint16_t *rgb_buf, int width)
{
  uint8_t* rgb_byte = (uint8_t*) rgb_buf;
  int cols;
  int y, u, v, r, g, b;
  int y_pos,u_pos,v_pos;
 
 
  y_pos = 0;
  u_pos = 1;
  v_pos = 3;
 
  for (cols = 0; cols < width; cols++) {
    y = yuv_buf[y_pos];
    u = yuv_buf[u_pos] - 128;
    v = yuv_buf[v_pos] - 128;

    r = RANGE_LIMIT(y + v + ((v * 103) >> 8));
    g = RANGE_LIMIT(y - ((u * 88) >> 8) - ((v * 183) >> 8));
    b = RANGE_LIMIT(y + u + ((u * 198) >> 8));

    // Pixel : RRRR RGGG GGGB BBBB
    *rgb_buf++ = (((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3));
    // Swapped Pixel for LCD: GGGB BBBB RRRR RGGG
    //*rgb_byte++ = (r & 0xf8) | (g >> 5);
    //*rgb_byte++ = ((g & 0x1c) << 3) | ((b & 0xf8) >> 3);
    
    y_pos += 2;

    if (cols & 0x01) {
      u_pos += 4;
      v_pos += 4;
    }
  }
}

void updateFPS(int msec)
{
  fps = 1000.0 / (float) msec;
}


uint16_t sumRGB2(uint16_t s1, uint16_t s2)
{
  uint16_t r, g, b;
  r = ((s1 >> 11) + (s2 >> 11)) / 2;
  g = (((s1 & 0x07E0) >> 5) + ((s2 & 0x07E0) >> 5)) / 2;
  b = ((s1 & 0x001F) + (s2 & 0x001F)) / 2;

  return (r << 11) | (g << 5) | b;
}


uint16_t sumRGB4(uint16_t s1, uint16_t s2, uint16_t s3, uint16_t s4)
{
  uint16_t r, g, b;
  r = ((s1 >> 11) + (s2 >> 11) + (s3 >> 11) + (s4 >> 11)) / 4;
  g = (((s1 & 0x07E0) >> 5) + ((s2 & 0x07E0) >> 5) + ((s3 & 0x07E0) >> 5) + ((s4 & 0x07E0) >> 5)) / 4;
  b = ((s1 & 0x001F) + (s2 & 0x001F) + (s3 & 0x001F) + (s4 & 0x001F)) / 4;

  return (r << 11) | (g << 5) | b;
}
