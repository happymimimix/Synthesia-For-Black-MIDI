// Synthesia
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#ifndef __MIDI_NOTE_H
#define __MIDI_NOTE_H

#include <set>
#include "MidiTypes.h"

// Range of all MIDI notes (0-255 for 256-key support)
typedef unsigned char NoteId;

enum NoteState : unsigned char
{
   AutoPlayed,
   UserPlayable,
   UserHit,
   UserMissed
};

template <class T>
struct GenericNote
{
   bool operator()(const GenericNote<T> &lhs, const GenericNote<T> &rhs) const
   {
      if (lhs.start < rhs.start) return true;
      if (lhs.start > rhs.start) return false;

      if (lhs.end < rhs.end) return true;
      if (lhs.end > rhs.end) return false;

      if (lhs.note_id < rhs.note_id) return true;
      if (lhs.note_id > rhs.note_id) return false;

      if (lhs.track_id < rhs.track_id) return true;
      if (lhs.track_id > rhs.track_id) return false;

      return false;
   }

   T start;
   T end;
   NoteId note_id;
   unsigned short track_id;

   // We have to drag a little extra info around so we can
   // play the user's input correctly
   unsigned char channel;
   unsigned char velocity;

   NoteState state;
};

// Note keeps the internal pulses found in the MIDI file which are
// independent of tempo or playback speed.  TranslatedNote contains
// the exact (translated) microsecond that notes start and stop on
// based on a given playback speed, after dereferencing tempo changes.
typedef GenericNote<unsigned long> Note;
typedef GenericNote<microseconds_t> TranslatedNote;

typedef std::set<Note, Note> NoteSet;
typedef std::set<TranslatedNote, TranslatedNote> TranslatedNoteSet;

#endif
