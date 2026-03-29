// Synthesia
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#include "MidiTrack.h"
#include "MidiEvent.h"
#include "MidiUtil.h"
#include "Midi.h"

#include <array>
#include <queue>
#include <string>

using namespace std;

// We used to jump through a couple hoops (vector -> string ->
// istringstream) just to get a seekable stream.  This avoids the
// extra copies by reading straight from the buffer we already have.
class MemoryReadBuffer : public std::streambuf
{
public:
   MemoryReadBuffer(char *base, size_t size)
   {
      setg(base, base, base + size);
   }

protected:
   pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override
   {
      char *next;
      if (dir == std::ios_base::beg) next = eback() + off;
      else if (dir == std::ios_base::cur) next = gptr() + off;
      else next = egptr() + off;

      if (next < eback() || next > egptr()) return pos_type(off_type(-1));
      setg(eback(), next, egptr());
      return pos_type(next - eback());
   }

   pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override
   {
      return seekoff(off_type(pos), std::ios_base::beg, mode);
   }
};

MidiTrack MidiTrack::ReadFromStream(std::istream &stream)
{
   // Verify the track header
   const static string MidiTrackHeader = "MTrk";

   // I could use (MidiTrackHeader.length() + 1), but then this has to be
   // dynamically allocated.  More hassle than it's worth.  MIDI is well
   // defined and will always have a 4-byte header.  We use 5 so we get
   // free null termination.
   char header_id[5] = { 0, 0, 0, 0, 0 };
   unsigned long track_length = 0;

   stream.read(header_id, MidiTrackHeader.length());
   if (stream.fail() || std::string(header_id) != MidiTrackHeader) throw MidiError(MidiError_BadTrackHeaderType);
   stream.read(reinterpret_cast<char*>(&track_length), sizeof(unsigned long));
   if (stream.fail()) throw MidiError(MidiError_TrackHeaderTooShort);

   // Pull the full track out of the file all at once -- there is an
   // End-Of-Track event, but this allows us handle malformed MIDI a
   // little more gracefully.
   track_length = BigToSystem32(track_length);

   std::vector<char> buffer(track_length);
   stream.read(buffer.data(), track_length);

   if (stream.fail())
   {
      std::vector<char>().swap(buffer);
      throw MidiError(MidiError_TrackTooShort);
   }

   // Read directly from the buffer in memory, avoiding the
   // double-copy that istringstream would require.
   MemoryReadBuffer membuf(buffer.data(), track_length);
   std::istream event_stream(&membuf);

   MidiTrack t;

   // Read events until we run out of track
   char last_status = 0;
   unsigned long current_pulse_count = 0;
   while (event_stream.peek() != EOF)
   {
      MidiEvent ev = MidiEvent::ReadFromStream(event_stream, last_status); 
      last_status = ev.StatusCode();
      
      t.m_events.push_back(ev);

      current_pulse_count += ev.GetDeltaPulses();
      t.m_event_pulses.push_back(current_pulse_count);
   }

   t.DiscoverInstrument();

   return t;
}

struct NoteInfo
{
   unsigned char velocity;
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
   std::array<std::queue<NoteInfo>,0x100> m_active_notes;

   for (size_t i = 0; i < m_events.size(); ++i)
   {
      const MidiEvent &ev = m_events[i];
      if (ev.Type() != MidiEventType_NoteOn && ev.Type() != MidiEventType_NoteOff) continue;

      bool on = (ev.Type() == MidiEventType_NoteOn && ev.NoteVelocity() > 0);
      NoteId id = ev.NoteNumber();

      // Close off the last event if there was one
      if (!on && !m_active_notes[id].empty())
      {
         NoteInfo &find_ret = m_active_notes[id].front();
         Note n;
         n.start = find_ret.pulses;
         n.end = m_event_pulses[i];
         n.note_id = id;
         n.channel = find_ret.channel;
         n.velocity = find_ret.velocity;

         // NOTE: This must be set at the next level up.  The track
         // itself has no idea what its index is.
         n.track_id = 0;

         // Add a note and remove this NoteId from the active list
         m_note_set.insert(n);
         m_active_notes[id].pop();
      } else {
      // Add a new active event
      NoteInfo info;
      info.channel = ev.Channel();
      info.velocity = ev.NoteVelocity();
      info.pulses = m_event_pulses[i];

      m_active_notes[id].push(info);
      }
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

   m_note_count = static_cast<unsigned int>(m_note_set.size());
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

void MidiTrack::Reset()
{
   m_running_microseconds = 0;
   m_last_event = -1;

   m_notes_remaining = m_note_count;
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
         m_last_event = static_cast<long long>(i);

         if (m_events[i].Type() == MidiEventType_NoteOn &&
            m_events[i].NoteVelocity() > 0) m_notes_remaining--;
      }
      else break;
   }

   return evs;
}
