// Lenia continuous cellular automaton //

#include "hardware/structs/rosc.h"
#include "st7735_lcd.pio.h"

#define PIN_DIN   11
#define PIN_CLK   10
#define PIN_CS    13
#define PIN_DC    9
#define PIN_RESET 7

PIO pio = pio0;
uint sm = 0;
uint offset;

#define SERIAL_CLK_DIV 1.f

#define WIDTH   80
#define HEIGHT  160
#define SCR     (WIDTH*HEIGHT)

float mu = 0.15f;
float sigma = 0.015f;
float dt = 0.3f;
int R = 13;

float *field, *nextField, *sat;

float randomf(float minf, float maxf) { return minf + (rand() % (1UL << 31)) * (maxf - minf) / (1UL << 31); }
uint16_t color565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }

uint16_t grayLUT[256];

#define LUT_SIZE 1024
float growthLUT[LUT_SIZE];

void precache_growth() {

  for (int i = 0; i < LUT_SIZE; i++) {
    float x = (float)i / (LUT_SIZE - 1);
    float diff = (x - mu) / sigma;
    growthLUT[i] = expf(-0.5f * diff * diff) * 2.0f - 1.0f;
  }

}

static const uint8_t st7735_init_seq[] = {
  1, 20, 0x01,
  1, 10, 0x11,
  2, 2, 0x3A, 0x05,
  2, 0, 0x36, 0x08,
  5, 0, 0x2A, 0x00, 0x18, 0x00, 0x67,
  5, 0, 0x2B, 0x00, 0x00, 0x00, 0x9F,
  1, 2, 0x20,
  1, 2, 0x13,
  1, 2, 0x29,
  0
};

static inline void lcd_set_dc_cs(bool dc, bool cs) {
    sleep_us(1);
    gpio_put_masked((1u<<PIN_DC)|(1u<<PIN_CS),
        (!!dc<<PIN_DC)| (!!cs<<PIN_CS));
    sleep_us(1);
}

static inline void lcd_write_cmd(PIO pio, uint sm,
                                 const uint8_t *cmd, size_t count) {
    st7735_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(0,0);
    st7735_lcd_put(pio, sm, *cmd++);
    if (count >= 2) {
        lcd_set_dc_cs(1,0);
        for (size_t i=0;i<count-1;i++)
            st7735_lcd_put(pio, sm, *cmd++);
    }
    lcd_set_dc_cs(1,1);
}

static inline void lcd_init(PIO pio, uint sm,
                            const uint8_t *init_seq) {
    const uint8_t *cmd = init_seq;
    while (*cmd) {
        lcd_write_cmd(pio, sm, cmd+2, *cmd);
        sleep_ms(*(cmd+1)*5);
        cmd += *cmd + 2;
    }
}

static inline void st7735_start_pixels(PIO pio, uint sm) {
    uint8_t cmd = 0x2C;
    lcd_write_cmd(pio, sm, &cmd, 1);
    lcd_set_dc_cs(1,0);
}

static inline void seed_random_from_rosc() {

  uint32_t random = 0;
  volatile uint32_t *rnd_reg = (uint32_t *)(ROSC_BASE + ROSC_RANDOMBIT_OFFSET);
  for (int k = 0; k < 32; k++) {
    random = (random << 1) | ((*rnd_reg) & 1);
  }
  srand(random);

}

void setup() {

  seed_random_from_rosc();

  offset = pio_add_program(pio, &st7735_lcd_program);
  st7735_lcd_program_init(pio, sm, offset, PIN_DIN, PIN_CLK, SERIAL_CLK_DIV);

  gpio_init(PIN_CS);
  gpio_init(PIN_DC);
  gpio_init(PIN_RESET);
  gpio_set_dir(PIN_CS, GPIO_OUT);
  gpio_set_dir(PIN_DC, GPIO_OUT);
  gpio_set_dir(PIN_RESET, GPIO_OUT);
  gpio_put(PIN_CS,1);
  gpio_put(PIN_RESET,1);

  lcd_init(pio, sm, st7735_init_seq);

  field = (float*)malloc(SCR * sizeof(float));
  nextField = (float*)malloc(SCR * sizeof(float));
  sat = (float*)malloc(SCR * sizeof(float));
  
  mu = randomf(0.13f, 0.20f);
  sigma = randomf(0.012f, 0.022f);

  for (int i = 0; i < 256; i++) grayLUT[i] = color565(i, i, i);
  for(int i = 0; i < SCR; i++) field[i] = (randomf(0.0f, 1.0f) > 0.75f) ? randomf(0.0f, 1.0f) : 0.0f;

  precache_growth();

}

void loop() {

  st7735_start_pixels(pio, sm);

  for (int y = 0; y < HEIGHT; y++) {
    float rowSum = 0;
    for (int x = 0; x < WIDTH; x++) {
      rowSum += field[x + y * WIDTH];
      sat[x + y * WIDTH] = (y > 0 ? sat[x + (y-1) * WIDTH] : 0) + rowSum;
    }
  }

  int r_inner = R;
  int r_side = R * 0.7f; 

  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
            
      int x1a = max(0, x - r_inner), x1b = min(WIDTH - 1, x + r_inner);
      int y1a = max(0, y - r_side),  y1b = min(HEIGHT - 1, y + r_side);
      float s1 = sat[x1b + y1b*WIDTH] - sat[x1a + y1b*WIDTH] - sat[x1b + y1a*WIDTH] + sat[x1a + y1a*WIDTH];
      float a1 = (x1b - x1a) * (y1b - y1a);

      int x2a = max(0, x - r_side),  x2b = min(WIDTH - 1, x + r_side);
      int y2a = max(0, y - r_inner), y2b = min(HEIGHT - 1, y + r_inner);
      float s2 = sat[x2b + y2b*WIDTH] - sat[x2a + y2b*WIDTH] - sat[x2b + y2a*WIDTH] + sat[x2a + y2a*WIDTH];
      float a2 = (x2b - x2a) * (y2b - y2a);

      int x3a = max(0, x - r_side),  x3b = min(WIDTH - 1, x + r_side);
      int y3a = max(0, y - r_side),  y3b = min(HEIGHT - 1, y + r_side);
      float s3 = sat[x3b + y3b*WIDTH] - sat[x3a + y3b*WIDTH] - sat[x3b + y3a*WIDTH] + sat[x3a + y3a*WIDTH];
      float a3 = (x3b - x3a) * (y3b - y3a);

      float area = max(1.0f, a1 + a2 - a3);
      float avg = (s1 + s2 - s3) / area;
      int lut_idx = (int)(avg * (LUT_SIZE - 1));
      if (lut_idx < 0) lut_idx = 0;
      if (lut_idx >= LUT_SIZE) lut_idx = LUT_SIZE - 1;

      float g_val = growthLUT[lut_idx];
      float val = field[x + y * WIDTH] + dt * g_val;
      float res = constrain(val, 0.0f, 1.0f);
            
      nextField[x + y * WIDTH] = res;
      uint16_t image = grayLUT[(uint8_t)(res * 255.0f)];
      
      st7735_lcd_put(pio, sm, image>>8);
      st7735_lcd_put(pio, sm, image&0xFF);

    }
  }

  float* temp = field; 
  field = nextField; 
  nextField = temp;
  
}