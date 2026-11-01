Here is the exact pin-to-pin wiring map matching your ribbon cable and STEMMA QT leads to the Seeed Studio XIAO ESP32-S3 Sense headers.

**1. I2C Bus Daisy Chain (OLED + Si7021 + LIS3MDL)**

The I2C lines share a single bus. Because your STEMMA QT cables chain the boards together, only one set of 4 wires needs to connect to the XIAO headers:

|**STEMMA QT Wire**|**Signal**|**Target Pin on XIAO ESP32-S3**|**Notes**|
|---|---|---|---|
|**Black**|**GND**|**GND**|Shared common ground|
|**Red**|**3V3**|**3V3**|3.3V power rail|
|**Blue**|**SDA**|**Pin D4** (GPIO 5)|I2C Serial Data|
|**Yellow**|**SCL**|**Pin D5** (GPIO 6)|I2C Serial Clock|

_All 3 devices (LIS3MDL at 0x1C, SH1107 OLED at 0x3C, and Si7021 at 0x40) run concurrently across these two pins._

**2. MAX98357A I2S Audio Amplifier (Your 7-Wire Ribbon)**

|**Ribbon Wire**|**MAX98357A Pin**|**Target Pin / Connection**|**Notes & Configuration**|
|---|---|---|---|
|**Red**|**VIN**|**5V (VBUS)**|Connect to XIAO **5V** pad/pin. _Powering the amp from 5V provides maximum speaker headroom and prevents brownouts._|
|**Orange**|**GND**|**GND**|System common ground.|
|**Yellow**|**SD (Shutdown)**|_Leave Floating / NC_ (or tie to 3V3)|The board has an internal pull-up (~100kΩ) that defaults to ![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADkAAAASCAMAAADWrYZCAAAAAXNSR0IArs4c6QAAAGxQTFRFAAAAAAAAAAA6AABmADpmADqQAGa2OgAAOgA6OgBmOjqQOma2OpDbZgAAZgA6ZgBmZjoAZjpmZmaQZma2Zrb/kDoAkDo6kNv/tmYAttv/tv//25A625Bm27aQ2////7Zm/9uQ/9u2//+2///bw/N0gwAAAAF0Uk5TAEDm2GYAAAAJcEhZcwAADsQAAA7EAZUrDhsAAAAZdEVYdFNvZnR3YXJlAE1pY3Jvc29mdCBPZmZpY2V/7TVxAAAA+UlEQVQ4T8VSaw+CMBDbUPH9fk2Hgvj//6O99ZARSaafvISRlPbaO2bMf+q2uySMi/29j+EX6cDVrEcawHppe/XArR1ejWnaC/uxsdkBbsT8KGrqBGf53DyPYFRTyFH4VK/Oxg+uwAOtEzlSOqjkeZ7OQqvXbCCNqFS9GrVKEdwmECmhzEkpEZFQ04t4q6zGGFM2rwZ0NvUW1oSkByvsBEUAOG2YVles0cOEchTvvbSeGJG74cltqJAYuA8dPk4rgWSVqmm8YFEEcG5kHM3XmdNZbN+NLhqTwQM5CwN/3L7or3B4vX0fuO4m+er+gCQ9InxzwX/p9zv3BSe3E5mt3NmJAAAAAElFTkSuQmCC)mixed mono when left disconnected.|
|**Green**|**GAIN**|**GND**|Connect directly to **GND** to set standard **+9 dB** gain (ideal balance for small cavity speakers without distortion).|
|**Blue**|**DIN**|**Pin D9** (GPIO 8)|I2S Serial Data Out|
|**Purple**|**BCLK**|**Pin D7** (GPIO 44)|I2S Bit Clock|
|**White**|**LRC**|**Pin D8** (GPIO 7)|I2S Word Select / Left-Right Clock|

- **Speaker Terminals (+ and -):** Solder the red and black leads of your cavity speaker directly to the screw terminal or bare through-holes on the edge of the MAX98357A board.

**3. Capacitive Touch Sensor (Skull Interaction)**

|**Signal**|**Source**|**Target Pin on XIAO ESP32-S3**|**Notes**|
|---|---|---|---|
|**Touch**|Copper Foil Tape inside KIRA skull|**Pin D0** (GPIO 1)|Native ESP32-S3 capacitive touch channel (Touch 1) for wake/interrupt|

**4. XIAO ESP32-S3 Header Mapping Summary**

To keep assembly straightforward when soldering or inserting into a breadboard/perfboard, here is how the XIAO's two physical rows line up:

```
                  ┌───────────────────────┐  
                  │     USB-C / ANTENNA   │  
                  └───────────────────────┘

                        XIAO ESP32-S3

                  ┌─────┐           ┌─────┐  
     Touch (Pad) ─┤ D0  │           │ 5V  ├── Amp VIN (Red)  
          [Free] ─┤ D1  │           │ GND ├── Common GND (Amp Org, I2C Blk, Gain Grn)  
          [Free] ─┤ D2  │           │ 3V3 ├── I2C VCC (Red)  
          [Free] ─┤ D3  │           │ D10 ├── [Free]  
   I2C SDA (Blu) ─┤ D4  │           │ D9  ├── Amp DIN (Blue)  
   I2C SCL (Yel) ─┤ D5  │           │ D8  ├── Amp LRC / WS (White)  
          [Free] ─┤ D6  │           │ D7  ├── Amp BCLK (Purple)  
                  └─────┘           └─────┘
```

**Assembly Note:** The MAX98357A **Green (GAIN)** wire can simply be bridged or soldered directly to the adjacent **Orange (GND)** wire on the amplifier side to keep your wire count down to the XIAO headers.