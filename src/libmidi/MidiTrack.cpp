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
   char *buffer = new char[track_length];

   stream.read(buffer, track_length);
   if (stream.fail())
   {
      delete[] buffer;
      throw MidiError(MidiError_TrackTooShort);
   }

   // We have to jump through a couple hoops because istringstream
   // can't handle binary data unless constructed through an std::string. 
   MemoryReadBuffer membuf(buffer, track_length);
   istream event_stream(&membuf);

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

   delete[] buffer;
   return t;
}

struct NoteInfo
{
   unsigned char velocity;
   unsigned char channel;
   unsigned long pulses;
};

void MidiTrack::BuildNoteSet(TranslatedNoteSet* translated_notes, unsigned short pulses_per_quarter_note, unsigned short track_id, Midi* self, microseconds_t(Midi:: *PtrToGetEventPulseInMicroseconds)(unsigned long, unsigned short, size_t&) const)
{
   // Keep a list of all the notes currently "on" (and the pulse that
   // it was started).  On a note_on event, we create an element.  On
   // a note_off event we check that an element exists, make a "Note",
   // and remove the element from the list.  If there is already an
   // element on a note_on we both cap off the previous "Note" and
   // begin a new one.
   //
   // A note_on with velocity 0 is a note_off
   array<queue<NoteInfo>, 0x100> m_active_notes;
   size_t tempo_hint = 0;
   m_note_count = 0;

   for (size_t i = 0; i < m_events.size(); ++i)
   {
      const MidiEvent &ev = m_events[i];
      if (ev.Type() != MidiEventType_NoteOn && ev.Type() != MidiEventType_NoteOff) continue;

      bool on = (ev.Type() == MidiEventType_NoteOn && ev.NoteVelocity() > 0);

      // Close off the last event if there was one.
      if (!on && !m_active_notes[ev.NoteNumber()].empty())
      {
         NoteInfo &find_ret = m_active_notes[ev.NoteNumber()].front();
         TranslatedNote trans = {};

         trans.note_id = ev.NoteNumber();
         trans.track_id = track_id;
         trans.channel = find_ret.channel;
         trans.velocity = find_ret.velocity;
         trans.start = (self->*PtrToGetEventPulseInMicroseconds)(find_ret.pulses, pulses_per_quarter_note, tempo_hint);
         size_t end_hint = tempo_hint; // Make a copy
         trans.end = (self->*PtrToGetEventPulseInMicroseconds)(m_event_pulses[i], pulses_per_quarter_note, end_hint);

         // Add a note and remove this NoteId from the active list
         translated_notes->insert(trans);
         m_active_notes[ev.NoteNumber()].pop();

         m_note_count++;
      } else {
      // Add a new active event
      NoteInfo info;
      info.channel = ev.Channel();
      info.velocity = ev.NoteVelocity();
      info.pulses = m_event_pulses[i];

      m_active_notes[ev.NoteNumber()].push(info);
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
}

void MidiTrack::DiscoverInstrument()
{
   // Default to Program 0 per the MIDI Standard
   m_instrument_id = 0;
   bool instrument_found = false;

   // These are actually 10 and 16 in the MIDI standard.  However, MIDI
   // channels are 1-based facing the user.  They're stored 0-based.
   const static int PercussionChannel1 = 9;

   // Check to see if any/all of the notes
   // in this track use Channel 10.
   bool any_note_uses_percussion = false;
   bool any_note_does_not_use_percussion = false;

   for (size_t i = 0; i < m_events.size(); ++i)
   {
      const MidiEvent &ev = m_events[i];
      if (ev.Type() != MidiEventType_NoteOn) continue;

      if (ev.Channel() == PercussionChannel1) any_note_uses_percussion = true;
      if (ev.Channel() != PercussionChannel1) any_note_does_not_use_percussion = true;
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
      }
      else break;
   }

   return evs;
}
