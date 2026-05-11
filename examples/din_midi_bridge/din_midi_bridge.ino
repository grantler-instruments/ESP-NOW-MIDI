#include <MIDI.h>
#include "enomik_client.h"

// ---- Pin config -------------------------------------------------------
#define DIN_MIDI_RX_PIN  16   // adjust for your board
#define DIN_MIDI_TX_PIN  17   // adjust for your board

// ---- Peer MAC (run print_mac on the dongle and paste here) ------------
// uint8_t peerMacAddress[6] = { 0xCC, 0x8D, 0xA2, 0x8B, 0x85, 0x1C };

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, DIN_MIDI);
enomik::Client _client;

void onDinNoteOn(byte ch, byte note, byte vel)              { _client.sendNoteOn(note, vel, ch); }
void onDinNoteOff(byte ch, byte note, byte vel)             { _client.sendNoteOff(note, vel, ch); }
void onDinCC(byte ch, byte ctrl, byte val)                  { _client.sendControlChange(ctrl, val, ch); }
void onDinPC(byte ch, byte prog)                            { _client.sendProgramChange(prog, ch); }
void onDinAT(byte ch, byte pressure)                        { _client.sendAfterTouch(pressure, ch); }
void onDinPolyAT(byte ch, byte note, byte pressure)         { _client.sendPolyAfterTouch(note, pressure, ch); }
void onDinPitchBend(byte ch, int val)                       { _client.sendPitchBend(val, ch); }
void onDinSongPos(unsigned beats)                           { _client.sendSongPosition(beats); }
void onDinSongSel(byte song)                                { _client.sendSongSelect(song); }
void onDinClock()                                           { _client.sendClock(); }
void onDinStart()                                           { _client.sendStart(); }
void onDinContinue()                                        { _client.sendContinue(); }
void onDinStop()                                            { _client.sendStop(); }

void setup() {
  Serial.begin(115200);

  Serial1.begin(31250, SERIAL_8N1, DIN_MIDI_RX_PIN, DIN_MIDI_TX_PIN);
  DIN_MIDI.begin(MIDI_CHANNEL_OMNI);
  DIN_MIDI.turnThruOff();

  DIN_MIDI.setHandleNoteOn(onDinNoteOn);
  DIN_MIDI.setHandleNoteOff(onDinNoteOff);
  DIN_MIDI.setHandleControlChange(onDinCC);
  DIN_MIDI.setHandleProgramChange(onDinPC);
  DIN_MIDI.setHandleAfterTouchChannel(onDinAT);
  DIN_MIDI.setHandleAfterTouchPoly(onDinPolyAT);
  DIN_MIDI.setHandlePitchBend(onDinPitchBend);
  DIN_MIDI.setHandleSongPosition(onDinSongPos);
  DIN_MIDI.setHandleSongSelect(onDinSongSel);
  DIN_MIDI.setHandleClock(onDinClock);
  DIN_MIDI.setHandleStart(onDinStart);
  DIN_MIDI.setHandleContinue(onDinContinue);
  DIN_MIDI.setHandleStop(onDinStop);

  _client.begin();
  // _client.addPeer(peerMacAddress);

  _client.setHandleNoteOn(           [](byte ch, byte note, byte vel)      { DIN_MIDI.sendNoteOn(note, vel, ch);          });
  _client.setHandleNoteOff(          [](byte ch, byte note, byte vel)      { DIN_MIDI.sendNoteOff(note, vel, ch);         });
  _client.setHandleControlChange(    [](byte ch, byte ctrl, byte val)      { DIN_MIDI.sendControlChange(ctrl, val, ch);   });
  _client.setHandleProgramChange(    [](byte ch, byte prog)                { DIN_MIDI.sendProgramChange(prog, ch);        });
  _client.setHandleAfterTouchChannel([](byte ch, byte pressure)            { DIN_MIDI.sendAfterTouch(pressure, ch);       });
  _client.setHandleAfterTouchPoly(   [](byte ch, byte note, byte pressure) { DIN_MIDI.sendAfterTouch(note, pressure, ch); });
  _client.setHandlePitchBend(        [](byte ch, int val)                  { DIN_MIDI.sendPitchBend(val, ch);             });
  _client.setHandleSongPosition(     [](uint16_t pos)                      { DIN_MIDI.sendSongPosition(pos);              });
  _client.setHandleSongSelect(       [](byte song)                         { DIN_MIDI.sendSongSelect(song);               });
  _client.setHandleClock(            []()                                  { DIN_MIDI.sendClock();                        });
  _client.setHandleStart(            []()                                  { DIN_MIDI.sendStart();                        });
  _client.setHandleContinue(         []()                                  { DIN_MIDI.sendContinue();                     });
  _client.setHandleStop(             []()                                  { DIN_MIDI.sendStop();                         });

  // Register with the dongle
  _client.sendControlChange(127, 127, 16);
}

void loop() {
  DIN_MIDI.read();    // DIN MIDI IN -> ESP-NOW MIDI
  _client.loop();     // ESP-NOW MIDI receive -> DIN MIDI OUT
}
