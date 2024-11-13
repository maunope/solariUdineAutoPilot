**DISCLAIMER**
This is still a prototype, still to be tested on a real clock, use at your risk and plz don't come whining if you damage your flip clock or set your house on fire. 


# Solari Udine Auto Pilot

Arduino based Solari Udine Clock controller, suitable for alternating pulse Solari Udine Clock motors (i.e. Cifra 12). Controls hours and minutes, not calendar functions are provided.

**Main features:**
- Self adjusts after a power loss
- Handles DST (Central Europe rules)
- Allows manual adjustment

A led provides visual feedback, an optional LCD1602 cab be plugged in to display status. 

The software tries to mitigate strain on the flip clock limiting the number and frequency of rollers turns:
- Turns are limited to 1 every 3 seconds (manual step adjustment bypasses this)
- If more than 120 roller flips would be require to catch up, movement is paused till the next day (a halted clock displays the right time once a day after all **:-)**)
- If the eeprom or rtc module fail, no pulses are sent to the motor

The following features improve durability and operation:
- The system self restart once per week
- EEProm writes are spread over a 256kbit eeprom, this should guarantee at least 20 years of operation before writes start to fail, more likley 50-70
- If eeprom stored time is unavailable, the clock halts until it is adjusted
- If RTC module is unavailable, the clock blinks for 30 seconds and then resets



## Dependencies

- [LiquidCrustal I2C]([URL](https://github.com/johnrickman/LiquidCrystal_I2C)
- [RTClib](https://github.com/adafruit/RTClib)
- [SparkFun External EEPROM Arduino Library](https://github.com/sparkfun/SparkFun_External_EEPROM_Arduino_Library)

## Circuit Board
**to be updated*
![Solari Udine Autopilot Circuit Board]([path/to/your/image.jpg](https://github.com/maunope/solariUdineAutoPilot/blob/main/Controller%20Solari.png?raw=true))


## Todo

Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.
