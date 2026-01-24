# Spartronics countdown clock

Counts down the time until events such as kickoff, first competition, etc.
Scrolling message is supported which is set via the mode button

## Parts
- Teensy LC
- Adafruit NeoMatrix
- DS3231 RTC
- AT24C32 for storage -- currently not used

## Libraries
- [IntervalTimer library](https://www.pjrc.com/teensy/td_timing_IntervalTimer.html)
- [DebounceEvent button manager](https://github.com/xoseperez/debounceevent)
- Adafruit NeoPixel and NeoMatrix library
- [DS1307RTC library (compatible w/ DS3231)](https://www.pjrc.com/teensy/td_libs_DS1307RTC.html)

## Usage
- 3 buttons control the behavior of the display
	- Mode - Change between countdown and messages
	- Inc - Change between countdown and date/time
	- Dec - Display the name of the next event

- To set the clock (ie, adjusting for daylight savings)
	- Connect a computer to the USB port of the clock module
	- Load the Arduino IDE, and select the port where the Teensy is connected (**Tools -> Port**)
	- Start the Arduino serial monitor (**Tools -> Serial Monitor**), and set data rate to **115200bps**
	- On clock module, change mode to time display by pressing **Inc** twice (this may take multiple tries, its a little finicky)
	- While the time is displayed, click **Mode** twice (quickly)
	- The words **SET TIME** should be displayed in red on the clock, and a prompt message should be displayed o the serial terminal
	- Using the serial terminal, follow the prompts and enter the time in the format **YYYY:MM:DD:hh:mm:ss**
		- The date can be copied to clipboard on Mac with the command: **date +%Y:%m:%d:%H:%M:%S | pbcopy**
	- If successful, the new time should be displayed. Otherwise, you must re-enter time setting mode and try again

## Building and Loading Code
Follow these instructions to install Teensy https://www.pjrc.com/teensy/td_download.html
Install the various libraries that are needed.

## Future Enhancements?
- Use the EEPROM on the RTC board to store message strings that can be updated via the serial terminal
- Use the EEPROM on the RTC board to store upcoming countdown dates that can be updated via the serial termanal
