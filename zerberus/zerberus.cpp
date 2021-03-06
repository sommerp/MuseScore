//=============================================================================
//  Zerberus
//  Zample player
//
//  Copyright (C) 2013 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "midievent.h"
#include "libmscore/midipatch.h"

#include "zerberus.h"
#include "voice.h"
#include "channel.h"
#include "instrument.h"
#include "zone.h"

#include <stdio.h>

bool Zerberus::initialized = false;

//---------------------------------------------------------
//   Zerberus
//---------------------------------------------------------

Zerberus::Zerberus()
   : Synthesizer()
      {
      if (!initialized) {
            initialized = true;
            Voice::init();
            }
      for (int i = 0; i < MAX_VOICES; ++i)
            freeVoices.push(new Voice);
      for (int i = 0; i < MAX_CHANNEL; ++i)
            _channel[i] = new Channel(this, i);
      }

//---------------------------------------------------------
//   ~Zerberus
//---------------------------------------------------------

Zerberus::~Zerberus()
      {
      }

//---------------------------------------------------------
//   programChange
//---------------------------------------------------------

void Zerberus::programChange(int /*channel*/, int /*program*/)
      {
      }

//---------------------------------------------------------
//   trigger
//    gui
//---------------------------------------------------------

void Zerberus::trigger(Channel* channel, int key, int velo, Trigger trigger)
      {
      Instrument* i = channel->instrument();
// printf("trigger %d %d type %d\n", key, velo, trigger);
      for (Zone* z : i->zones()) {
            if (z->match(channel, key, velo, trigger)) {
//                  printf("  match %d\n", z->trigger);
                  if (freeVoices.empty()) {
                        printf("out of voices...\n");
                        return;
                        }
                  Voice* voice = freeVoices.pop();
                  if (!voice->isOff())
                        abort();
                  voice->start(channel, key, velo, z);
                  if (trigger == Trigger::RELEASE)
                        voice->stop();    // start voice in stop mode
                  voice->setNext(activeVoices);
                  activeVoices = voice;

                  //
                  // handle offBy voices
                  //
                  if (z->group) {
                        for (Voice* v = activeVoices; v; v = v->next()) {
                              if (v->offBy() == z->group) {
                                    if (v->offMode() == OffMode::FAST)
                                          v->stop(1);
                                    else
                                          v->stop();
                                    }
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   play
//---------------------------------------------------------

void Zerberus::play(const MidiEvent& event)
      {
      midiEvents.push(event);
      }

//---------------------------------------------------------
//   loadInstrument
//    return true on success
//---------------------------------------------------------

bool Zerberus::loadInstrument(const QString& path)
      {
      if (path.isEmpty())
            return false;
      busy = true;
      Instrument* instr = new Instrument(this);
      if (instr->load(path)) {
            instruments.push_back(instr);
            if (instruments.size() == 1) {
                  for (int i = 0; i < MAX_CHANNEL; ++i)
                        _channel[i]->setInstrument(instr);
                  }
            busy = false;
            return true;
            }
      busy = false;
      delete instr;
      return false;
      }

//---------------------------------------------------------
//   instrument
//---------------------------------------------------------

Instrument* Zerberus::instrument(int program) const
      {
      for(Instrument* i : instruments) {
            if (i->program() == program)
                  return i;
            }
      qFatal("instrument for program %d not found", program);
      return 0;
      }

//---------------------------------------------------------
//   processNoteOff
//---------------------------------------------------------

void Zerberus::processNoteOff(Channel* cp, int key)
      {
      for (Voice* v = activeVoices; v; v = v->next()) {
            if ((v->channel() == cp)
               && (v->key() == key)
               && (v->loopMode() != LoopMode::ONE_SHOT)
               ) {
                  if (cp->sustain() < 0x40) {
                        v->stop();
                        trigger(cp, key, v->velocity(), Trigger::RELEASE);
                        }
                  else {
                        if (v->isPlaying()) {
                              // printf("sustain %p\n", v);
                              v->sustained();
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   processNoteOn
//---------------------------------------------------------

void Zerberus::processNoteOn(Channel* cp, int key, int velo)
      {
      for (Voice* v = activeVoices; v; v = v->next()) {
            if (v->channel() == cp && v->key() == key) {
                  if (v->isSustained()) {
                        // if (v->isPlaying())
                        // printf("retrigger (stop) %p\n", v);
                        v->stop(100);     // fast stop
                        }
                  }
            }
      trigger(cp, key, velo, Trigger::ATTACK);
      }

//---------------------------------------------------------
//   process
//---------------------------------------------------------

void Zerberus::process(const MidiEvent& event)
      {
      if (busy)
            return;
      Channel* cp = _channel[int(event.channel())];
      if (cp->instrument() == 0) {
//            printf("Zerberus::process(): no instrument for channel %d\n", event.channel());
            return;
            }

      switch(event.type()) {
            case MidiEventType::NOTEOFF:
                  processNoteOff(cp, event.dataA());
                  break;
            case MidiEventType::NOTEON: {
                  int key = event.dataA();
                  int vel = event.dataB();
                  if (vel)
                        processNoteOn(cp, key, vel);
                  else
                        processNoteOff(cp, key);
                  }
                  break;
            case MidiEventType::CONTROLLER:
                  cp->controller(event.dataA(), event.dataB());
                  break;
            default:
                  printf("event type 0x%02x\n", event.type());
                  break;
            }
      }

//---------------------------------------------------------
//   process
//    realtime
//---------------------------------------------------------

void Zerberus::process(int frames, float* p)
      {
      if (busy)
            return;
      while (!midiEvents.empty())
            process(midiEvents.pop());

      Voice* v = activeVoices;
      Voice* pv = 0;
      int n = 0;
      int nn = 0;
      while (v) {
            ++nn;
            v->process(frames, p);
            if (v->isOff()) {
                  if (pv)
                        pv->setNext(v->next());
                  else
                        activeVoices = v->next();
                  ++n;
                  freeVoices.push(v);
                  }
            else
                  pv = v;
            v = v->next();
            }
      }

//---------------------------------------------------------
//   name
//---------------------------------------------------------

const char* Zerberus::name() const
      {
      return "Zerberus";
      }

//---------------------------------------------------------
//   process
//---------------------------------------------------------

void Zerberus::process(unsigned n, float* p, float)
      {
      process(n, p);
      }

//---------------------------------------------------------
//   play
//---------------------------------------------------------

void Zerberus::play(const Event& e)
      {
      MidiEvent me(MidiEventType(e.type()), e.channel(), e.dataA(), e.dataB());
      midiEvents.push(me);
      }

//---------------------------------------------------------
//   getPatchInfo
//---------------------------------------------------------

const QList<MidiPatch*>& Zerberus::getPatchInfo() const
      {
      static QList<MidiPatch*> pl;
      qDeleteAll(pl);
      pl.clear();
      int idx = 0;
      for (Instrument* i : instruments) {
            MidiPatch* p = new MidiPatch { false, 2, 0, idx, i->name() };
            pl.append(p);
            ++idx;
            }
      return pl;
      }

//---------------------------------------------------------
//   init
//---------------------------------------------------------

void Zerberus::init()
      {
      }

//---------------------------------------------------------
//   allSoundsOff
//---------------------------------------------------------

void Zerberus::allSoundsOff(int channel)
      {
      allNotesOff(channel);
      }

//---------------------------------------------------------
//   allNotesOff
//---------------------------------------------------------

void Zerberus::allNotesOff(int channel)
      {
      MidiEvent me(MidiEventType::CONTROLLER, channel, CTRL_ALL_NOTES_OFF, 0);
      if (channel == -1) {
            for (int i = 0; i < MAX_CHANNEL; ++i) {
                  me.setChannel(i);
                  midiEvents.push(me);
                  }
            }
      else
            midiEvents.push(me);
      }

//---------------------------------------------------------
//   loadSoundFonts
//---------------------------------------------------------

bool Zerberus::loadSoundFonts(const QStringList& sl)
      {
      foreach(const QString& s, sl) {
            if (!loadInstrument(s))
                  return false;
            }
      return true;
      }

//---------------------------------------------------------
//   soundFonts
//---------------------------------------------------------

QStringList Zerberus::soundFonts() const
      {
      QStringList sl;
      for (Instrument* i : instruments) {
            printf("  soundFonts <%s>\n", qPrintable(i->path()));
            sl.append(i->path());
            }
      return sl;
      }

//---------------------------------------------------------
//   addSoundFont
//---------------------------------------------------------

bool Zerberus::addSoundFont(const QString& s)
      {
      return loadInstrument(s);
      }

//---------------------------------------------------------
//   removeSoundFont
//---------------------------------------------------------

bool Zerberus::removeSoundFont(const QString& s)
      {
      for (Instrument* i : instruments) {
            if (i->path() == s) {
                  auto it = find(instruments.begin(), instruments.end(), i);
                  if (it == instruments.end())
                        return false;
                  instruments.erase(it);
                  for (int k = 0; k < MAX_CHANNEL; ++k) {
                        if (_channel[k]->instrument() == i)
                              _channel[k]->setInstrument(0);
                        }
                  if (!instruments.empty()) {
                        for (int i = 0; i < MAX_CHANNEL; ++i) {
                              if (_channel[i]->instrument() == 0)
                                    _channel[i]->setInstrument(instruments.front());
                              }
                        }
                  delete i;
                  return true;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   state
//---------------------------------------------------------

SyntiState Zerberus::state() const
      {
      SyntiState sp;

      QStringList sfl = soundFonts();
      foreach(QString sf, sfl)
            sp.append(SyntiParameter(SParmId(ZERBERUS_ID, 0, 0).val, "sfz-font", sf));
      return sp;
      }

//---------------------------------------------------------
//   setState
//---------------------------------------------------------

void Zerberus::setState(SyntiState& sp)
      {
      QStringList sfs;
      for (int i = 0; i < sp.size(); ++i) {
            SyntiParameter* p = &sp[i];
            if (p->id() == -1 && p->name() == "sfz-font")
                  sfs.append(p->sval());
            }
      loadSoundFonts(sfs);
      }

