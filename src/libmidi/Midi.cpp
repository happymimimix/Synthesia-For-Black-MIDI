
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#include "Midi.h"
#include "MidiEvent.h"
#include "MidiTrack.h"
#include "MidiUtil.h"
#include <algorithm>

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

Midi Midi::ReadFromStream(istream& stream)
{
    Midi m;

    const static string MidiFileHeader = "MThd";
    const static string RiffFileHeader = "RIFF";
    char header_id[5] = { 0, 0, 0, 0, 0 };

    // Read header ID and handle RIFF format if detected
    stream.read(header_id, 4);
    string header(header_id);
    if (header != MidiFileHeader) {
        if (header != RiffFileHeader) throw MidiError(MidiError_UnknownHeaderType);

        // Skip RIFF header (assuming handling is correct)
        unsigned long throw_away;
        stream.read(reinterpret_cast<char*>(&throw_away), sizeof(unsigned long) * 4);

        // Recursively read MIDI from the rest of the stream
        return ReadFromStream(stream);
    }

    // Read the rest of the header
    unsigned long header_length;
    unsigned short format, track_count, time_division;
    stream.read(reinterpret_cast<char*>(&header_length), sizeof(unsigned long));
    stream.read(reinterpret_cast<char*>(&format), sizeof(unsigned short));
    stream.read(reinterpret_cast<char*>(&track_count), sizeof(unsigned short));
    stream.read(reinterpret_cast<char*>(&time_division), sizeof(unsigned short));

    if (stream.fail()) throw MidiError(MidiError_NoHeader);

    // Validate MIDI format and track count
    header_length = BigToSystem32(header_length);
    if (header_length != 6) throw MidiError(MidiError_BadHeaderSize);

    enum MidiFormat { MidiFormat0 = 0, MidiFormat1, MidiFormat2 };
    format = BigToSystem16(format);
    if (format == MidiFormat2) throw MidiError(MidiError_Type2MidiNotSupported);

    track_count = BigToSystem16(track_count);
    if (format == 0 && track_count != 1) throw MidiError(MidiError_BadType0Midi);

    // Process time division (assuming no SMPTE support)
    time_division = BigToSystem16(time_division);
    if ((time_division & 0x8000) != 0) throw MidiError(MidiError_SMTPETimingNotImplemented);

    unsigned short pulses_per_quarter_note = time_division;

    // Read tracks
    for (int i = 0; i < track_count; ++i) {
        m.m_tracks.push_back(MidiTrack::ReadFromStream(stream));
    }

    m.BuildTempoTrack();

    // Set track IDs and translate notes and events
    for (size_t i = 0; i < m.m_tracks.size(); ++i) {
        m.m_tracks[i].SetTrackId(i);
        m.TranslateNotes(m.m_tracks[i].Notes(), pulses_per_quarter_note);

        MidiEventMicrosecondList event_usecs;
        const MidiEventPulsesList& event_pulses = m.m_tracks[i].EventPulses();
        for (size_t j = 0; j < event_pulses.size(); ++j) {
            event_usecs.push_back(m.GetEventPulseInMicroseconds(event_pulses[j], pulses_per_quarter_note));
        }
        m.m_tracks[i].SetEventUsecs(event_usecs);
    }

    m.m_initialized = true;

    // Calculate song length and dead air time
    m.m_microsecond_base_song_length = m.m_translated_notes.rbegin()->end;
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
// This allows quick(er) calculation of wall-clock event times
void Midi::BuildTempoTrack()
{
   // This map will help us get rid of duplicate events if
   // the tempo is specified in every track (as is common).
   //
   // It also does sorting for us so we can just copy the
   // events right over to the new track.
   std::map<unsigned long, MidiEvent> tempo_events;

   // Run through each track looking for tempo events
   for (MidiTrackList::iterator t = m_tracks.begin(); t != m_tracks.end(); ++t)
   {
      for (size_t i = 0; i < t->Events().size(); ++i)
      {
         MidiEvent ev = t->Events()[i];
         unsigned long ev_pulses = t->EventPulses()[i];

         if (ev.Type() == MidiEventType_Meta && ev.MetaType() == MidiMetaEvent_TempoChange)
         {
            // Pull tempo event out of both lists
            //
            // Vector is kind of a hassle this way -- we have to
            // walk an iterator to that point in the list because
            // erase MUST take an iterator... but erasing from a
            // list invalidates iterators.  bleah.
            MidiEventList::iterator event_to_erase = t->Events().begin();
            MidiEventPulsesList::iterator event_pulse_to_erase = t->EventPulses().begin();
            for (size_t j = 0; j < i; ++j) { ++event_to_erase; ++event_pulse_to_erase; }

            t->Events().erase(event_to_erase);
            t->EventPulses().erase(event_pulse_to_erase);

            // Adjust next event's delta time
            if (t->Events().size() > i)
            {
               // (We just erased the element at i, so
               // now i is pointing to the next element)
               unsigned long next_dt = t->Events()[i].GetDeltaPulses();

               t->Events()[i].SetDeltaPulses(ev.GetDeltaPulses() + next_dt);
            }

            // We have to roll i back for the next loop around
            --i;

            // Insert our newly stolen event into the auto-sorting map
            tempo_events[ev_pulses] = ev;
         }
      }
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

microseconds_t Midi::GetEventPulseInMicroseconds(unsigned long event_pulses, unsigned short pulses_per_quarter_note) const
{
   if (m_tracks.size() == 0) return 0;
   const MidiTrack &tempo_track = m_tracks.back();

   microseconds_t running_result = 0;

   bool hit = false;
   unsigned long last_tempo_event_pulses = 0;
   microseconds_t running_tempo = DefaultUSTempo;
   for (size_t i = 0; i < tempo_track.Events().size(); ++i)
   {
      unsigned long tempo_event_pulses = tempo_track.EventPulses()[i];

      // If the time we're asking to convert is still beyond
      // this tempo event, just add the last time slice (at
      // the previous tempo) to the running wall-clock time.
      unsigned long delta_pulses = 0;
      if (event_pulses > tempo_event_pulses)
      {
         delta_pulses = tempo_event_pulses - last_tempo_event_pulses;
      }
      else
      {
         hit = true;
         delta_pulses = event_pulses - last_tempo_event_pulses;
      }

      running_result += ConvertPulsesToMicroseconds(delta_pulses, running_tempo, pulses_per_quarter_note);

      // If the time we're calculating is before the tempo event we're
      // looking at, we're done.
      if (hit) break;

      running_tempo = tempo_track.Events()[i].GetTempoInUsPerQn();
      last_tempo_event_pulses = tempo_event_pulses;
   }

   // The requested time may be after the very last tempo event
   if (!hit)
   {
      unsigned long remaining_pulses = event_pulses - last_tempo_event_pulses;
      running_result += ConvertPulsesToMicroseconds(remaining_pulses, running_tempo, pulses_per_quarter_note);
   }

   return running_result;
}

void Midi::Reset(microseconds_t lead_in_microseconds, microseconds_t lead_out_microseconds)
{
   m_microsecond_lead_out = lead_out_microseconds;
   m_microsecond_song_position = m_microsecond_dead_start_air - lead_in_microseconds;
   m_first_update_after_reset = true;

   for (MidiTrackList::iterator i = m_tracks.begin(); i != m_tracks.end(); ++i) { i->Reset(); }
}

void Midi::TranslateNotes(const NoteSet& notes, unsigned short pulses_per_quarter_note)
{
    for (const auto& note : notes) {
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
