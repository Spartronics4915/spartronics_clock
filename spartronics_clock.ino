/**
 * Spartronics sample code for Teensy LC based display via Adafruit NeoMatrix
 *
 * Parts/Libraries:
 *  - Teensy LC
 *  - Adafruit NeoMatrix
 *  - DS3231 RTC
 *  - AT24C32 for storage -- currently not used
 *
 * Libraries:
 *  - DebounceEvent button manager by Xose Perez
 *    https://github.com/xoseperez/debounceevent
 *  - DS1307RTC library (compatible w/ DS3231)
 *    https://www.pjrc.com/teensy/td_libs_DS1307RTC.html
 *
 * Usage:
 *  3 buttons control the behavior of the display
 *  - button 1: specifies mode for reprogramcurrentMing the display:
 *    prints scrolling message or coundown clock
 *  - button 2 & 3: unused -- button push will generate corresponding events
 *
 * Note: holding the DEC button down during boot will force setting
 *       the datetime via user input
 *
 * Note: Spartronics clock also hosts an AT24C32 which is a 32K EEPROM.
 *       It is currently not used.
 *       In the future, this can hold long messages or short novels.
 */

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <DebounceEvent.h>
#include <DS1307RTC.h>
#include <Time.h>
#include <TimeLib.h>
#include <Wire.h>
#include <stdbool.h>

#define DEBUG_ON    0

#define NUM_ELEMENTS(x) (sizeof(x) / sizeof(x[0]))

// Fun messages to print
static const char *messages[] = {
    "Good luck on finals",
    "You should join Spartronics next year",
    "Thank you to our lovely sponsors",
    "2026 Milstein Divison Winners",
    "FRC 4915"
};
static const char *months[] = {"Month 0", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/**
 *  Color names for various display modes
 */
typedef enum {
    COLOR_RED,
    COLOR_ORANGE,
    COLOR_YELLOW,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_PURPLE,
    // Insert new color names above this line
    COLOR_MAX
} ColorName_t;

// colors used for the matrix display
static const uint16_t _colors[] = {
    [COLOR_RED] = Adafruit_NeoMatrix::Color(128, 0, 0),
    [COLOR_ORANGE] = Adafruit_NeoMatrix::Color(128, 128, 0),
    [COLOR_YELLOW] = Adafruit_NeoMatrix::Color(90, 90, 0),
    [COLOR_GREEN] = Adafruit_NeoMatrix::Color(0, 128, 0),
    [COLOR_BLUE] = Adafruit_NeoMatrix::Color(0, 50, 170),
    [COLOR_PURPLE] = Adafruit_NeoMatrix::Color(128, 0, 128)
};

uint16_t timeColor = _colors[COLOR_YELLOW];
uint16_t colonColor = _colors[COLOR_BLUE];

typedef enum   // at each button mode press, display mode changes accordingly
{
    BLANK = 0,
    TIME = 1,
    DATE = 2,
    COUNTDOWN = 3,
    MESSAGE = 4,
} Mode;

Mode currentMode = DATE;  // tracks the state of the display
bool changeMode = true;
double timeTillModeChange = 6000;
int message = 0;
int messageCount = 5;
int messageColor = 0;
int messageX = 0;
const int cycleSpeed = 70;

/** Good reference for display configuration:
 *  https://learn.adafruit.com/adafruit-neopixel-uberguide/neomatrix-library
 *
 *  Spartronics display is setup as a 45x7 matrix.The first pixel is on top left,
 *  lines are arranged in rows as zig-zag order. The shield uses 800Khz (v2)
 *  pixels that expect GRB color data
 */
#define CONTROL_PIN     17
#define MATRIX_WIDTH    45
#define MATRIX_HEIGHT   7

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(MATRIX_WIDTH, MATRIX_HEIGHT,
  CONTROL_PIN,                                  // pin number
  NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +        // matrix layout
  NEO_MATRIX_ROWS    + NEO_MATRIX_ZIGZAG,       // flags describe our panel
  NEO_GRB            + NEO_KHZ800);             // pixel type flags


/**
 * For botton management using the DebounceEvent library
 * https://github.com/xoseperez/debounceevent
 */
#define MODE_BUTTON_PIN 14      // D14 (D == digital port) -- sets display mode
#define INC_BUTTON_PIN  15      // D15 -- increments value
#define DEC_BUTTON_PIN  16      // D16 -- decrements value

#define CUSTOM_DEBOUNCE_DELAY   50
#define CUSTOM_REPEAT_DELAY     0       // disabled double clicks

DebounceEvent *mode_button;
DebounceEvent *inc_button;
DebounceEvent *dec_button;

void event_callback(uint8_t pin, uint8_t event, uint8_t count, uint16_t length) {
    if (event != EVENT_RELEASED)
    {
        return;
    }

    Serial.println("pressed");

    switch (pin)
    {
        case MODE_BUTTON_PIN:
            Serial.println("mode");
            cycleMode();
            break;
        case INC_BUTTON_PIN:
            Serial.println("inc");
            break;
        case DEC_BUTTON_PIN:
            Serial.println("dec");
            break;
        default:
            return;
    }
}

/**
 * setup the display, matrix, and button callbacks
 **/
void setup() {  
    // Setup the debug serial output
    Serial.begin(115200);

    // setup matrix display
    matrix.begin();
    matrix.setTextWrap(false);
    matrix.setBrightness(100);
    matrix.setTextColor(_colors[COLOR_RED]);

    // setup the button event loops
    mode_button = new DebounceEvent(MODE_BUTTON_PIN, event_callback,
            BUTTON_PUSHBUTTON | BUTTON_DEFAULT_HIGH | BUTTON_SET_PULLUP);
    inc_button = new DebounceEvent(INC_BUTTON_PIN, event_callback,
            BUTTON_PUSHBUTTON | BUTTON_DEFAULT_HIGH | BUTTON_SET_PULLUP);
    dec_button = new DebounceEvent(DEC_BUTTON_PIN, event_callback,
            BUTTON_PUSHBUTTON | BUTTON_DEFAULT_HIGH | BUTTON_SET_PULLUP);
}

int currentYear = 0;
int currentMonth = 0;
int currentDay = 0;
int currentHour = 0;
int currentMin = 0;
int currentSec = 0;

void loop()
{
    // listen for button callbacks
    mode_button->loop();
    inc_button->loop();
    dec_button->loop();

// BAD. JUST CHANGE THE TIME ONCE YOU IMPLEMENT THAT
    time_t tmp = RTC.get() + 3600;
    tm* time = localtime(&tmp);

    currentYear = time->tm_year + 1900;
    currentMonth = time->tm_mon + 1;
    currentDay = time->tm_mday;
    currentHour = time->tm_hour;
    currentMin = time->tm_min;
    currentSec = time->tm_sec;

    matrix.fillScreen(0);
    if (currentMode == MESSAGE) displayMessage();
    if (currentMode == TIME) displayTime();
    if (currentMode == DATE) displayDate();
    matrix.show();
    delay(cycleSpeed); // Controls scrolling speed

    timeTillModeChange -= cycleSpeed;
    if (timeTillModeChange < 0) cycleMode();
}

void cycleMode() {
    if (currentMode == MESSAGE) {
        timeTillModeChange = 6000;
        currentMode = TIME;
    } else if (currentMode == TIME) {
        timeTillModeChange = 6000;
        currentMode = DATE;
    } else if (currentMode == DATE) {
        message = (message + 1) % messageCount;
        timeTillModeChange = ((strlen(messages[message]) * 6) + MATRIX_WIDTH) * cycleSpeed;
        currentMode = MESSAGE;
        messageX = MATRIX_WIDTH;
    }
}

void displayMessage() {
    messageX--;
    Serial.println(messageX);
    matrix.setCursor(messageX, 0);
    matrix.setTextColor(timeColor);
    matrix.print(messages[message]);
}

void displayDate() {
    matrix.setCursor(7, 0);
    matrix.setTextColor(timeColor);
    matrix.print(months[currentMonth]);

    char buffer[50];
    sprintf(buffer, "%02d", currentDay);
    matrix.setCursor(27, 0);
    matrix.setTextColor(colonColor);
    matrix.print(buffer);
}

void displayTime() {
    char buffer[50];
    
    sprintf(buffer, "%02d", currentHour % 12);
    matrix.setCursor(4, 0);
    matrix.setTextColor(timeColor);
    matrix.print(buffer);

    matrix.setCursor(14, 0);
    matrix.setTextColor(colonColor);
    matrix.print(":");

    sprintf(buffer, "%02d", currentMin);
    matrix.setCursor(17, 0);
    matrix.setTextColor(timeColor);
    matrix.print(buffer);

    matrix.setCursor(27, 0);
    matrix.setTextColor(colonColor);
    matrix.print(":");

    sprintf(buffer, "%02d", currentSec);
    matrix.setCursor(30, 0);
    matrix.setTextColor(timeColor);
    matrix.print(buffer);
}

//void shuffleMessages() {
//  std::shuffle(std::begin(_messages), std::end(_messages),
//               std::mt19937{std::random_device{}()});
//}
