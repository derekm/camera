# derekm's Camera

An edge AI camera for experimentation in the capture and analysis of real-time imagery.

### Dependencies
* Sony Spresense Arduino board
  * using SDHCI, Camera, DNNRT & GNSS libraries
* [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX)
* [BmpImage_ArduinoLib](https://github.com/YoshinoTaro/BmpImage_ArduinoLib)
* [2.2 inch 4-Wire SPI TFT LCD Display Module 240x320 Chip ILI9341](http://hiletgo.com/ProductDetail/2157050.html)
* circuit with button to D07 & LED to D06
  * debounced as in [Tutorial 02 for Arduino: Buttons, PWM, and Functions](https://youtu.be/_LCCGFSMOr4), but with PULLUP inputs, as in [Arduino Basics - INPUT and INPUT_PULLUP Using a Button](https://youtu.be/JRt3oGPeOWA)
