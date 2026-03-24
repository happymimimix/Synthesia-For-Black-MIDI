// Synthesia
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#include "Midi.h"
#include "MidiEvent.h"
#include "MidiTrack.h"
#include "MidiUtil.h"

#include <fstream>
#include <sstream>
#include <iterator>
#include <map>
#include <algorithm>

using namespace std;

Midi Midi::ReadFromFile(const wstring &filename)
{
#if defined WIN32
   fstream file(reinterpret_cast<const wchar_t*>((filename).c_str()), ios::in|ios::binary);
#else
   // TODO: This isn't Unicode!
   // MACTODO: Test to see if opening a unicode filename works.  I bet it doesn't.
   std::string narrow(filename.begin(), filename.end());
   fstream file(narrow.c_str(), ios::in | ios::binary);
#endif

   if (!file.good()) throw MidiError(MidiError_BadFilename);

   Midi m;

   try
   {
      m = ReadFromStream(file);
   }
   catch (const MidiError &e)
   {
      // Close our file resource before handing the error up
      file.close();
      throw e;
   }

   return m;
}

Midi Midi::ReadFromStream(istream &stream)
{
   Midi m;

   // header_id is always "MThd" by definition
   const static string MidiFileHeader = "MThd";
   const static string RiffFileHeader = "RIFF";

   // I could use (MidiFileHeader.length() + 1), but then this has to be
   // dynamically allocated.  More hassle than it's worth.  MIDI is well
   // defined and will always have a 4-byte header.  We use 5 so we get
   // free null termination.
   char           header_id[5] = { 0, 0, 0, 0, 0 };
   unsigned long  header_length;
   unsigned short format;
   unsigned short track_count;
   unsigned short time_division;

   stream.read(header_id, 4);
   string header(header_id);
   if (header != MidiFileHeader)
   {
      if (header != RiffFileHeader) throw MidiError(MidiError_UnknownHeaderType);
      else
      {
         // There's no way this could be wong, so let's just skip it.
         unsigned long throw_away;
         stream.read(reinterpret_cast<char*>(&throw_away), sizeof(unsigned long) * 4);

         // Call this recursively, without the RIFF header this time
         return ReadFromStream(stream);
      }
   }

   stream.read(reinterpret_cast<char*>(&header_length), sizeof(unsigned long));
   stream.read(reinterpret_cast<char*>(&format),        sizeof(unsigned short));
   stream.read(reinterpret_cast<char*>(&track_count),   sizeof(unsigned short));
   stream.read(reinterpret_cast<char*>(&time_division), sizeof(unsigned short));

   if (stream.fail()) throw MidiError(MidiError_NoHeader);

   // Chunk Size is always 6 by definition
   const static unsigned int MidiFileHeaderChunkLength = 6;

   header_length = BigToSystem32(header_length);
   if (header_length != MidiFileHeaderChunkLength)
   {
      throw MidiError(MidiError_BadHeaderSize);
   }

   enum MidiFormat { MidiFormat0 = 0, MidiFormat1, MidiFormat2 };

   format = BigToSystem16(format);
   if (format == MidiFormat2)
   {
      // MIDI 0: All information in 1 track
      // MIDI 1: Multiple tracks intended to be played simultaneously
      // MIDI 2: Multiple tracks intended to be played separately
      //
      // We do not support MIDI 2 at this time
      throw MidiError(MidiError_Type2MidiNotSupported);
   }

   track_count = BigToSystem16(track_count);
   if (format == 0 && track_count != 1)
   {
      // MIDI 0 has only 1 track by definition
      throw MidiError(MidiError_BadType0Midi);
   }

   // Time division can be encoded two ways based on a bit-flag:
   // - pulses per quarter note (15-bits)
   // - SMTPE frames per second (7-bits for SMPTE frame count and 8-bits for clock ticks per frame)
   time_division = BigToSystem16(time_division);
   bool in_smpte = ((time_division & 0x8000) != 0);

   if (in_smpte)
   {
      throw MidiError(MidiError_SMTPETimingNotImplemented);
   }

   // We ignore the possibility of SMPTE timing, so we can
   // use the time division value directly as PPQN.
   unsigned short pulses_per_quarter_note = time_division;

   // Read in our tracks
   for (int i = 0; i < track_count; ++i)
   {
      m.m_tracks.push_back(MidiTrack::ReadFromStream(stream));
   }

   m.BuildTempoTrack();
   m.BuildTempoIndex(pulses_per_quarter_note);
   m.BuildBeatLines(pulses_per_quarter_note);

   // Tell our tracks their IDs
   for (size_t i = 0; i < m.m_tracks.size(); ++i)
   {
      m.m_tracks[i].SetTrackId(i);

   // Translate each track's list of notes and list
   // of events into microseconds.

      m.TranslateNotes(m.m_tracks[i].Notes(), pulses_per_quarter_note);

      MidiEventMicrosecondList event_usecs;
      const MidiEventPulsesList& event_pulses = m.m_tracks[i].EventPulses();

      // Event pulses are sorted, so we can use the cursor-hint
      // version for O(n + t) total instead of O(n log t).
      size_t tempo_hint = 0;
      for (size_t j = 0; j < event_pulses.size(); ++j) {
         event_usecs.push_back(m.GetEventPulseInMicroseconds(event_pulses[j], pulses_per_quarter_note, tempo_hint));
      }
      m.m_tracks[i].SetEventUsecs(event_usecs);
   }

   m.m_initialized = true;

   // Just grab the end of the last note to find out how long the song is
   m.m_microsecond_base_song_length = m.m_translated_notes.rbegin()->end;

   // Eat everything up until *just* before the first note event
   m.m_microsecond_dead_start_air = m.GetEventPulseInMicroseconds(m.FindFirstNotePulse(), pulses_per_quarter_note) - 1;
   
   return m;
}

// NOTE: This is required for much of the other functionality provided
// by this class, however, this causes a destructive change in the way
// the MIDI is represented internally which means we can never save the
// file back out to disk exactly as we loaded it.
//
// This adds an extra track dedicated to tempo change events.  Tempo events
// are extracted from every other track and placed in the new one.
//
// While we're at it, we also collect time signature events (without
// extracting them) so BuildBeatLines can use them later.
//
// This allows quick(er) calculation of wall-clock event times
void Midi::BuildTempoTrack()
{
   // This map will help us get rid of duplicate events if
   // the tempo is specified in every track (as is common).
   //
   // It also does sorting for us so we can just copy the
   // events right over to the new track.
   std::map<unsigned long, MidiEvent> tempo_events;
   std::map<unsigned long, MidiEvent> timesig_events;

   // Run through each track looking for tempo events
   for (MidiTrackList::iterator t = m_tracks.begin(); t != m_tracks.end(); ++t)
   {
      for (size_t i = t->Events().size(); i > 0; --i)
      {
         size_t idx = i - 1;
         MidiEvent ev = t->Events()[idx];
         unsigned long ev_pulses = t->EventPulses()[idx];

         bool is_tempo = (ev.Type() == MidiEventType_Meta && ev.MetaType() == MidiMetaEvent_TempoChange);
         bool is_timesig = (ev.Type() == MidiEventType_Meta && ev.MetaType() == MidiMetaEvent_TimeSignature);

         if (!is_tempo && !is_timesig) continue;

         if (is_tempo) tempo_events[ev_pulses] = ev;
         if (is_timesig) timesig_events[ev_pulses] = ev;

         // Adjust next event's delta time before erasing
         if (idx + 1 < t->Events().size())
         {
            unsigned long next_dt = t->Events()[idx + 1].GetDeltaPulses();
            t->Events()[idx + 1].SetDeltaPulses(ev.GetDeltaPulses() + next_dt);
         }

         t->Events().erase(t->Events().begin() + idx);
         t->EventPulses().erase(t->EventPulses().begin() + idx);
      }
   }

   // Store collected time signature data as parallel arrays
   for (std::map<unsigned long, MidiEvent>::const_iterator i = timesig_events.begin(); i != timesig_events.end(); ++i)
   {
      m_timesig_pulse_marks.push_back(i->first);
      m_timesig_numerators.push_back(i->second.GetTimeSignatureNumerator());
      m_timesig_denominators.push_back(i->second.GetTimeSignatureDenominator());
   }

   // Create a new track (always the last track in the track list)
   m_tracks.push_back(MidiTrack::CreateBlankTrack());

   MidiEventList &tempo_track_events = m_tracks[m_tracks.size()-1].Events();
   MidiEventPulsesList &tempo_track_event_pulses = m_tracks[m_tracks.size()-1].EventPulses();

   // Copy over all our tempo events
   unsigned long previous_absolute_pulses = 0;
   for (std::map<unsigned long, MidiEvent>::const_iterator i = tempo_events.begin(); i != tempo_events.end(); ++i)
   {
      unsigned long absolute_pulses = i->first;
      MidiEvent ev = i->second;

      // Reset each of their delta times while we go
      ev.SetDeltaPulses(absolute_pulses - previous_absolute_pulses);
      previous_absolute_pulses = absolute_pulses;

      // Add them to the track
      tempo_track_event_pulses.push_back(absolute_pulses);
      tempo_track_events.push_back(ev);
   }
}

unsigned long Midi::FindFirstNotePulse()
{
   unsigned long first_note_pulse = 0;

   // Find the very last value it could ever possibly be, to start with
   for (MidiTrackList::const_iterator t = m_tracks.begin(); t != m_tracks.end(); ++t)
   {
      if (t->EventPulses().size() == 0) continue;
      unsigned long pulses = t->EventPulses().back();

      if (pulses > first_note_pulse) first_note_pulse = pulses;
   }

   // Now run through each event in each track looking for the very
   // first note_on event
   for (MidiTrackList::const_iterator t = m_tracks.begin(); t != m_tracks.end(); ++t)
   {
      for (size_t ev_id = 0; ev_id < t->Events().size(); ++ev_id)
      {
         if (t->Events()[ev_id].Type() == MidiEventType_NoteOn)
         {
            unsigned long note_pulse = t->EventPulses()[ev_id];

            if (note_pulse < first_note_pulse) first_note_pulse = note_pulse;

            // We found the first note event in this
            // track.  No need to keep searching.
            break;
         }
      }
   }

   return first_note_pulse;
}

microseconds_t Midi::ConvertPulsesToMicroseconds(unsigned long pulses, microseconds_t tempo, unsigned short pulses_per_quarter_note)
{
   // Here's what we have to work with:
   //   pulses is given
   //   tempo is given (units of microseconds/quarter_note)
   //   (pulses/quarter_note) is given as a constant in this object file
   const double quarter_notes = static_cast<double>(pulses) / static_cast<double>(pulses_per_quarter_note);
   const double microseconds = quarter_notes * static_cast<double>(tempo);

   return static_cast<microseconds_t>(microseconds);
}

// Builds a lookup table from the tempo track so that pulse-to-microsecond
// conversion can be done with a binary search instead of a linear scan.
// Each entry records the pulse position, cumulative wall-clock time, and
// tempo value at that point.
void Midi::BuildTempoIndex(unsigned short pulses_per_quarter_note)
{
   m_tempo_ppqn = pulses_per_quarter_note;
   m_tempo_pulse_marks.clear();
   m_tempo_usec_marks.clear();
   m_tempo_values.clear();

   // The initial state before any tempo event: 120 BPM at pulse 0
   m_tempo_pulse_marks.push_back(0);
   m_tempo_usec_marks.push_back(0);
   m_tempo_values.push_back(DefaultUSTempo);

   if (m_tracks.size() == 0) return;
   const MidiTrack &tempo_track = m_tracks.back();

   microseconds_t running_usec = 0;
   unsigned long last_pulse = 0;
   microseconds_t current_tempo = DefaultUSTempo;

   for (size_t i = 0; i < tempo_track.Events().size(); ++i)
   {
      unsigned long pulse = tempo_track.EventPulses()[i];

      // Accumulate wall-clock time for the segment we just passed
      running_usec += ConvertPulsesToMicroseconds(pulse - last_pulse, current_tempo, pulses_per_quarter_note);

      current_tempo = tempo_track.Events()[i].GetTempoInUsPerQn();
      last_pulse = pulse;

      m_tempo_pulse_marks.push_back(pulse);
      m_tempo_usec_marks.push_back(running_usec);
      m_tempo_values.push_back(current_tempo);
   }
}

// Walks through the entire song and generates beat and bar lines based
// on time signature events collected during BuildTempoTrack.
//
// The resulting lists are sorted and stored in microseconds so the
// display code can quickly find visible lines with a linear scan.
void Midi::BuildBeatLines(unsigned short pulses_per_quarter_note)
{
   m_beat_lines.clear();
   m_bar_lines.clear();

   if (m_tracks.size() == 0) return;

   // Find the last pulse in the song so we know when to stop
   unsigned long last_pulse = 0;
   for (size_t t = 0; t < m_tracks.size(); ++t)
   {
      if (m_tracks[t].EventPulses().size() > 0)
      {
         unsigned long p = m_tracks[t].EventPulses().back();
         if (p > last_pulse) last_pulse = p;
      }
   }

   if (last_pulse == 0) return;

   // Default time signature is 4/4
   unsigned char numerator = 4;
   unsigned char denominator = 4;

   // "pulses per beat" depends on the denominator:
   //    pulses_per_beat = ppqn * 4 / denominator
   // This works because PPQN gives us pulses per quarter note,
   // and (4 / denominator) scales to the actual beat unit.
   unsigned long pulses_per_beat = pulses_per_quarter_note * 4 / denominator;

   size_t next_timesig = 0;
   size_t tempo_hint = 0;
   unsigned long current_pulse = 0;
   unsigned char beat_in_bar = 0;

   while (current_pulse <= last_pulse)
   {
      // Check if we've reached a time signature change
      if (next_timesig < m_timesig_pulse_marks.size() && current_pulse >= m_timesig_pulse_marks[next_timesig])
      {
         numerator = m_timesig_numerators[next_timesig];
         denominator = m_timesig_denominators[next_timesig];

         if (numerator == 0) numerator = 4;
         if (denominator == 0) denominator = 4;

         pulses_per_beat = pulses_per_quarter_note * 4 / denominator;
         if (pulses_per_beat == 0) pulses_per_beat = pulses_per_quarter_note;

         // Reset to beat 1 of the new time signature
         beat_in_bar = 0;
         current_pulse = m_timesig_pulse_marks[next_timesig];
         ++next_timesig;
      }

      microseconds_t usec = GetEventPulseInMicroseconds(current_pulse, pulses_per_quarter_note, tempo_hint);

      if (beat_in_bar == 0) m_bar_lines.push_back(usec);
      else m_beat_lines.push_back(usec);

      ++beat_in_bar;
      if (beat_in_bar >= numerator) beat_in_bar = 0;

      current_pulse += pulses_per_beat;
   }
}

// Binary search version for single lookups.
microseconds_t Midi::GetEventPulseInMicroseconds(unsigned long event_pulses, unsigned short pulses_per_quarter_note) const
{
   if (m_tempo_pulse_marks.size() == 0) return 0;

   // Binary search for the last tempo mark <= event_pulses.
   // upper_bound returns the first element strictly greater,
   // so we step back one to find the segment we're in.
   std::vector<unsigned long>::const_iterator it = std::upper_bound(m_tempo_pulse_marks.begin(), m_tempo_pulse_marks.end(), event_pulses);
   size_t seg = (it - m_tempo_pulse_marks.begin()) - 1;

   unsigned long remaining_pulses = event_pulses - m_tempo_pulse_marks[seg];
   return m_tempo_usec_marks[seg] + ConvertPulsesToMicroseconds(remaining_pulses, m_tempo_values[seg], pulses_per_quarter_note);
}

// Cursor-hint version for sorted (non-decreasing) pulse sequences.
// The hint is advanced forward as needed, so converting an entire
// sorted list costs O(n + t) total instead of O(n log t).
microseconds_t Midi::GetEventPulseInMicroseconds(unsigned long event_pulses, unsigned short pulses_per_quarter_note, size_t &hint) const
{
   if (m_tempo_pulse_marks.size() == 0) return 0;

   // Advance the hint forward until we find the right segment
   while (hint + 1 < m_tempo_pulse_marks.size() && m_tempo_pulse_marks[hint + 1] <= event_pulses)
   {
      ++hint;
   }

   unsigned long remaining_pulses = event_pulses - m_tempo_pulse_marks[hint];
   return m_tempo_usec_marks[hint] + ConvertPulsesToMicroseconds(remaining_pulses, m_tempo_values[hint], pulses_per_quarter_note);
}

void Midi::Reset(microseconds_t lead_in_microseconds, microseconds_t lead_out_microseconds)
{
   m_microsecond_lead_out = lead_out_microseconds;
   m_microsecond_song_position = m_microsecond_dead_start_air - lead_in_microseconds;
   m_first_update_after_reset = true;

   for (MidiTrackList::iterator i = m_tracks.begin(); i != m_tracks.end(); ++i) { i->Reset(); }
}

void Midi::TranslateNotes(const NoteSet &notes, unsigned short pulses_per_quarter_note)
{
   for (const auto& note : notes)
   {
      TranslatedNote trans;
      
      trans.note_id = note.note_id;
      trans.track_id = note.track_id;
      trans.channel = note.channel;
      trans.velocity = note.velocity;
      trans.start = GetEventPulseInMicroseconds(note.start, pulses_per_quarter_note);
      trans.end = GetEventPulseInMicroseconds(note.end, pulses_per_quarter_note);

      m_translated_notes.insert(trans);
   }
}

MidiEventListWithTrackId Midi::Update(microseconds_t delta_microseconds)
{
   MidiEventListWithTrackId aggregated_events;
   if (!m_initialized) return aggregated_events;

   m_microsecond_song_position += delta_microseconds;
   if (m_first_update_after_reset)
   {
      delta_microseconds += m_microsecond_song_position;
      m_first_update_after_reset = false;
   }

   if (delta_microseconds == 0) return aggregated_events;
   if (m_microsecond_song_position < 0) return aggregated_events;
   if (delta_microseconds > m_microsecond_song_position) delta_microseconds = m_microsecond_song_position;

   const size_t track_count = m_tracks.size();
   for (size_t i = 0; i < track_count; ++i)
   {
      MidiEventList track_events = m_tracks[i].Update(delta_microseconds);

      const size_t event_count = track_events.size();
      for (size_t j = 0; j < event_count; ++j)
      {
         aggregated_events.insert(aggregated_events.end(), std::pair<size_t, MidiEvent>(i, track_events[j]));
      }
   }

   return aggregated_events;
}

microseconds_t Midi::GetSongLengthInMicroseconds() const
{
   if (!m_initialized) return 0;
   return m_microsecond_base_song_length - m_microsecond_dead_start_air;
}

unsigned int Midi::AggregateEventsRemain() const
{
   if (!m_initialized) return 0;

   unsigned int aggregate = 0;
   for (MidiTrackList::const_iterator i = m_tracks.begin(); i != m_tracks.end(); ++i)
   {
      aggregate += i->AggregateEventsRemain();
   }
   return aggregate;
}

unsigned int Midi::AggregateNotesRemain() const
{
   if (!m_initialized) return 0;

   unsigned int aggregate = 0;
   for (MidiTrackList::const_iterator i = m_tracks.begin(); i != m_tracks.end(); ++i)
   {
      aggregate += i->AggregateNotesRemain();
   }
   return aggregate;
}

unsigned int Midi::AggregateEventCount() const
{
   if (!m_initialized) return 0;

   unsigned int aggregate = 0;
   for (MidiTrackList::const_iterator i = m_tracks.begin(); i != m_tracks.end(); ++i)
   {
      aggregate += i->AggregateEventCount();
   }
   return aggregate;
}

unsigned int Midi::AggregateNoteCount() const
{
   if (!m_initialized) return 0;

   unsigned int aggregate = 0;
   for (MidiTrackList::const_iterator i = m_tracks.begin(); i != m_tracks.end(); ++i)
   {
      aggregate += i->AggregateNoteCount();
   }
   return aggregate;
}

double Midi::GetSongPercentageComplete() const
{
   if (!m_initialized) return 0.0;

   const double pos = static_cast<double>(m_microsecond_song_position - m_microsecond_dead_start_air);
   const double len = static_cast<double>(GetSongLengthInMicroseconds());

   if (pos < 0) return 0.0;
   if (len == 0) return 1.0;

   return std::min( (pos / len), 1.0 );
}

bool Midi::IsSongOver() const
{
   if (!m_initialized) return true;
   return (m_microsecond_song_position - m_microsecond_dead_start_air) >= GetSongLengthInMicroseconds() + m_microsecond_lead_out;
}
