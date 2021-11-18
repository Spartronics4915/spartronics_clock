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
 *  - IntervalTimer library for Teensy
 *  - DS1307RTC library (compatible w/ DS3231)
 *    https://www.pjrc.com/teensy/td_libs_DS1307RTC.html
 *
 * Usage:
 *  3 buttons control the behavior of the display
 *  - button 1: specifies mode for reprogramming the display:
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

// Spartronics colors! (From ATLaS, 2014)
#define SPARTRONICS_YELLOW Adafruit_NeoMatrix::Color(90, 90, 0)
#define SPARTRONICS_BLUE Adafruit_NeoMatrix::Color(0, 50, 170)

// Fun messages to print
static const char *_messages[] = {
    "Spartronics 4915 - Woot!",
    "Gracious Professionalism",
    "Everybody is in Marketing!",
    "Robots don't quit!",
    "Clio was here...",
    "Mentors Rock!",
    "Water game confirmed!",
    "Are we all having PHUN?",
    "Did you do your survey?",
    "ROCKIN' ROBOT!",
    "ENABLING!",
    "ATLaS",
    "GAEA",
    "ARES",
    "HELIOS",
    "THEMIS",
    "CHAOS",
    "ATHENA",
    "RAPID REACT!",
    "Sleep? Who has time for that?",
    "Safety Glasses!",
    "More than Robots",
    "How about a scissor lift?",
    "I build robots...",
    "Do. Or do not. There is no try.",
    "Camden is above average",
    "Mr. Chee is a great teacher!",
    "owo"
};

/**
 *  Structure to hold time values in their human-readable components
 */
typedef struct {
    unsigned year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned second;
    const char *description;
} CalendarTime_t;

/**
 *  List of events to count-down to. They must be sorted in ascending order.
 */
static const CalendarTime_t _important_times[] = {
    { 2022,  1,  8,  7,  0,  0, "Kickoff" },            // Kickoff
    { 2022,  2, 18, 12,  0,  0, "Sammamish" },          // Sammammish
    { 2022,  3, 25, 12,  0,  0, "Auburn" },             // Auburn
    { 2022,  4,  6,  9,  0,  0, "Districts" },          // District Champs
    { 2022,  4, 23, 12,  0,  0, "Worlds" },             // Worlds
};

static time_t _target_time;
static const char * _target_description;

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
    [COLOR_YELLOW] = SPARTRONICS_YELLOW,
    [COLOR_GREEN] = Adafruit_NeoMatrix::Color(0, 128, 0),
    [COLOR_BLUE] = SPARTRONICS_BLUE,
    [COLOR_PURPLE] = Adafruit_NeoMatrix::Color(0, 128, 128),
};

/**
 * Setup the display state
 *      - countdown: displays the countdown clock -- updated by timer_ticks
 *      - days/hrs/mins/secs/: changes the value using inc/dec button presses
 *      - message: prints text messages
 */
typedef enum   // at each button mode press, display mode changes accordingly
{
    STATE_INIT = 0,
    STATE_MESSAGE,
    STATE_MESSAGE_ONCE,
    STATE_MESSAGE_REPEAT,
    STATE_COUNTDOWN,
    STATE_DATE,
    STATE_TIME,
    STATE_SET_TIME,
    // Add new states above this line
    STATE_MAX
} State_t;

/**
 * Setup the events for triggering state changes
 * These are driven by the buttons
 */
typedef enum
{
    EVENT_NULL = 0,     // no event
    EVENT_MODE,
    EVENT_SET,
    EVENT_INCREMENT,
    EVENT_DECREMENT,
    EVENT_TIMER,
    EVENT_SCROLL,
    // Add new events above this line
    EVENT_MAX
} Event_t;

State_t _state;  // tracks the state of the display

/**
 *  Variable to track the time interval remaining until the event being tracked
 */
static uint32_t _countdown_time;

/**
 *  Structure to hold the time interval value to be displayed on the clock
 */
typedef struct              // parses the countdown_time to its components
{
    uint8_t  seconds;
    uint8_t  minutes;
    uint8_t  hours;
    uint16_t days;
} TimeInterval_t;


/**
 *  Setup interrupts for scroll and timer events, callbacks,
 *  and variables to update controls
 */
IntervalTimer scroll_interval;

bool update_timer_event = false;
bool update_scroll_event = false;;

void scroll_callback()
{
    update_scroll_event = true;
}

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
    Event_t state_event = EVENT_NULL;

    if (event != EVENT_RELEASED)
    {
        return;
    }

    switch (pin)
    {
        case MODE_BUTTON_PIN:
            if (count == 1)
            {
                state_event = EVENT_MODE;
            }
            else if (count == 2)
            {
                state_event = EVENT_SET;
            }
            break;
        case INC_BUTTON_PIN:
            if (count == 1)
            {
                state_event = EVENT_INCREMENT;
            }
            break;
        case DEC_BUTTON_PIN:
            if (count == 1)
            {
                state_event = EVENT_DECREMENT;
            }
            break;
        default:
            return;
    }

    if (state_event != EVENT_NULL)
    {
        // update the state machine
        _state = state_machine(_state, state_event);
    }
}

/**
 * setup the display, matrix, and button callbacks
 **/
void setup()
{
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

    // Set the system clock from the RTC
    get_time_from_rtc();

    // initialize state machine
    _state = STATE_INIT;

    // IntervalTimer takes a callback function, and a number of microseconds
    // scroll_interval is set to run the callback 10 times per second
    scroll_interval.begin(scroll_callback, 100 /* ms */ * 1000 /* us per ms */);
}

/**
 * continously loop through button callbacks to detect display state changes
 * if no button presses, update the display accordingly
 *      - if in countdown mode, wait for timer_ticks to adjust seconds, etc.
 *      - if in sayings mode, print the 'sayings' on the matrix
 *      - in any other mode, wait for inc/dec button presses
 */
void loop()
{
    Event_t event;
    static time_t last_time = 0;

    // listen for button callbacks and trigger state_machine change accordingly
    mode_button->loop();
    inc_button->loop();
    dec_button->loop();

    // TODO Fix this so it's the only timer source for seconds tick
    if (now() != last_time)
    {
        last_time = now();
        update_timer_event = true;
    }

    // listen for timer events & update state machine
    if (update_timer_event)
    {
        update_timer_event = false;
        event = EVENT_TIMER;
        _state = state_machine(_state, event);
    }
    else if (update_scroll_event)
    {
        update_scroll_event = false;
        event = EVENT_SCROLL;
        _state = state_machine(_state, event);
    }

    // Check if a new countdown time is needed?
    if (_countdown_time == 0)
    {
        // Go through list and find next time that is ahead of now
    }
}

/**
 *  given a date info, makes time -- returns time in seconds since epoch
 */
time_t convert_time(const CalendarTime_t &time)
{
    tmElements_t tmSet;

    tmSet.Year = time.year - 1970;
    tmSet.Month = time.month;
    tmSet.Day = time.day;
    tmSet.Hour = time.hour;
    tmSet.Minute = time.minute;
    tmSet.Second = time.second;

    return makeTime(tmSet); //convert to time_t
}

time_t find_next_target(time_t now)
{
    time_t target = 0;

    for (unsigned idx=0; idx<NUM_ELEMENTS(_important_times); idx++)
    {
        time_t temp;
        // Convert the time to time_t so we can compare to now
        temp = convert_time(_important_times[idx]);

        // Compare to 'now'. If temp is in the future, then it is our target
        if (temp > now)
        {
            target = temp;
            _target_time = target;
            _target_description = _important_times[idx].description;
            break;
        }
    }

    return target;
}

/**
 *  Break down the provided time interval (in seconds) into clock-style
 *  components of days, hours, minutes and seconds.
 *    see: https://pastebin.com/sfEjA94n
 */
void compute_elapsedTime(TimeInterval_t &time_interval, uint32_t countdown_time)
{
    time_interval.seconds = countdown_time % 60;
    countdown_time /= 60; // now it is minutes

    time_interval.minutes = countdown_time % 60;
    countdown_time /= 60; // now it is hours

    time_interval.hours = countdown_time % 24;
    countdown_time /= 24; // now it is days

    time_interval.days = countdown_time;
}

static State_t _handle_state_init(Event_t event, bool first_time)
{
    State_t next_state = STATE_COUNTDOWN;

    // We can do some initialization code here, if we need to
    matrix.clear();

    // Try to find a date to count down to in our list
    if (find_next_target(now()) == 0)
    {
        // No target time found!
        message_start("ERR: NO COUNTDOWN DATES!", COLOR_RED);
        next_state = STATE_MESSAGE_ONCE;
    }

    return next_state;
}

static State_t _handle_state_countdown(Event_t event, bool first_time)
{
    State_t next_state = _state;
    TimeInterval_t time_interval;
    time_t time_now = now();
    uint32_t countdown_time = 0;

    if (first_time)
    {
        // Refresh the software clock from the hardware
        get_time_from_rtc();
    }

    switch (event)
    {
        case EVENT_TIMER:
            if (_target_time >= time_now)
            {
                countdown_time = _target_time - time_now;
                // Print the remaining time interval on the display
                compute_elapsedTime(time_interval, countdown_time);

                // display the time value
                print_time_interval(time_interval);
#if DEBUG_ON
                Serial.print("---_countdown_time: ");
                Serial.println(countdown_time);
                Serial.print("---display time: ");
                Serial.print(time_interval.days);
                Serial.print(":");
                Serial.print(time_interval.hours);
                Serial.print(":");
                Serial.print(time_interval.minutes);
                Serial.print(":");
                Serial.println(time_interval.seconds);
#endif

                if (time_in_state() > (15 /* minutes */ * 60 /* seconds */))
                {
                    // Print a random message
                    message_start(_messages[random(NUM_ELEMENTS(_messages))],
                            (ColorName_t)random(NUM_ELEMENTS(_colors)));
                    next_state = STATE_MESSAGE_ONCE;
                }
            }
            else
            {
                // No target found, so go to date/time display
                next_state = STATE_DATE;
            }
            break;
        case EVENT_MODE:
            // Go to the message display
            message_start(_messages[random(NUM_ELEMENTS(_messages))],
                    (ColorName_t)random(NUM_ELEMENTS(_colors)));
            next_state = STATE_MESSAGE;
            break;
        case EVENT_INCREMENT:
            // Go to the date display
            next_state = STATE_DATE;
            break;
        case EVENT_DECREMENT:
            // Display the description of this event
            message_start(_target_description, COLOR_GREEN);
            next_state = STATE_MESSAGE_ONCE;
            break;
        default:
            // Ignore all other events
            break;
    }

    return next_state;
}

/**
 *  Print the current date on the display
 */
static State_t _handle_state_date(Event_t event, bool first_time)
{
    State_t next_state = _state;
    CalendarTime_t calendar_time;

    if (first_time)
    {
        tmElements_t tm;

        // Refresh the software clock from the hardware
        get_time_from_rtc();

        // Get the calendar time
        breakTime(now(), tm);
        calendar_time.year = tmYearToCalendar(tm.Year);
        calendar_time.month = tm.Month;
        calendar_time.day = tm.Day;
        calendar_time.hour = tm.Hour;
        calendar_time.minute = tm.Minute;
        calendar_time.second = tm.Second;

        // Print the date
        print_date(calendar_time);
    }

    switch (event)
    {
        case EVENT_TIMER:
            if (time_in_state() > 10 /* seconds */)
            {
                next_state = STATE_TIME;
            }
            break;
        case EVENT_MODE:
            // Go to the message display
            message_start(_messages[random(NUM_ELEMENTS(_messages))],
                    (ColorName_t)random(COLOR_MAX));
            next_state = STATE_MESSAGE;
            break;
        case EVENT_DECREMENT:
            // Go back to the countdown display
            next_state = STATE_COUNTDOWN;
            break;
        case EVENT_INCREMENT:
            // Go to the date display
            next_state = STATE_TIME;
            break;
        default:
            // Ignore all other events
            break;
    }

    return next_state;
}

/**
 *  Print the current time on the display
 */
static State_t _handle_state_time(Event_t event, bool first_time)
{
    State_t next_state = _state;
    CalendarTime_t calendar_time;
    tmElements_t tm;

    if (first_time)
    {
        // Refresh the software clock from the hardware
        get_time_from_rtc();

        // Get the calendar time
        breakTime(now(), tm);
        calendar_time.year = tmYearToCalendar(tm.Year);
        calendar_time.month = tm.Month;
        calendar_time.day = tm.Day;
        calendar_time.hour = tm.Hour;
        calendar_time.minute = tm.Minute;
        calendar_time.second = tm.Second;

        // Print the time
        print_time(calendar_time);
    }

    switch (event)
    {
        case EVENT_TIMER:
            // TODO: Update display each second
            breakTime(now(), tm);
            calendar_time.year = tmYearToCalendar(tm.Year);
            calendar_time.month = tm.Month;
            calendar_time.day = tm.Day;
            calendar_time.hour = tm.Hour;
            calendar_time.minute = tm.Minute;
            calendar_time.second = tm.Second;

            // Print the time
            print_time(calendar_time);

            if (time_in_state() > 10 /* seconds */)
            {
                next_state = STATE_DATE;
            }
            break;
        case EVENT_MODE:
            // Go to the message display
            message_start(_messages[random(NUM_ELEMENTS(_messages))],
                    (ColorName_t)random(COLOR_MAX));
            next_state = STATE_MESSAGE;
            break;
        case EVENT_DECREMENT:
            // Go back to the countdown display
            next_state = STATE_COUNTDOWN;
            break;
        case EVENT_INCREMENT:
            // Go to the date display
            next_state = STATE_DATE;
            break;
        case EVENT_SET:
            // Special event, only used in this state
            // Set the time (over the USB port)
            next_state = STATE_SET_TIME;
            break;
        default:
            // Ignore all other events
            break;
    }

    return next_state;
}

/**
 *  Set the system time
 */
static State_t _handle_state_set_time(Event_t event, bool first_time)
{
    State_t next_state = STATE_TIME;

    // Call the set_time() function to set the clock
    set_time();

    return next_state;
}

static State_t _handle_state_message(Event_t event, bool first_time)
{
    State_t next_state = _state;

    if (first_time)
    {
        // Reset message parameters
        message_start(NULL, COLOR_RED);
    }

    switch (event)
    {
        case EVENT_SCROLL:
            // Scroll a message on the display
            message_scroll();
            if (message_done())
            {
                switch (next_state)
                {
                    case STATE_MESSAGE:
                        message_start(
                                _messages[random(NUM_ELEMENTS(_messages))],
                                (ColorName_t)random(COLOR_MAX));
                        break;
                    case STATE_MESSAGE_REPEAT:
                        message_start(NULL, COLOR_RED);
                        break;
                    case STATE_MESSAGE_ONCE:
                    default:
                        next_state = STATE_COUNTDOWN;
                        break;
                }
            }
            break;
        case EVENT_MODE:
        case EVENT_INCREMENT:
        case EVENT_DECREMENT:
            // Advance to the next state
            next_state = STATE_COUNTDOWN;
            break;
        default:
            // Ignore all other events
            break;
    }

    return next_state;
}

/**
 *  Allow a state handler to know how long the state has been running
 */
static time_t _state_entry_time = 0;
static uint32_t time_in_state(void)
{
    uint32_t seconds = 0;

    if (now() > _state_entry_time)
    {
        seconds = now() - _state_entry_time;
    }

    return seconds;
}

/**
 * State machine to control the display
 * Note: events triggered by the buttons and timer_ticks
 */
State_t state_machine(State_t current_state, Event_t event)
{
    State_t next_state = current_state;
    bool first_time = false;
    static State_t last_state = STATE_INIT;

    // Set a flag if this is the first time in this new state
    if (current_state != last_state)
    {
        first_time = true;
        _state_entry_time = now();
    }

    switch (current_state)
    {
        case STATE_INIT:
            next_state = _handle_state_init(event, first_time);
            break;

        case STATE_COUNTDOWN:
            next_state = _handle_state_countdown(event, first_time);
            break;

        case STATE_DATE:
            next_state = _handle_state_date(event, first_time);
            break;

        case STATE_TIME:
            next_state = _handle_state_time(event, first_time);
            break;

        case STATE_SET_TIME:
            next_state = _handle_state_set_time(event, first_time);
            break;

        case STATE_MESSAGE:
        case STATE_MESSAGE_ONCE:
        case STATE_MESSAGE_REPEAT:
            next_state = _handle_state_message(event, first_time);
            break;

        // Unhandled case. Don't know how we got here, just set the default
        default:
            next_state = STATE_INIT;
            break;
    }

    last_state = current_state;

    return next_state;
}
