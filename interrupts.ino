/*
with help from:
- https://github.com/ProjectsByJRP/M0_Sleep_External_Int/blob/master/sleep_with_ext_int_pin6.ino (BUT NOT PIN 6!)
- https://github.com/GabrielNotman/AutonomoTesting/blob/master/Interrupts/SleepChangeInterrupt/SleepChangeInterrupt.ino
- https://github.com/cavemoa/Feather-M0-Adalogger/blob/master/SimpleSleepUSB/SimpleSleepUSB.ino#L36
*/

#ifdef MOTION_ACTIVATED

void setupInterrupts() {
  // these interrupts are used for the motion sensor to wake us from sleep
  // Attach the interrupt and set the wake flag
  attachInterrupt(digitalPinToInterrupt(START_PIN), ISR, RISING);

  // Set the XOSC32K to run in standby (needed for RISING)
  SYSCTRL->XOSC32K.bit.RUNSTDBY = 1;

  // Configure EIC to use GCLK1 which uses XOSC32K
  // This has to be done after the first call to attachInterrupt()
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(GCM_EIC) | GCLK_CLKCTRL_GEN_GCLK1 | GCLK_CLKCTRL_CLKEN;

  // Set sleep mode
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
}

#endif

#ifdef NIGHT_SOUNDS

// use rtc for turning off lights/music after a set amount of time
void alarmMatch() { g_music_on = false; }

void setupInterrupts() {
  // these interrupts are used for the timer to stop playing music

  if (alarm_hours or alarm_minutes or alarm_seconds) {
    rtc.attachInterrupt(alarmMatch);
    rtc.begin();
  }
}

#endif

void ISR() {
  // TODO: is there anything we actually want to do during the interrupt?
}

void sleepUntilInterrupt() {
  DEBUG_PRINTLN("Sleeping... (Serial will disconnect)");

  if (VS1053_RESET != -1) {
    // turn off music player
    digitalWrite(VS1053_RESET, LOW);
    delay(100);
  }

  // everything should already be off by now, but just in case...
  fill_solid(leds, num_LEDs, CRGB::Black);

#ifdef DEBUG
  Serial.end();
  USBDevice.detach(); // Safely detach the USB prior to sleeping
#endif

  // Enter sleep mode. this should save us power (power drops to 90mA which is still higher than expected
  __WFI(); // Wait for interrupt

  // we are awake!
#ifdef DEBUG
  USBDevice.attach(); // Re-attach the USB, audible sound on windows machines

  // TODO: might need more of a delay here for serial to work well
  Serial.begin(115200);

  DEBUG_PRINTLN("Awake!");
#endif

  if (VS1053_RESET != -1) {
    // turn the music player back on
    setupMusicPlayer();
  }
}
