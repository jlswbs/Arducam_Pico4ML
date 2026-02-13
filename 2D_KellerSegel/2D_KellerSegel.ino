// Keller-Segel system simulation //

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
#define AGENTS  500

uint8_t chem[WIDTH][HEIGHT];
uint8_t nextChem[WIDTH][HEIGHT];

struct Agent {
  float x, y, angle;
};

Agent agents[AGENTS];

const float sensorAngle = 0.78f;
const float sensorDist = 3.0f;
const float stepSize = 1.0f;

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

float sense(Agent &a, float angleOffset) {

  float lookAngle = a.angle + angleOffset;
  int sx = (int)(a.x + cos(lookAngle) * sensorDist);
  int sy = (int)(a.y + sin(lookAngle) * sensorDist);
  
  if (sx >= 0 && sx < WIDTH && sy >= 0 && sy < HEIGHT) {
    return chem[sx][sy];
  }
  return -1;

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
  
  for (int i = 0; i < AGENTS; i++) {
    agents[i] = {(float)(rand()%WIDTH), (float)(rand()%HEIGHT), (float)(rand() % 628) / 100.0f};
  }

}

void loop() {

  st7735_start_pixels(pio, sm);

  for (int i = 0; i < AGENTS; i++) {

    float fwd = sense(agents[i], 0);
    float left = sense(agents[i], -sensorAngle);
    float right = sense(agents[i], sensorAngle);

    if (fwd > left && fwd > right) {}
    else if (fwd < left && fwd < right) {
      agents[i].angle += (rand() % 2 == 0 ? 1 : -1) * 0.2f;
    }
    else if (left > right) agents[i].angle -= 0.2;
    else if (right > left) agents[i].angle += 0.2;

    agents[i].x += cosf(agents[i].angle) * stepSize;
    agents[i].y += sinf(agents[i].angle) * stepSize;

    if (agents[i].x < 0 || agents[i].x >= WIDTH) agents[i].angle = PI - agents[i].angle;
    if (agents[i].y < 0 || agents[i].y >= HEIGHT) agents[i].angle = -agents[i].angle;

    int ix = (int)agents[i].x;
    int iy = (int)agents[i].y;
    if (ix >= 0 && ix < WIDTH && iy >= 0 && iy < HEIGHT) {
      chem[ix][iy] = 255;
    }

  }
  
  for (int y = 0; y < HEIGHT; y++) {

    for (int x = 0; x < WIDTH; x++) {

      uint16_t val = chem[x][y];
      
      uint8_t r = (100 * val) >> 8;
      uint8_t g = (150 * val) >> 8;
      uint8_t b = val;
      uint16_t image = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
      
      st7735_lcd_put(pio, sm, image>>8);
      st7735_lcd_put(pio, sm, image&0xFF);

      nextChem[x][y] = (val * 9) / 10;

    }

  }

  memcpy(chem, nextChem, sizeof(chem));

}