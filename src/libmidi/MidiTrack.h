// Synthesia
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#ifndef __MIDI_TRACK_H
#define __MIDI_TRACK_H

#include <vector>
#include <iostream>

#include "Note.h"
#include "MidiEvent.h"
#include "MidiUtil.h"

class Midi;
class MidiEvent;

typedef std::vector<MidiEvent> MidiEventList;
typedef std::pair<MidiEvent*,MidiEvent*> MidiEventListRange;

#pragma pack(push, 1)
class MidiTrack
{
public:
   static MidiTrack ReadFromStream(std::istream &stream);
   static MidiTrack CreateBlankTrack() { return MidiTrack(); }

   const MidiEventList *Events() const { return &m_events; }

   const std::wstring InstrumentName() const { return InstrumentNames[m_instrument_id]; }

   // Reports whether this track contains any Note-On MIDI events
   // (vs. just being an information track with a title or copyright)
   bool hasNotes() const { return (m_note_count > 0); }

   void Reset();
   MidiEventListRange Update(microseconds_t delta_microseconds);

   unsigned int AggregateNoteCount() const { return m_note_count; }

   void BuildNoteSet(TranslatedNoteSet* translated_notes, unsigned short pulses_per_quarter_note, unsigned short track_id);

private:
   MidiTrack() : m_instrument_id(0), m_note_count(0) { Reset(); }

   void DiscoverInstrument();

   MidiEventList m_events = {};

   unsigned int m_note_count;

   unsigned char m_instrument_id;

   microseconds_t m_running_microseconds;
   size_t m_last_event;
};
#pragma pack(pop)

#endif
