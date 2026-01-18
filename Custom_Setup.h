// Custom TFT_eSPI configuration for ESP32 Audiobook Player
// 2.4" ST7789 Display (320x240) with EC11 Rotary Encoder Module
//
// To use this file:
// 1. Copy this file to the TFT_eSPI library folder
// 2. Edit TFT_eSPI/User_Setup_Select.h
// 3. Comment out the default setup and include this file:
//    #include <../../../Custom_Setup.h>
//
// Or simply copy the contents below into User_Setup.h

// ============================================================================
// DISPLAY DRIVER
// ============================================================================
// Only enable ONE driver

#define ST7789_DRIVER      // Full configuration option for ST7789 240x240 and 240x320
//#define ST7789_2_DRIVER  // Alternative ST7789 driver (not needed for this display)

// ============================================================================
// DISPLAY RESOLUTION
// ============================================================================
// For ST7789, ST7735, ILI9163, and ILI9341 based displays these options can be used
// Set the display size for ST7789 (default is square 240x240)

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ============================================================================
// FONTS
// ============================================================================
// Font loading is enabled by default. Disable below if not needed
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
//#define LOAD_FONT8N // Font 8. Alternative to Font 8 above, slightly narrower, so 3 digits fit a 160 pixel TFT
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

// ============================================================================
// SMOOTH FONTS (optional, uses more memory)
// ============================================================================
// Comment out the #defines below with // to stop that font being loaded
// If all fonts are loaded the extra FLASH space required is about 17Kbytes...
// To save space only enable the fonts you need!

#define SMOOTH_FONT

// ============================================================================
// SPI PIN CONFIGURATION FOR ESP32
// ============================================================================
// For ESP32 Dev board (only tested with ILI9341 display)
// The hardware SPI can be mapped to any pins

// ESP32 VSPI (SPI3) default pins:
// MOSI = 23, MISO = 19, SCLK = 18, SS = 5

// We're using these custom pins for the audiobook player:
#define TFT_MISO -1    // Not connected (display is output only)
#define TFT_MOSI 23    // SDA - Master Out Slave In
#define TFT_SCLK 18    // SCL - Serial Clock
#define TFT_CS   5     // Chip select control pin
#define TFT_DC   2     // Data Command control pin
#define TFT_RST  4     // Reset pin (could connect to RST pin on ESP32)

// ============================================================================
// BACKLIGHT CONTROL
// ============================================================================
// Do NOT define TFT_BL since we're controlling backlight via PWM in the sketch
// The backlight is connected to GPIO25 and controlled by our code with ledcWrite()

//#define TFT_BL 25          // LED back-light control pin (leave commented out!)
//#define TFT_BACKLIGHT_ON HIGH  // Level to turn ON back-light (leave commented out!)

// ============================================================================
// DISPLAY COLOR ORDER
// ============================================================================
// Some displays need color bytes to be swapped. Test with different settings if colors are wrong.

//#define TFT_RGB_ORDER TFT_RGB  // Colour order Red-Green-Blue
//#define TFT_RGB_ORDER TFT_BGR  // Colour order Blue-Green-Red

// If colors look wrong, uncomment one of the above lines and try uploading again

// ============================================================================
// DISPLAY INVERSION
// ============================================================================
// Some ST7789 displays have different inversion settings.
// If the display looks like a negative image, uncomment the line below:

//#define TFT_INVERSION_ON
//#define TFT_INVERSION_OFF

// ============================================================================
// SPI FREQUENCY
// ============================================================================
// Define the SPI clock frequency, this affects the graphics rendering speed
// Too fast and the TFT driver will not keep up and display corruption appears
// Acceptable values are 27, 40, 80 MHz for ESP32 (80MHz is default)

//#define SPI_FREQUENCY  27000000  // 27MHz - Safe conservative speed
//#define SPI_FREQUENCY  40000000  // 40MHz - Try this if 27MHz works well
//#define SPI_FREQUENCY  80000000  // 80MHz - Maximum speed, may cause issues
#define SPI_FREQUENCY  10000000  // 10MHz - Working speed for this display

// Optional reduced SPI frequency for reading TFT (not used in this project)
#define SPI_READ_FREQUENCY  20000000

// ============================================================================
// SPI PORT (ESP32 specific)
// ============================================================================
// The ESP32 has 2 free SPI ports i.e. VSPI and HSPI, the VSPI is the default.
// If the VSPI port is in use and pins are not accessible (e.g. WROVER) then
// uncomment the following line to use the HSPI port instead:

//#define USE_HSPI_PORT

// ============================================================================
// TOUCH SCREEN (not used in this project)
// ============================================================================
// The XPT2046 requires a lower SPI clock rate of 2.5MHz so we define that here
// This is for touch screen support which we don't have on this module

#define SPI_TOUCH_FREQUENCY  2500000

// ============================================================================
// DISPLAY SPECIFIC INIT COMMANDS (ST7789 240x320)
// ============================================================================
// Some ST7789 displays need specific initialization. If display doesn't work,
// uncomment one of these defines:

#define CGRAM_OFFSET      // Library will add offsets for some ST7789 displays

// For 240x320 ST7789 displays (our display)
//#define ST7789_240x320    // Disabled - using generic ST7789 init instead

// ============================================================================
// ADDITIONAL OPTIONS
// ============================================================================

// #define SUPPORT_TRANSACTIONS  // Enable transaction support (for shared SPI bus)
// #define TFT_PARALLEL_8_BIT    // Use 8-bit parallel instead of SPI (not applicable)

// ============================================================================
// DEBUG OPTIONS
// ============================================================================
// Uncomment to enable debug output to Serial Monitor

//#define TFT_eSPI_DEBUG

// ============================================================================
// END OF CONFIGURATION
// ============================================================================
