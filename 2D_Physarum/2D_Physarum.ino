// 2D Physarum growth //

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

#define WIDTH   80
#define HEIGHT  160
#define SCR     (WIDTH * HEIGHT)
#define SERIAL_CLK_DIV 1.f

#define ITER  2000
#define NUM   8
#define MAX_TRAIL 100

uint16_t grid[WIDTH][HEIGHT];
uint16_t trail[WIDTH][HEIGHT];
uint16_t coll[NUM];
uint16_t image;
int t, q;

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
    uint32_t r = 0;
    volatile uint32_t *rnd =
      (uint32_t *)(ROSC_BASE + ROSC_RANDOMBIT_OFFSET);
    for (int i=0;i<32;i++) {
        while (((*rnd)&1) == (((*rnd)>>1)&1));
        r = (r<<1) | ((*rnd)&1);
    }
    srand(r);
}

void rndseed(){

  for (int i = 0; i < NUM; i++) coll[i] = rand();

  for (int y = 0; y < HEIGHT; y++){  
    for (int x = 0; x < WIDTH; x++){
      
      if(x == 0 || x == 1 || x == WIDTH-2 || x == WIDTH-1 || y == 0 || y == 1 || y == HEIGHT-2 || y == HEIGHT-1) grid[x][y] = 1;
      else grid[x][y] = 0;

    }
  }
  
  for (int i = 1; i < NUM; i++){
    
    int x = 2 * (5 + rand()%(WIDTH/2)-5);
    int y = 2 * (5 + rand()%(HEIGHT/2)-5);
    if(grid[x][y] == 0) grid[x][y] = 1000+(i*100);

  }
  
}

void nextstep(){

  for (int i = 0; i < ITER; i++){
    int x = 2 * (1 + rand()%(WIDTH/2)-1);
    int y = 2 * (1 + rand()%(HEIGHT/2)-1);
    
    if(grid[x][y] >= 1000 && grid[x][y] < 2000){
      q = (grid[x][y]/100) - 10;
      
      if(grid[x+2][y] == 0){ 
        grid[x+2][y] = q*100; 
        grid[x+1][y] = q*100;
        trail[x+1][y] = 10;
      }
      if(grid[x][y+2] == 0){ 
        grid[x][y+2] = q*100; 
        grid[x][y+1] = q*100;
        trail[x][y+1] = 10;
      }
      if(grid[x-2][y] == 0){ 
        grid[x-2][y] = q*100; 
        grid[x-1][y] = q*100;
        trail[x-1][y] = 10;
      }
      if(grid[x][y-2] == 0){ 
        grid[x][y-2] = q*100; 
        grid[x][y-1] = q*100;
        trail[x][y-1] = 10;
      }
      
      grid[x][y] = q*100;
    }
    
    if(grid[x][y] >= 100 && grid[x][y] < 1000){
      q = grid[x][y]/100;

      if(x < 2 || x >= WIDTH-2 || y < 2 || y >= HEIGHT-2) continue;
      
      int sensors[4];
      sensors[0] = trail[x+2][y];
      sensors[1] = trail[x][y+2];
      sensors[2] = trail[x-2][y];
      sensors[3] = trail[x][y-2];
      
      int maxDir = 0;
      int maxVal = sensors[0];
      for(int d = 1; d < 4; d++){
        if(sensors[d] > maxVal){
          maxVal = sensors[d];
          maxDir = d;
        }
      }

      int bias = rand() % 100;
      if(bias < 70 && maxVal > 0){
        t = maxDir + 1;
      } else {
        t = 1 + rand()%4;
      }
      
      int dx = (t==1)?2:(t==3)?-2:0;
      int dy = (t==2)?2:(t==4)?-2:0;
      
      if(x+dx < 0 || x+dx >= WIDTH || y+dy >= HEIGHT || y+dy < 0) continue;
      
      if(grid[x+dx][y+dy] == 0){
        grid[x+dx][y+dy] = q*100;
        grid[x+dx/2][y+dy/2] = q*100;
        trail[x][y] = (trail[x][y] + 5 > MAX_TRAIL) ? MAX_TRAIL : trail[x][y] + 5;
      }

      else if(grid[x+dx][y+dy] >= 100 && grid[x+dx][y+dy] < 1000){
        int otherQ = grid[x+dx][y+dy]/100;
        if(otherQ != q){
          if(rand()%2 == 0){
            grid[x+dx][y+dy] = q*100;
            grid[x+dx/2][y+dy/2] = q*100;
          }
        }
      }
    }
    
    if(trail[x][y] > 0) trail[x][y] = trail[x][y] - 1;
  }

}

void setup() {

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

    seed_random_from_rosc();

    rndseed();

}

void loop() {

    nextstep();

    st7735_start_pixels(pio, sm);

    for (int y = 0; y < HEIGHT; y++){

        for (int x = 0; x < WIDTH; x++){
    
            if(grid[x][y] >= 100 && grid[x][y] < 1000){
                q = (grid[x][y] / 100) % NUM;
                image = coll[q];    
            } else image = 0;

            st7735_lcd_put(pio, sm, image>>8);
            st7735_lcd_put(pio, sm, image&0xFF);
      
        }

    }

}