#include <cstdio>

#include <nesdoug.h>
#include <neslib.h>

constexpr char kScreenWidth = 32;
constexpr char kScreenHeight = 30;
constexpr int kScreenSize = kScreenWidth * kScreenHeight;

constexpr char kPixelsPerTile = 8;

constexpr char hello[] = "Hello, NES!";

constexpr char background_pal[] = {
    0x0f, 0x10, 0x20, 0x30, // grayscale
    0x0f, 0x10, 0x20, 0x30, // grayscale
    0x0f, 0x10, 0x20, 0x30, // grayscale
    0x0f, 0x10, 0x20, 0x30, // grayscale
};

constexpr char sprite_pal[] = {
    0x0f, 0x10, 0x26, 0x30, // cogwheel
    0x0f, 0x11, 0x2a, 0x16, // explosions
    0x0f, 0x10, 0x20, 0x30, // unused
    0x0f, 0x10, 0x20, 0x30, // unused
};

void init_ppu() {
  // Disable the PPU so we can freely modify its state
  ppu_off();

  // Set up bufferd VRAM operations (see `multi_vram_buffer_horz` below)
  set_vram_buffer();

  // Use lower half of PPU memory for background tiles
  bank_bg(0);

  // Set the background palette
  pal_bg(background_pal);

  // Fill the background with space characters to clear the screen
  vram_adr(NAMETABLE_A);
  vram_fill(' ', kScreenSize);

  // Write a message
  vram_adr(NTADR_A(10, 10));
  vram_write(hello, sizeof(hello) - 1);

  // Use the upper half of PPU memory for sprites
  bank_spr(1);

  // Set the sprite palette
  pal_spr(sprite_pal);

  // Turn the PPU back on
  ppu_on_all();
}

struct Explosion {
  char x{};
  char y{};
  char timer{};
};

int main() {
  init_ppu();

  // Counters to cycle through palette colors, changing every half second
  char palette_color = 0;
  char counter = 0;

  // Cogwheel position
  char cog_x = 15 * kPixelsPerTile;
  char cog_y = 14 * kPixelsPerTile;

  // Circular buffer for explosion animations
  constexpr char kNumExplosions = 8;
  Explosion explosions[kNumExplosions];
  int explosion_head = 0;
  int explosion_tail = 0;

  // Store pad state across frames to check for changes
  char prev_pad_state = 0;

  for (;;) {
    // Wait for the NMI routine to end so we can start working on the next frame
    ppu_wait_nmi();

    // The OAM (object attribute memory) is an area of RAM that contains data about
    // all the sprites that will be drawn next frame.
    oam_clear();

    // Note: if you don't poll a controller during a frame, emulators will
    // report that as lag
    const char pad_state = pad_poll(0);

    if (!(prev_pad_state & PAD_A) && (pad_state & PAD_A)) {
      // Pressed A - create an explosion at the cogwheel location
      explosions[explosion_tail++] = {.x = cog_x, .y = cog_y, .timer = 31};

      // Wrap tail to 0 if necessary
      if (explosion_tail == kNumExplosions) explosion_tail = 0;

      // Eject oldest explosion if buffer is full
      if (explosion_tail == explosion_head) {
        ++explosion_head;
        if (explosion_head == kNumExplosions) explosion_head = 0;
      }
    }

    // Speed up when pressing B
    const char speed = pad_state & PAD_B ? 2 : 1;

    if (pad_state & PAD_UP) {
      cog_y -= speed;
    } else if (pad_state & PAD_DOWN) {
      cog_y += speed;
    }

    if (pad_state & PAD_LEFT) {
      cog_x -= speed;
    } else if (pad_state & PAD_RIGHT) {
      cog_x += speed;
    }

    prev_pad_state = pad_state;

    // TODO refactor
    int head = explosion_head;
    while (head > explosion_tail && explosions[head].timer) {
      oam_spr(explosions[head].x + 8, explosions[head].y + explosions[head].timer, 0x30 + (7 - (explosions[head].timer-- >> 2)), 1);
      if (explosions[head].timer == 0) {
        // Expired. Increment head
        if (++explosion_head == kNumExplosions) explosion_head = 0;
      }
      if(++head == kNumExplosions) head = 0;
    }
    while (head < explosion_tail && explosions[head].timer) {
      oam_spr(explosions[head].x + 8, explosions[head].y + explosions[head].timer, 0x30 + (7 - (explosions[head].timer-- >> 2)), 1);
      if (explosions[head].timer == 0) {
        // Expired. Increment head
        if (++explosion_head == kNumExplosions) explosion_head = 0;
      }
      if(++head == kNumExplosions) head = 0;
    }

    // Adding Cogwheel after the explosions means the explosions will be prioritized
    for (char row = 0; row < 3; ++row) {
      for (char col = 0; col < 3; ++col) {
        // Convert subpixels to pixels and add row/col
        char const sprite_x = cog_x + (col << 3);
        char const sprite_y = cog_y + (row << 3);

        // There are 16 tiles per row; shift by 4
        char const tile = (row << 4) + col;
        oam_spr(sprite_x, sprite_y, tile, 0);
      }
    }

    if(counter == 0) {
      if (++palette_color == 64) palette_color = 0;

      // Tell `neslib` that we want to do a buffered background data transfer
      // this frame
      char buffer[4];
      std::snprintf(buffer, sizeof(buffer), "$%02x", static_cast<int>(palette_color));
      multi_vram_buffer_horz(buffer, 3, NTADR_A(14, 12));
    }

    pal_col(0, palette_color);

    if (++counter == 30) counter = 0;
  }
}