## WARNING
I just decided to share my steps for completeness sake, not because i think this is a perfectly polished fool-proof design

## Circuit board
### Parts
* RP2040-zero with headers
* TXS0108E module with headers
* Right angle IDC-10 male connector
* 10-50k resistor (level-shifter OE pull-down)
* Single/double-sided perfboard
* Something to cut the perfboard with: i just score it with a knife on both sides, break and polish
* Thin wires

### Assembly
* **IMPORTANT!!!** I inverted the angle of the IDC-10 connector by pulling all pins, rotating the plastic housing and reinserting the pins back.
  So orientation cutoff on my rotated connector faces towards the perfboard.
  This allows using a normal straight-crimped ribbon cable and (maybe) simplifies the routing a bit, so you should either do the same or reroute the connector signals yourself.
* Cut a 11 holes * 20 holes piece of a perfboard with a bit extra on short side for connector support:
![plot](/doc/connector.jpg)
* Solder connector and headers to the top side. If your RP2040/TXS0108E has headers presoldered - solder the entire module, just watch for the orientation:
![plot](/doc/perfboard1.jpg)
* Solder GND, VCCs, OE and OE pulldown resistor:
![plot](/doc/perfboard2.jpg)
* Now a bit tricky part: all level-shifter's channels are equal and bidirectional,
  so which signal goes where does not matter, you just have to route them to correct RP2040 pins.
  Also there are 8 level-shifter channels, but only 7 data signals, so i used the remaining 1 to aid VCCB routing.
  My routing is below:
![plot](/doc/perfboard3.jpg)

### Firmware
There is no prebuilt firmware for this setup, but it is pretty easy to build from sources.

The changes required in blaster.c:
* define TCK_DCLK_PIN 8
* define LEVEL_SHIFTER_OE_PIN 15
* undefine ACTIVE_LED_PIN
* define ACTIVE_LED_WS2812_PIN 16
* choose your standby and active LED colors
  
## Enclosure
### Parts
* Assembled circuit board
* Glue
* Printed 'cap' part
* Printed 'body' part
* A piece of something as an LED light guide: i used a bit of translucent PETG filament

### Print settings
* No supports
* Layer height 0.2mm is fine
* Print 'cap' part flat side down

### Assembly
* Put the board into the 'body' part. It should sit tight on both length and width, so you may have to add spacers / adjust dimensions using F3D source
* Check that the 'cap' part fits, the buttons are working etc.
* Glue your light guide thing to the LED - hot glue gun is ideal for this
* Pass the light guide through the 'cap' and glue the 'cap' to the 'body' part
* Trim excess light guide
* (optional) Add a tiny bit of glue to secure the lightguide to the cap

The result should look like this:
![plot](/doc/result.jpg)
