// Synthesia
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#ifndef __MIDI_EVENT_H
#define __MIDI_EVENT_H

#include <string>
#include <iostream>

#include "Note.h"
#include "MidiUtil.h"

enum PulseType : unsigned char {
   DeltaPulse,
   AbsPulse,
   AbsMicrosec
};

#pragma pack(push, 1)
struct MidiEventSimple
{
   MidiEventSimple() : status(0), byte1(0), byte2(0) { }
   MidiEventSimple(unsigned char s, unsigned char b1, unsigned char b2) : status(s), byte1(b1), byte2(b2) { }

   unsigned char status;
   unsigned char byte1;
   unsigned char byte2;
};

class MidiEvent
{
public:
   static MidiEvent ReadFromStream(std::istream &stream, unsigned char last_status);
   static MidiEvent Build(const MidiEventSimple &simple);
   static MidiEvent NullEvent();

   // NOTE: There is a VERY good chance you don't want to use this directly.
   // The only reason it's not private is because the standard containers
   // require a default constructor.
   MidiEvent() : m_status(0), m_data1(0), m_data2(0), m_pulses(0) { }

   // Returns true if the event could be expressed in a simple event.  (So, this will
   // return false for Meta and SysEx events.)
   bool GetSimpleEvent(MidiEventSimple *simple) const;

   MidiEventType Type() const;
   unsigned int GetDeltaPulses() const { return *reinterpret_cast<const unsigned int*>(&m_pulses); }
   unsigned long long GetAbsPulses() const { return m_pulses; }
   unsigned long long GetAbsMicrosecs() const { return m_pulses; }

   // This is generally for internal Midi library use only.
   void SetPulses(PulseType type, unsigned long long delta_pulses) { if (type == DeltaPulse) *reinterpret_cast<unsigned int*>(&m_pulses) = static_cast<unsigned int>(delta_pulses); else m_pulses = delta_pulses; }

   NoteId NoteNumber() const;

   // Returns a friendly name for this particular Note-On or Note-
   // Off event. (e.g. "A#2")  Returns empty string on other types
   // of events.
   static std::string NoteName(NoteId note_number);

   // Returns the "Program to change to" value if this is a Program
   // Change event, 0 otherwise.
   unsigned char ProgramNumber() const;

   // Returns the "velocity" of a Note-On (or 0 if this is a Note-
   // Off event).  Returns -1 for other event types.
   signed char NoteVelocity() const;

   void SetVelocity(unsigned char velocity);

   // Returns which type of meta event this is (or
   // MetaEvent_Unknown if type() is not EventType_Meta).
   MidiMetaEventType MetaType() const;

   // Retrieve the tempo from a tempo meta event in microseconds
   // per quarter note.  (Non-meta-tempo events will throw an error).
   unsigned int GetTempoInUsPerQn() const;

   // Retrieve the time signature numerator (beats per bar) and
   // denominator (beat unit, e.g. 4 = quarter note).  The
   // denominator is stored already decoded from the power-of-2
   // encoding used in the MIDI file.
   unsigned char GetTimeSignatureNumerator() const;
   unsigned char GetTimeSignatureDenominator() const;

   // Returns which channel this event operates on.  This is
   // only defined for standard MIDI events that require a
   // channel argument.
   unsigned char Channel() const;

   void SetChannel(unsigned char channel);

   // Returns the status code of the MIDI event
   unsigned char StatusCode() const { return m_status; }

private:
   void ReadMeta(std::istream &stream);
   void ReadSysEx(std::istream &stream);
   void ReadStandard(std::istream &stream);

   unsigned char m_status;
   unsigned char m_meta_type;
   unsigned char m_data1;
   unsigned char m_data2;
   unsigned long long m_pulses;
};
#pragma pack(pop)

#endif __MIDI_EVENT_H
