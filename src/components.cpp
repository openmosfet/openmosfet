#include "components.h"
#include "configuration.h"
#include "inputsInterface.h"

//----------------------------- TRIGGER -------------------------------------
OMVirtualTrigger::TriggerState OMVirtualTrigger::_state = OMVirtualTrigger::stateReleased;

void OMVirtualTrigger::pull(void)
{
#ifdef DEBUG
  Serial.println("OMVirtualTrigger::pull");
#endif
  OMVirtualTrigger::_state = OMVirtualTrigger::statePulled;
  OMVirtualReplica::triggerPulled();
  OMVirtualReplica::updateLastActive();
}

void OMVirtualTrigger::release(void)
{
#ifdef DEBUG
  Serial.println("OMVirtualTrigger::release");
#endif
  OMVirtualTrigger::_state = OMVirtualTrigger::stateReleased;
  OMVirtualReplica::triggerReleased();
  OMVirtualReplica::updateLastActive();
}

//----------------------------- GEARBOX -------------------------------------

OMVirtualGearbox::GearboxState OMVirtualGearbox::_state = OMVirtualGearbox::stateResting;
OMVirtualGearbox::GearboxCycleState OMVirtualGearbox::_cycleState = OMVirtualGearbox::stateCocking;
unsigned int OMVirtualGearbox::_precockDuration_ms = 0;
unsigned long OMVirtualGearbox::_precockEndTime_ms = 0;

void OMVirtualGearbox::cycle(unsigned int precockDuration_ms)
{
  if(OMVirtualGearbox::_state != OMVirtualGearbox::stateCycling)
  {
    #ifdef DEBUG
      Serial.println("OMVirtualGearbox::cycle");
    #endif
    OMInputsInterface::motorOn();
    OMVirtualGearbox::_precockDuration_ms = precockDuration_ms;
    OMVirtualGearbox::_state = OMVirtualGearbox::stateCycling;
    OMVirtualGearbox::_cycleState = OMVirtualGearbox::stateCocking;
  }
}

void OMVirtualGearbox::cycleEndDetected(void)
{
#ifdef DEBUG
  Serial.println("OMVirtualGearbox::cycleEndDetected");
#endif
  if (OMVirtualGearbox::_precockDuration_ms <= 0 || !OMConfiguration::enablePrecocking)
  {
    OMVirtualGearbox::endCycle(OMVirtualGearbox::stateResting);
  }
  else
  {
    OMVirtualGearbox::_precockEndTime_ms = millis() + _precockDuration_ms;
    OMVirtualGearbox::_cycleState = OMVirtualGearbox::statePrecocking;
  }
}

void OMVirtualGearbox::update(void)
{
  if (
      OMVirtualGearbox::_state == OMVirtualGearbox::stateCycling &&
      OMVirtualGearbox::_cycleState == OMVirtualGearbox::statePrecocking)
  {
    if (millis() >= OMVirtualGearbox::_precockEndTime_ms)
    {
      OMVirtualGearbox::endCycle(OMVirtualGearbox::statePrecocked);
    }
  }
}

void OMVirtualGearbox::endCycle(OMVirtualGearbox::GearboxState state)
{
#ifdef DEBUG
  Serial.println("endCycle");
#endif
  OMInputsInterface::motorOff();

  OMVirtualGearbox::_state = state;
  OMVirtualReplica::endFiringCycle();
}

//----------------------------- SELECTOR -------------------------------------
OMVirtualSelector::SelectorState OMVirtualSelector::_state = OMVirtualSelector::stateSafe;

void OMVirtualSelector::setState(OMVirtualSelector::SelectorState state)
{
#ifdef DEBUG
  Serial.println("OMVirtualSelector::setState");
#endif
  _state = state;
  OMVirtualReplica::updateLastActive();
}

//----------------------------- REPLICA -------------------------------------

#define COND_SAFETY_OFF OMVirtualSelector::getState() != OMVirtualSelector::stateSafe
#define COND_GEARBOX_NOT_CYCLING OMVirtualGearbox::getState() != OMVirtualGearbox::stateCycling
#define COND_TRIGGER_PULLED OMVirtualTrigger::getState() == OMVirtualTrigger::statePulled
#define COND_BURST_NOT_INTERRUPTIBLE OMInputsInterface::getCurrentFiringSetting().getBurstMode() != OMFiringSettings::burstModeInterruptible
#define COND_BURST_NOT_FINISHED OMVirtualReplica::_bbs_fired < OMInputsInterface::getCurrentFiringSetting().getBurstLength()
#define COND_BURST_EXTENDIBLE OMInputsInterface::getCurrentFiringSetting().getBurstMode() == OMFiringSettings::burstModeExtendible
#define COND_TRIGGER_AS_BEEN_MAINTAINED_AFTER_LAST_END_CYCLE OMVirtualReplica::_lastTriggerReleaseMs < OMVirtualReplica::_lastEndCycleMs && OMVirtualTrigger::getState() == OMVirtualTrigger::statePulled

uint8_t OMVirtualReplica::_bbs_fired = 0;
OMVirtualReplica::ReplicaState OMVirtualReplica::_state = OMVirtualReplica::stateIdle;
uint8_t OMVirtualReplica::_currentBurstBBCount = 0;
unsigned long OMVirtualReplica::_lastActiveTimeMs = 0;
unsigned long OMVirtualReplica::_lastTriggerReleaseMs = 0;
unsigned long OMVirtualReplica::_lastEndCycleMs = 0;

void OMVirtualReplica::begin(void)
{
  OMVirtualReplica::_lastActiveTimeMs = millis();
  OMVirtualReplica::_lastTriggerReleaseMs = millis();
  OMVirtualReplica::_lastEndCycleMs = millis();
}



void OMVirtualReplica::update(void)
{
  // uncock if trigger is maintained after a set amount of time
  if(
    OMVirtualReplica::_state == OMVirtualReplica::stateIdle
    &&
    OMVirtualGearbox::getState() == OMVirtualGearbox::statePrecocked
    &&
    OMConfiguration::decockAfter_s > 0
    &&
    COND_TRIGGER_AS_BEEN_MAINTAINED_AFTER_LAST_END_CYCLE
    &&
    OMVirtualReplica::_lastEndCycleMs + (unsigned long)(OMConfiguration::decockAfter_s * 1000) < millis()
    )
  {
    OMVirtualGearbox::cycle(0);
  }

  if (millis() - OMVirtualReplica::_lastActiveTimeMs > OMConfiguration::deepSleepDelayMinutes * 60000)
  {
#ifdef DEBUG
    Serial.print("deepsleep enabled after ");
    Serial.print(float(millis() - OMVirtualReplica::_lastActiveTimeMs) / 60000);
    Serial.println(" minutes.");
#endif

    //ESP.deepSleep(0);//esp8266
    esp_deep_sleep_start();
  }
  else
  {
    OMVirtualGearbox::update();
  }
}

void OMVirtualReplica::updateLastActive(void)
{
  OMVirtualReplica::_lastActiveTimeMs = millis();
}

void OMVirtualReplica::triggerPulled(void)
{
  #ifdef DEBUG
    Serial.println("triggerPulled");
  #endif
  OMVirtualReplica::_bbs_fired = 0;
  OMVirtualReplica::startFiringCycle();
}

void OMVirtualReplica::triggerReleased(void)
{
#ifdef DEBUG
  Serial.println("triggerReleased");
#endif
  OMVirtualReplica::_lastTriggerReleaseMs = millis();
  #ifdef DEBUG
    printf("OMVirtualReplica::_lastTriggerReleaseMs : %lu\n", OMVirtualReplica::_lastTriggerReleaseMs);
  #endif
  OMVirtualReplica::_state = OMVirtualReplica::stateIdle;
}

void OMVirtualReplica::startFiringCycle(void)
{
#ifdef DEBUG
  Serial.println("startFiringCycle");
#endif

  if
  (
    COND_SAFETY_OFF
    &&
    COND_GEARBOX_NOT_CYCLING
    &&
    (
      (
        COND_TRIGGER_PULLED
        &&
        (COND_BURST_NOT_FINISHED || COND_BURST_EXTENDIBLE)
      )
      ||
      (COND_BURST_NOT_INTERRUPTIBLE && COND_BURST_NOT_FINISHED)
    )
  )
  {
    OMVirtualReplica::_state = OMVirtualReplica::stateFiring;
    OMVirtualGearbox::cycle(OMInputsInterface::getCurrentFiringSetting().getPrecockDurationMs());
  }else{
    OMVirtualReplica::_state = OMVirtualReplica::stateIdle;
    #ifdef DEBUG
      Serial.println("stateIdle");
    #endif
  }
}

void OMVirtualReplica::endFiringCycle(void)
{
  #ifdef DEBUG
    Serial.println("endFiringCycle");
  #endif

  OMVirtualReplica::_lastEndCycleMs = millis();
  #ifdef DEBUG
    printf("OMVirtualReplica::_lastEndCycleMs : %lu\n", OMVirtualReplica::_lastEndCycleMs);
  #endif
  OMVirtualReplica::_bbs_fired++;
  OMVirtualReplica::startFiringCycle();
}
