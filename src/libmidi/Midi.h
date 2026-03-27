// Synthesia
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#ifndef __MIDI_H
#define __MIDI_H

#include <iostream>
#include <vector>

#include "Note.h"
#include "MidiTrack.h"
#include "MidiTypes.h"

class MidiError;
class MidiEvent;

typedef std::vector<MidiTrack> MidiTrackList;
typedef std::vector<MidiEvent> MidiEventList;
typedef std::vector<std::pair<unsigned short, MidiEvent>> MidiEventListWithTrackId;

// NOTE: This library's MIDI loading and handling is destructive.  Perfect
//       1:1 serialization routines will not be possible without quite a
//       bit of additional work.
class Midi
{
public:
   static Midi ReadFromFile(const std::wstring &filename);
   static Midi ReadFromStream(std::istream &stream);

   const std::vector<MidiTrack> &Tracks() const { return m_tracks; }

   const TranslatedNoteSet &Notes() const { return m_translated_notes; }

   MidiEventListWithTrackId Update(microseconds_t delta_microseconds);

   void Reset(microseconds_t lead_in_microseconds, microseconds_t lead_out_microseconds);

   microseconds_t GetSongPositionInMicroseconds() const { return m_microsecond_song_position; }
   microseconds_t GetSongLengthInMicroseconds() const;

   microseconds_t GetDeadAirStartOffsetMicroseconds() const { return m_microsecond_dead_start_air; }

   // This doesn't include lead-in (so it's perfect for a progress bar).
   // (It is also clamped to [0.0, 1.0], so lead-in and lead-out won't give any
   // unexpected results.)
   double GetSongPercentageComplete() const;

   // This will report when the lead-out period is complete.
   bool IsSongOver() const;

   unsigned int AggregateEventsRemain() const;
   unsigned int AggregateEventCount() const;

   unsigned int AggregateNotesRemain() const;
   unsigned int AggregateNoteCount() const;

   // These contain the microsecond positions of every beat line and
   // bar line in the song, sorted in ascending order.  Bar lines land
   // on beat 1 of each measure; beat lines land on every other beat.
   const std::vector<microseconds_t> &BeatLines() const { return m_beat_lines; }
   const std::vector<microseconds_t> &BarLines() const { return m_bar_lines; }

private:
   const static unsigned long DefaultBPM = 120;
   const static microseconds_t OneMinuteInMicroseconds = 60000000;
   const static microseconds_t DefaultUSTempo = OneMinuteInMicroseconds / DefaultBPM;

   static microseconds_t ConvertPulsesToMicroseconds(unsigned long pulses, microseconds_t tempo, unsigned short pulses_per_quarter_note);

   Midi(): m_initialized(false), m_microsecond_dead_start_air(0), m_tempo_ppqn(0) { Reset(0, 0); }
   
   // The tempo index lets us do this in O(log n) instead of the old
   // linear scan.
   microseconds_t GetEventPulseInMicroseconds(unsigned long event_pulses, unsigned short pulses_per_quarter_note) const;

   // This overload remembers where it left off between calls, so
   // converting a sorted list of pulses is essentially free after
   // the first lookup.  (The caller just has to make sure the hint
   // starts at 0 and the input pulses are non-decreasing.)
   microseconds_t GetEventPulseInMicroseconds(unsigned long event_pulses, unsigned short pulses_per_quarter_note, size_t &hint) const;

   unsigned long FindFirstNotePulse();

   void BuildTempoTrack();
   void BuildTempoIndex(unsigned short pulses_per_quarter_note);
   void BuildBeatLines(unsigned short pulses_per_quarter_note);
   void TranslateNotes(const NoteSet &notes, unsigned short pulses_per_quarter_note, unsigned short track_id);

   bool m_initialized;

   // These three parallel arrays cache the cumulative wall-clock time
   // at each tempo change so we don't have to recalculate from the
   // beginning every time.
   std::vector<unsigned long>  m_tempo_pulse_marks;
   std::vector<microseconds_t> m_tempo_usec_marks;
   std::vector<microseconds_t> m_tempo_values;
   unsigned short m_tempo_ppqn;

   // Time signature data collected during BuildTempoTrack.
   std::vector<unsigned long>  m_timesig_pulse_marks;
   std::vector<unsigned char>  m_timesig_numerators;
   std::vector<unsigned char>  m_timesig_denominators;

   // Beat and bar line positions (in microseconds) for the display
   std::vector<microseconds_t> m_beat_lines;
   std::vector<microseconds_t> m_bar_lines;

   TranslatedNoteSet m_translated_notes;

   // Position can be negative (for lead-in).
   microseconds_t m_microsecond_song_position;
   microseconds_t m_microsecond_base_song_length;

   microseconds_t m_microsecond_lead_out;
   microseconds_t m_microsecond_dead_start_air;

   bool m_first_update_after_reset;
   MidiTrackList m_tracks;
};

#endif
