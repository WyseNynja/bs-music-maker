#define DEBUG
#include <bs_debug.h>

// TODO: comment this out for night sounds. uncomment this for motion activated player (wip)
#define MOTION_ACTIVATED

// based on
// https://github.com/adafruit/Adafruit_VS1053_Library/blob/master/examples/feather_player/feather_player.ino

#include "Arduino.h"
#include <Adafruit_VS1053.h>
#include <EDB.h>
#include <FastLED.h>
#include <IniFile.h>
#include <SD.h>
#include <SPI.h>
#include <RTCZero.h>

#include "types.h"

// These are the pins used
#define VS1053_RESET -1 // VS1053 reset pin (not used!)

#define SDCARD_CS 5   // already wired for us. Card chip select pin. already wired for us
#define VS1053_CS 6   // already wired for us. VS1053 chip select pin (output)
// DREQ should be an Int pin *if possible* (not possible on 32u4)
// pin 9 is also used for checking voltage. i guess we can't do that anymore :(
#define VS1053_DREQ 9 // already wired for us (but not marked on board). VS1053 Data request, ideally an Interrupt pin
#define VS1053_DCS 10 // already wired for us. VS1053 Data/command select pin (output)
#define START_PIN 11  // TODO: what pin?
#define LED_DATA 12    // TODO: what pin?
#define RED_LED 13     // already wired for us
#define SPI_MISO 22
#define SPI_MOSI 23
#define SPI_SCK 24

#define LED_CHIPSET NEOPIXEL
const int num_LEDs = 60;
CRGB leds[num_LEDs];

// TODO: put on SD?
#define LED_FADE_RATE 100

Adafruit_VS1053_FilePlayer musicPlayer =
    Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, SDCARD_CS);

RTCZero rtc;

// rotating "base color" used by some patterns
uint8_t g_hue = 0;

// define database ids
#ifdef MOTION_ACTIVATED

// TODO: v2: don't hard code this. loop over directories to create playlists instead
#define PLAYLIST_MUSIC 0
#define NUM_PLAYLISTS 1

#else  // !MOTION_ACTIVATED

// TODO: v2: don't hard code this. loop over directories to create playlists instead
#define PLAYLIST_SPOKEN_WORD 0
#define PLAYLIST_NIGHT_SOUNDS 1
#define NUM_PLAYLISTS 2

#endif  // MOTION_ACTIVATED

PlaylistData playlist_data_buffer;

Playlist playlists[NUM_PLAYLISTS];

// these are set by config or fallback to defaults
// TODO: making this unsigned makes IniConfig sad. they shouldn't ever be negative though!
int default_brightness, frames_per_second, alarm_hours, alarm_minutes, alarm_seconds;

void setup() {
  // if you're using Bluefruit or LoRa/RFM Feather, disable the BLE interface
  // pinMode(8, INPUT_PULLUP);

#ifdef DEBUG
  Serial.begin(115200);

  delay(1000);
  while (!Serial) {
    ; // wait for serial port to connect so we can watch all prints during setup
  }
#else
  delay(2000);
#endif  // DEBUG

  DEBUG_PRINTLN(F("Setting up..."));
  delay(4000);  // TODO: remove this once sleep code is well tested

  // for night sounds, this is a button
  // for motion activated, this is a pir sensor
  // NOTE: pir sensor is NOT accurate until it has been plugged in for 20 seconds or so.
  pinMode(START_PIN, INPUT);

  if (!musicPlayer.begin()) { // initialise the music player
    DEBUG_PRINTLN(F("Couldn't find VS1053, do you have the right pins defined?"));
    while (1)
      ;
  }

  DEBUG_PRINTLN(F("VS1053 found"));

  // TODO: loop here and wait for the SD instead
  if (!SD.begin(SDCARD_CS)) {
    DEBUG_PRINTLN(F("SD failed, or not present"));
    while (1)
      ; // don't do anything more
  }
  delay(100); // SD sometimes takes some time to wake up
  DEBUG_PRINTLN("SD OK!");

  setupConfig();

  // TODO: re-enable this
  setupLights();

  // Set volume for left, right channels. lower numbers == louder volume!
  // TODO: config volume off SD card
  // TODO: constrain 0-255
  musicPlayer.setVolume(10, 0);

  musicPlayer.sineTest(0x44, 500); // Make a tone for 500ms to indicate VS1053 is working

  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);  // DREQ int

  loadPlaylists();

#ifdef MOTION_ACTIVATED
  setupInterrupts();
#else  // !MOTION_ACTIVATED
  if (alarm_hours or alarm_minutes or alarm_seconds) {
    rtc.attachInterrupt(alarmMatch);
    rtc.begin();
  }
#endif // MOTION_ACTIVATED

  DEBUG_PRINTLN(F("Starting..."));
}

/*
 * this stuff was in its own .ino files, but something was broken about it
 * i think i fixed it by moving the struct definitions to types.h, but I'm not sure how best to move db_file and db defs
 */

// TODO: what table size?
#define TABLE_SIZE 4096 * 2

// The max number of records that should be created = (TABLE_SIZE - sizeof(EDB_Header)) / sizeof(LogEvent).
// If you try to insert more, operations will return EDB_OUT_OF_RANGE for all records outside the usable range.

const char *db_name = "playlist.db";
File db_file;

// The read and write handlers for using the SD Library
// Also blinks the led while writing/reading
// database entries start at 1, but that's crazy
inline void writer(unsigned long address, const byte *data, unsigned int recsize) {
  digitalWrite(RED_LED, HIGH);
  db_file.seek(address + 1);
  db_file.write(data, recsize);
  db_file.flush();
  digitalWrite(RED_LED, LOW);
}

inline void reader(unsigned long address, byte *data, unsigned int recsize) {
  digitalWrite(RED_LED, HIGH);
  db_file.seek(address + 1);
  db_file.read(data, recsize);
  digitalWrite(RED_LED, LOW);
}

// Create an EDB object with the appropriate write and read handlers
// NOTE! These handlers do NOT open or close the database file!
EDB db(&writer, &reader);

/*
 * END things that should be moved into their own ino files
 */

bool g_lights_on = false;

#ifdef MOTION_ACTIVATED

// run when START_PIN is RISING
void playMotionActivated() {
  // TODO: save how many songs are played?
  playTrackFromPlaylist(playlists[PLAYLIST_MUSIC]);
}

// loop for motion activated sounds for a adopted porta potty
// TODO: this plays a whole song before turning off. should we tie it more to the PIR?
void loop() {
  static int off_frames = 0;
  static const int sleep_frames = 10 * 1000 / (1000 / frames_per_second); // give the lights enough frames to fade to black before sleeping
  static const int loop_delay = 1000 / frames_per_second;  // TODO: sleep less? 100/fps?

  updateLights(g_lights_on);

  if (musicPlayer.stopped()) {
    // TODO: pir sensor and music wing don't get along. https://forums.adafruit.com/viewtopic.php?f=19&t=138121
    if (true or digitalRead(START_PIN) == HIGH) {
      // there is still motion. start a new song and stay on
      DEBUG_PRINTLN("Motion detected!");
      playMotionActivated();
      g_lights_on = true;
      off_frames = 0;
    } else {
      // music has ended had there is no motion. turn off
      g_lights_on = false;
      off_frames++;  // todo: use millis instead

      if (off_frames >= sleep_frames) {
        sleep();

        playMotionActivated();
        g_lights_on = true;
        off_frames = 0;
      }
    }
  } else {
    g_lights_on = true;
    off_frames = 0;
  }

  FastLED.delay(loop_delay);
}

#else  // !MOTION_ACTIVATED

volatile bool g_music_on = true;

// use rtc for turning off lights/music after a set amount of time?
void alarmMatch() {
  g_music_on = false;
}

// loop for night sounds
void loop() {
  // sleep to save power. waiting for button press (START_PIN to be RISING)
  sleep();

  // button was pressed! start the show
  g_lights_on = true;   // TODO: make lights optional
  g_music_on = true;
  playTrackFromPlaylist(playlists[PLAYLIST_SPOKEN_WORD]);

  // wait for the meditation track to finish
  while (!musicPlayer.stopped()) {
    // TODO: re-enable once other things are working
    //updateLights(g_lights_on);
    //FastLED.delay(1000 / frames_per_second);
    delay(1000 / frames_per_second);
  }

  // set timer for night sounds to turn off now that the meditation is over
  if (alarm_hours or alarm_minutes or alarm_seconds) {
    rtc.setTime(0, 0, 0);
    rtc.setAlarmTime(alarm_hours, alarm_minutes, alarm_seconds);
    rtc.enableAlarm(rtc.MATCH_HHMMSS);
  }

  // loop night sounds
  while (true) {
    // the rtc will disable g_music_on if alarms are configured. otherwise night sounds will play until button is pressed
    // TODO: use bounce library instead of simple digitalRead?
    if (!g_music_on or digitalRead(START_PIN) == HIGH) {
      musicPlayer.stopPlaying();
      break;
    }

    playTrackFromPlaylist(playlists[PLAYLIST_NIGHT_SOUNDS]);

    //updateLights(g_lights_on);
    // NOTE! this is 100, not 1000 so that there is less audio delay when it loops
    // TODO: check if this is really necessary
    FastLED.delay(100 / frames_per_second);
  }
}

#endif // MOTION_ACTIVATED
