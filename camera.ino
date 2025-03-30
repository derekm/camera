/*
 *  camera.ino - Simple camera example sketch
 *  Copyright 2018, 2022 Sony Semiconductor Solutions Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  This is a test app for the camera library.
 *  This library can only be used on the Spresense with the FCBGA chip package.
 */

#include <SDHCI.h>
#include <stdio.h>  /* for sprintf */
#include <iostream>
#include <iomanip>
#include <sstream>

#include <Camera.h>

#include <Arduino_GFX_Library.h>

#include <DNNRT.h>
#include <GNSS.h>

#include "BmpImage.h"

#define BAUDRATE                (115200)
#define TOTAL_PICTURE_COUNT     (10)

int switchPin = PIN_D07;
int ledPin = PIN_D06;
int takePicturePin = PIN_D04;
boolean takePicture = false;
boolean lastButton = HIGH; // Spresense uses pull-up registers
boolean currentButton = HIGH;
boolean ledActive = false;

SDClass  theSD;
boolean takingPicture = false;
boolean captureFrames = false;
int take_picture_count = 0;
int start_seconds;
int seconds_now;
int frames = 0;
int fps = 0;
// char filename[16] = {0};
std::string filename;

DNNRT dnnrt;
DNNVariable input(3*32*32);

const int target_w = CAM_IMGSIZE_QVGA_H;
const int target_h = CAM_IMGSIZE_QVGA_V;
const int pixfmt   = CAM_IMAGE_PIX_FMT_RGB565;
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
Arduino_ILI9341 gfx = Arduino_ILI9341(bus, TFT_RST, 1 /* rotation */, false /* IPS */);
Arduino_Canvas canvas = Arduino_Canvas(target_w, target_h, &gfx, 0, 0, 0);
Arduino_Canvas scaledFrame = Arduino_Canvas(32, 32, &canvas, target_w-32, 32, 0);
BmpImage bmp320x240;
BmpImage bmp32x32crop;
BmpImage bmp32x32frame;
// BmpImage bmp40x30;

#define SCALE_FACTOR 1
#define RAW_WIDTH    CAM_IMGSIZE_QVGA_H    // 320 <-- Classifies ok.
#define RAW_HEIGHT   CAM_IMGSIZE_QVGA_V    // 240: 320*240=76800
#define CLIP_WIDTH  (128 * SCALE_FACTOR) // EI_CLASSIFIER_INPUT_WIDTH  is defined by the EI Model in "model_metadata.h"
#define CLIP_HEIGHT (128 * SCALE_FACTOR) // EI_CLASSIFIER_INPUT_HEIGHT is defined by the EI Model in "model_metadata.h"
#define OFFSET_X   ((RAW_WIDTH  - CLIP_WIDTH)  / 2)             // (320-96) / 2 = 112
#define OFFSET_Y   ((RAW_HEIGHT - CLIP_HEIGHT) / 2)             // (240-96) / 2 =  72

#define STRING_BUFFER_SIZE  128       /**< %Buffer size */
#define RESTART_CYCLE       (60 * 5)  /**< positioning test term */
static SpGnss Gnss;                   /**< SpGnss object */

void setup_display() {
  Serial.println("Init Display");
  // Init Display
  if (!canvas.begin() || !scaledFrame.begin(GFX_SKIP_OUTPUT_BEGIN))
  // if (!gfx->begin(80000000)) /* specify data bus speed */
  {
    Serial.println("Arduino_GFX::begin() failed!");
  }
  memset(scaledFrame.getFramebuffer(), 0, 2*scaledFrame.height()*scaledFrame.width());
  canvas.setTextSize(2);
  canvas.setTextColor(RGB565_RED);
  canvas.fillScreen(RGB565_WHITE);
}

void setup_sdcard() {
  // sprintf(filename, "PICT%03d", take_picture_count);
  std::stringstream filenameStr;
  filenameStr << "PICT" << std::setw(3) << std::setfill('0') << take_picture_count;
  filename = filenameStr.str();

  /* Initialize SD */
  int i = 0;
  while (!theSD.begin()) {
    /* wait until SD card is mounted. */
    Serial.println("Insert SD card.");
    if (++i == 16) {
      canvas.setCursor(0, 0);
      canvas.fillScreen(RGB565_WHITE);
      i = 1;
    }
    canvas.println("Insert SD card.");
    canvas.flush();
  }
  /* Find the first filename that doen't exist */
  while(theSD.exists((filename + ".JPG").c_str())) {
    filenameStr.str(std::string());
    filenameStr.clear();
    filenameStr << "PICT" << std::setw(3) << std::setfill('0') << ++take_picture_count;
    filename = filenameStr.str();
    Serial.println(filename.c_str());
  }
  Serial.println(filename.c_str());
}

void setup_model() {
  File nnbfile = theSD.open("model.nnb");
  if (!nnbfile) {
    Serial.println("nnb not found");
    return;
  }
  int ret = dnnrt.begin(nnbfile);
  if (ret < 0) {
    Serial.println("Runtime initialization failure.");
    if (ret == -16) {
      Serial.print("Please install bootloader!");
      Serial.println(" or consider memory configuration!");
    } else {
      Serial.println(ret);
    }
    return;
  }
}

/**
 * Print error message
 */

void printError(enum CamErr err)
{
  Serial.print("Error: ");
  switch (err)
    {
      case CAM_ERR_NO_DEVICE:
        Serial.println("No Device");
        break;
      case CAM_ERR_ILLEGAL_DEVERR:
        Serial.println("Illegal device error");
        break;
      case CAM_ERR_ALREADY_INITIALIZED:
        Serial.println("Already initialized");
        break;
      case CAM_ERR_NOT_INITIALIZED:
        Serial.println("Not initialized");
        break;
      case CAM_ERR_NOT_STILL_INITIALIZED:
        Serial.println("Still picture not initialized");
        break;
      case CAM_ERR_CANT_CREATE_THREAD:
        Serial.println("Failed to create thread");
        break;
      case CAM_ERR_INVALID_PARAM:
        Serial.println("Invalid parameter");
        break;
      case CAM_ERR_NO_MEMORY:
        Serial.println("No memory");
        break;
      case CAM_ERR_USR_INUSED:
        Serial.println("Buffer already in use");
        break;
      case CAM_ERR_NOT_PERMITTED:
        Serial.println("Operation not permitted");
        break;
      default:
        break;
    }
}

/**
 * Callback from Camera library when video frame is captured.
 */
void CamCB(CamImage img)
{
  if (captureFrames) {
    canvas.printf("Writing %s ...", filename.c_str());
    canvas.flush();
    Serial.println(("Writing " + filename + " BMPs").c_str());
    File bmpFile = theSD.open((filename + "-320x240.BMP").c_str(), FILE_WRITE);
    bmpFile.write(bmp320x240.getBmpBuff(), bmp320x240.getBmpSize());
    bmpFile.close();
    bmpFile = theSD.open((filename + "-32x32crop.BMP").c_str(), FILE_WRITE);
    bmpFile.write(bmp32x32crop.getBmpBuff(), bmp32x32crop.getBmpSize());
    bmpFile.close();
    bmpFile = theSD.open((filename + "-32x32frame.BMP").c_str(), FILE_WRITE);
    bmpFile.write(bmp32x32frame.getBmpBuff(), bmp32x32frame.getBmpSize());
    bmpFile.close();
    // bmpFile = theSD.open((filename + "-40x30.bmp").c_str(), FILE_WRITE);
    // bmpFile.write(bmp40x30.getBmpBuff(), bmp40x30.getBmpSize());
    // bmpFile.close();
    captureFrames = false;
  }

  /* Check the img instance is available or not. */

  if (img.isAvailable())
    {
      CamImage resizeImg;
      CamErr resizeErr = img.clipAndResizeImageByHW(resizeImg,                   // CamImage &img,
                                   OFFSET_X,                    // int 	     lefttop_x                    = 112,
                                   OFFSET_Y,                    // int 	     lefttop_y                    =  72,
                                   OFFSET_X + CLIP_WIDTH - 1,   // int 	     rightbottom_x = 112 + 96 - 1 = 207,
                                   OFFSET_Y + CLIP_HEIGHT - 1,  // int 	     rightbottom_y = 112 + 96 - 1 = 207,
                                   32,   // int       width = 96.  Must be the same as the Impulse Input Block.
                                   32); // int       height = 96. Must be the same as the Impulse Input Block.
      if (resizeErr != CAM_ERR_SUCCESS) {
        printError(resizeErr);
      }
      CamImage resizeImg2;
      resizeErr = img.resizeImageByHW(resizeImg2, target_w / 8, target_h / 8);
      if (resizeErr != CAM_ERR_SUCCESS) {
        printError(resizeErr);
      }

      img.convertPixFormat(pixfmt);
      canvas.draw16bitRGBBitmap(0, 0, (uint16_t *)img.getImgBuff(), img.getWidth(), img.getHeight());
      bmp320x240.end();
      bmp320x240.begin(BmpImage::BMP_IMAGE_RGB565, img.getWidth(), img.getHeight(), img.getImgBuff());

      resizeImg.convertPixFormat(pixfmt);
      canvas.draw16bitRGBBitmap(target_w-32, 0, (uint16_t *)resizeImg.getImgBuff(), resizeImg.getWidth(), resizeImg.getHeight());
      bmp32x32crop.end();
      bmp32x32crop.begin(BmpImage::BMP_IMAGE_RGB565, resizeImg.getWidth(), resizeImg.getHeight(), resizeImg.getImgBuff());
      bmp32x32crop.alignImageLine(false);

      CamImage resizeImg2b;
      resizeErr = resizeImg2.clipAndResizeImageByHW(resizeImg2b, 4, 0, resizeImg2.getWidth()-4-1, 30-1, 32, 30);
      if (resizeErr != CAM_ERR_SUCCESS) {
        printError(resizeErr);
      }
      resizeImg2.convertPixFormat(pixfmt);
      canvas.draw16bitRGBBitmap(target_w-(target_w / 8), 64, (uint16_t *)resizeImg2.getImgBuff(), resizeImg2.getWidth(), resizeImg2.getHeight());
      // bmp40x30.end();
      // bmp40x30.begin(BmpImage::BMP_IMAGE_RGB565, resizeImg2.getWidth(), resizeImg2.getHeight(), resizeImg2.getImgBuff());

      resizeImg2b.convertPixFormat(pixfmt);
      scaledFrame.draw16bitRGBBitmap(0, 1, (uint16_t *)resizeImg2b.getImgBuff(), resizeImg2b.getWidth(), resizeImg2b.getHeight());
      scaledFrame.flush();
      bmp32x32frame.end();
      bmp32x32frame.begin(BmpImage::BMP_IMAGE_RGB565, scaledFrame.width(), scaledFrame.height(), (uint8_t *)scaledFrame.getFramebuffer());
      bmp32x32frame.alignImageLine(false);

      canvas.setCursor(0, 0);
      canvas.printf("%d FPS\n", fps);
      if (takingPicture) {
        canvas.printf("Writing %s ...", filename.c_str());
        // canvas.draw16bitRGBBitmap(target_w-resizedPictImg.getWidth(), target_h-resizedPictImg.getHeight(), (uint16_t *)resizedPictImg.getImgBuff(), resizedPictImg.getWidth(), resizedPictImg.getWidth());
      }
      canvas.drawRect(OFFSET_X, OFFSET_Y, 128, 128, RGB565_WHITE);
      canvas.flush();

      uint16_t *frameData = scaledFrame.getFramebuffer();
      float *inputData = input.data();
      for (int i = 0; i < 32*32; i = ++i) {
        uint16_t pixel = frameData[i];
        uint8_t r, g, b;
        r = ((pixel & 0xF800) >> 11) << 3;
        g = ((pixel & 0x7E0) >> 5) << 2;
        b = (pixel & 0x1F) << 3;
        inputData[i] = float(r);
        inputData[i+32*32] = float(g);
        inputData[i+32*32*2] = float(b);
      }
      // dnnrt.inputVariable(input, 0);
      // dnnrt.forward();
      // DNNVariable output = dnnrt.outputVariable(0);
      // Serial.printf("Winning label is %d\n", output.maxIndex());

      ++frames;
      seconds_now = millis();
      if (seconds_now - start_seconds >= 1000) {
        start_seconds = seconds_now;
        fps = frames;
        // Serial.print(frames);
        // Serial.println(" fps");
        frames = 0;
      }

      /* If you want RGB565 data, convert image data format to RGB565 */

      // img.convertPixFormat(CAM_IMAGE_PIX_FMT_RGB565);

      /* You can use image data directly by using getImgSize() and getImgBuff().
       * for displaying image to a display, etc. */

      // Serial.print("Image data size = ");
      // Serial.print(img.getImgSize(), DEC);
      // Serial.print(" , ");

      // Serial.print("buff addr = ");
      // Serial.print((unsigned long)img.getImgBuff(), HEX);
      // Serial.println("");
    }
  else
    {
      Serial.println("Failed to get video stream image");
    }
}

/**
 * @brief Initialize camera
 */
void setup()
{
  setup_button();
  CamErr err;

  /* Open serial communications and wait for port to open */

  Serial.begin(BAUDRATE);
  while (!Serial)
    {
      ; /* wait for serial port to connect. Needed for native USB port only */
    }
#ifdef CONFIG_ARCH_CHIP_CXD56XX
      Serial.println("This is a Spresense device!");
#endif
#ifndef CONFIG_ARCH_CHIP_CXD56XX
      Serial.println("This is NOT a Spresense device!");
#endif
  setup_display();
  setup_sdcard();

  /* begin() without parameters means that
   * number of buffers = 1, 30FPS, QVGA, YUV 4:2:2 format */

  Serial.println("Prepare camera");
  // err = theCamera.begin();
  // err = theCamera.begin(1, CAM_VIDEO_FPS_30, CAM_IMGSIZE_VGA_V, CAM_IMGSIZE_QVGA_H, CAM_IMAGE_PIX_FMT_RGB565, 7);
  err = theCamera.begin(1, CAM_VIDEO_FPS_30, CAM_IMGSIZE_QVGA_H, CAM_IMGSIZE_QVGA_V, CAM_IMAGE_PIX_FMT_YUV422, 7);
  if (err != CAM_ERR_SUCCESS)
    {
      printError(err);
    }

  /* Start video stream.
   * If received video stream data from camera device,
   *  camera library call CamCB.
   */

  Serial.println("Start streaming");
  err = theCamera.startStreaming(true, CamCB);
  if (err != CAM_ERR_SUCCESS)
    {
      printError(err);
    }

  /* Auto white balance configuration */

  Serial.println("Set Auto white balance parameter");
  err = theCamera.setAutoWhiteBalanceMode(CAM_WHITE_BALANCE_AUTO);
  if (err != CAM_ERR_SUCCESS)
    {
      printError(err);
    }
 
  /* Set parameters about still picture.
   * In the following case, QUADVGA and JPEG.
   */

  Serial.println("Set still picture format");
  theCamera.setJPEGQuality(90);
  err = theCamera.setStillPictureImageFormat(
    //  CAM_IMGSIZE_5M_H,
     CAM_IMGSIZE_3M_H,
    //  CAM_IMGSIZE_FULLHD_H,
    //  CAM_IMGSIZE_5M_V,
     CAM_IMGSIZE_3M_V,
    //  CAM_IMGSIZE_FULLHD_V,
     CAM_IMAGE_PIX_FMT_JPG, 9);
  if (err != CAM_ERR_SUCCESS) {
    printError(err);
  }
  // setup_model();
}

void setup_button() {
  pinMode(switchPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(takePicturePin, INPUT);
  digitalWrite(ledPin, LOW);
}

boolean debounce(boolean last) {
  boolean current = digitalRead(switchPin);
  // if (current == LOW) {
  //   Serial.println("Button pressed...");
  // }
  if (last != current) {
    delay(5);
    current = digitalRead(switchPin);
  }
  return current;
}

void loop() {
  currentButton = debounce(lastButton);
  if (lastButton == HIGH && currentButton == LOW) {
    ledActive = true;
    digitalWrite(ledPin, ledActive);
  }
  lastButton = currentButton;
  if (digitalRead(takePicturePin) == LOW && !takePicture) {
    Serial.printf("Take a picture!\n");
    ledActive = true;
    digitalWrite(ledPin, ledActive);
    takePicture = true;
  }

  if (ledActive) {
    /* Take still picture.
    * Unlike video stream(startStreaming) , this API wait to receive image data
    *  from camera device.
    */

    Serial.println("call takePicture()");
    CamImage pictImg = theCamera.takePicture();

    /* Check availability of the img instance. */
    /* If any errors occur, the img is not available. */
    if (pictImg.isAvailable())
      {
        takingPicture = true;
        captureFrames = true;
        Serial.print("Save taken picture as ");
        Serial.print(filename.c_str());
        Serial.println("");

        /* Create new file. */
        File myFile = theSD.open((filename + ".JPG").c_str(), FILE_WRITE);
        myFile.write(pictImg.getImgBuff(), pictImg.getImgSize());
        myFile.close();
      }
    else
      {
        /* The size of a picture may exceed the allocated memory size.
          * Then, allocate the larger memory size and/or decrease the size of a picture.
          * [How to allocate the larger memory]
          * - Decrease jpgbufsize_divisor specified by setStillPictureImageFormat()
          * - Increase the Memory size from Arduino IDE tools Menu
          * [How to decrease the size of a picture]
          * - Decrease the JPEG quality by setJPEGQuality()
          */

        Serial.println("Failed to take picture");
      }
    takingPicture = false;
    takePicture = false;
    while (captureFrames) { // CamCB must complete before filename is incremented
      delay(5);
    }
    std::stringstream filenameStr;
    filenameStr << "PICT" << std::setw(3) << std::setfill('0') << ++take_picture_count;
    filename = filenameStr.str();
    ledActive = false;
    ledOff(ledPin);
    lastButton = HIGH;
  }
}
