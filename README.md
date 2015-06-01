[Mikrotik RouterOS](http://www.mikrotik.com/) devices are capable of sending SMS messages via a GSM modem connected via USB:

http://wiki.mikrotik.com/wiki/Manual:Tools/Sms

A sample use of the RouterOS command is:

```
/tool sms send usb1 "123" message="Hello, world!"
```

I saw this feature not as a way to send SMS messages, but rather as a mechanism to make it possible for Mikrotik devices to send control messages to external embedded devices via USB.

What I've done is use a $1 PIC16F1454 USB microcontroller to implement just enough of the 3GPP specification to make RouterOS believe that it is communicating with a GSM modem.

This provides the foundation for implementing Mikrotik scripted control of color LEDs, relays, etc.

As an example, I've also written the code to control WS281x devices, a commonly-available family of Chinese-market color LEDs.  The code interprets the message as a set of commands to control multiple color LEDs.

Each command is a single character:

'a'-'z' represent rainbow colors from violet to blue to green to yellow to orange to red

'0'-'9' represent grayscale values from 0% to 100% intensity

'>'     instructs the device to shift all LED values over by one

' '     instructs the device to not update the LED value at the current position

'X'     instructs the device to turn all LEDs off

So, as an example: the message "abc" would set LED1 to color 'a', LED2 to color 'b', and LED3 to color 'c'.  A subsequent message ">z" would shift LED3's value to LED4, LED2's value to LED3, LED1's value to LED2, and set LED1 to color 'z'.

