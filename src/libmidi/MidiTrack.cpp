
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#include "MidiTrack.h"
#include "MidiEvent.h"
#include "MidiUtil.h"
#include "Midi.h"

#include <array>
#include <sstream>
#include <string>
#include <map>

using namespace std;

MidiTrack MidiTrack::ReadFromStream(std::istream& stream)
{
    const static std::string MidiTrackHeader = "MTrk";
    char header_id[5] = { 0, 0, 0, 0, 0 };
    unsigned long track_length = 0;

    stream.read(header_id, MidiTrackHeader.length());
    if (stream.fail() || std::string(header_id) != MidiTrackHeader)
    {
        throw MidiError(MidiError_BadTrackHeaderType);
    }

    stream.read(reinterpret_cast<char*>(&track_length), sizeof(unsigned long));
    if (stream.fail())
    {
        throw MidiError(MidiError_TrackHeaderTooShort);
    }

    track_length = BigToSystem32(track_length);

    std::vector<char> buffer(track_length);
    stream.read(buffer.data(), track_length);

    if (stream.fail())
    {
        throw MidiError(MidiError_TrackTooShort);
    }

    // Use stringstream for binary data processing
    std::istringstream event_stream(std::string(buffer.begin(), buffer.end()), std::ios::binary);

    MidiTrack t;

    char last_status = 0;
    unsigned long current_pulse_count = 0;
    while (event_stream.peek() != EOF)
    {
        MidiEvent ev = MidiEvent::ReadFromStream(event_stream, last_status);
        last_status = ev.StatusCode();

        t.m_events.push_back(std::move(ev)); // This line uses a lot of cpu according to profiler

        current_pulse_count += ev.GetDeltaPulses();
        t.m_event_pulses.push_back(current_pulse_count);
    }

    t.BuildNoteSet();
    t.DiscoverInstrument();

    return t;
}

struct NoteInfo
{
   int velocity;
   unsigned char channel;
   unsigned long pulses;
};

void MidiTrack::BuildNoteSet()
{
   m_note_set.clear();

   // Keep a list of all the notes currently "on" (and the pulse that
   // it was started).  On a note_on event, we create an element.  On
   // a note_off event we check that an element exists, make a "Note",
   // and remove the element from the list.  If there is already an
   // element on a note_on we both cap off the previous "Note" and
   // begin a new one.
   //
   // A note_on with velocity 0 is a note_off
   std::map<NoteId, NoteInfo> m_active_notes;

   for (size_t i = 0; i < m_events.size(); ++i)
   {
      const MidiEvent &ev = m_events[i];
      if (ev.Type() != MidiEventType_NoteOn && ev.Type() != MidiEventType_NoteOff) continue;

      bool on = (ev.Type() == MidiEventType_NoteOn && ev.NoteVelocity() > 0);
      NoteId id = ev.NoteNumber();

      // Check for an active note
      std::map<NoteId, NoteInfo>::iterator find_ret = m_active_notes.find(id);
      bool active_event = (find_ret !=  m_active_notes.end());

      // Close off the last event if there was one
      if (active_event)
      {
         Note n;
         n.start = find_ret->second.pulses;
         n.end = m_event_pulses[i];
         n.note_id = id;
         n.channel = find_ret->second.channel;
         n.velocity = find_ret->second.velocity;

         // NOTE: This must be set at the next level up.  The track
         // itself has no idea what its index is.
         n.track_id = 0;

         // Add a note and remove this NoteId from the active list
         m_note_set.insert(n);
         m_active_notes.erase(find_ret);
      }

      // We've handled any active events.  If this was a note_off we're done.
      if (!on) continue;

      // Add a new active event
      NoteInfo info;
      info.channel = ev.Channel();
      info.velocity = ev.NoteVelocity();
      info.pulses = m_event_pulses[i];

      m_active_notes[id] = info;
   }

   if (m_active_notes.size() > 0)
   {
      // LOGTODO!
   
      // This is mostly non-critical.
      //
      // Erroring out would be needlessly restrictive against
      // promiscuous MIDI files.  As-is, a note just won't be
      // inserted if it isn't closed properly.
   }
}

void MidiTrack::DiscoverInstrument()
{
   // Default to Program 0 per the MIDI Standard
   m_instrument_id = 0;
   bool instrument_found = false;


   // Check to see if any/all of the notes
   // in this track use Channel 10.
   bool any_note_uses_percussion = false;
   bool any_note_does_not_use_percussion = false;

   for (size_t i = 0; i < m_events.size(); ++i)
   {
      const MidiEvent &ev = m_events[i];
      if (ev.Type() != MidiEventType_NoteOn) continue;

      if (ev.Channel() == 9) any_note_uses_percussion = true;
      if (ev.Channel() != 9) any_note_does_not_use_percussion = true;
   }

   if (any_note_uses_percussion && !any_note_does_not_use_percussion)
   {
      m_instrument_id = InstrumentIdPercussion;
      return;
   }

   if (any_note_uses_percussion && any_note_does_not_use_percussion)
   {
      m_instrument_id = InstrumentIdVarious;
      return;
   }

   for (size_t i = 0; i < m_events.size(); ++i)
   {
      const MidiEvent &ev = m_events[i];
      if (ev.Type() != MidiEventType_ProgramChange) continue;

      // If we've already hit a different instrument in this
      // same track, just tag it as "various" and exit early
      //
      // Also check that the same instrument isn't just set
      // multiple times in the same track
      if (instrument_found && m_instrument_id != ev.ProgramNumber())
      {
         m_instrument_id = InstrumentIdVarious;
         return;
      }

      m_instrument_id = ev.ProgramNumber();
      instrument_found = true;
   }
}

void MidiTrack::SetTrackId(size_t track_id)
{
   NoteSet old = m_note_set;
   
   m_note_set.clear();
   for (NoteSet::const_iterator i = old.begin(); i != old.end(); ++i)
   {
      Note n = *i;
      n.track_id = track_id;
      
      m_note_set.insert(n);
   }
}

void MidiTrack::Reset()
{
   m_running_microseconds = 0;
   m_last_event = -1;

   m_notes_remaining = static_cast<unsigned int>(m_note_set.size());
}

MidiEventList MidiTrack::Update(microseconds_t delta_microseconds)
{
   m_running_microseconds += delta_microseconds;

   MidiEventList evs;
   for (size_t i = m_last_event + 1; i < m_events.size(); ++i)
   {
      if (m_event_usecs[i] <= m_running_microseconds)
      {
         evs.push_back(m_events[i]);
         m_last_event = static_cast<long>(i);

         if (m_events[i].Type() == MidiEventType_NoteOn &&
            m_events[i].NoteVelocity() > 0) m_notes_remaining--;
      }
      else break;
   }

   return evs;
}
