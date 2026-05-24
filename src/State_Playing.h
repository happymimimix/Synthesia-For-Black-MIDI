// Synthesia
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#ifndef __STATE_PLAYING_H
#define __STATE_PLAYING_H

#include <string>
#include <vector>
#include <array>

#include "SharedState.h"
#include "GameState.h"
#include "KeyboardDisplay.h"

struct TrackProperties;
class Midi;
class MidiCommOut;
class MidiCommIn;

struct ActiveNoteChan {
   Track::TrackColor color;
   unsigned char channel;
};
typedef std::list<ActiveNoteChan> ActiveNoteSetItem;
typedef std::array<ActiveNoteSetItem, 0x100> ActiveNoteSet;
typedef std::list<TranslatedNoteSet::const_iterator> SingleNoteLookupTable;
typedef std::array<SingleNoteLookupTable, 0x100> NoteLookupTable;
typedef std::unordered_map<const TranslatedNote*, SingleNoteLookupTable::const_iterator> LookupTableMap;
typedef std::array<microseconds_t, 0x100> KeyReleaseTime;

class PlayingState : public GameState
{
public:
   PlayingState(const SharedState &state);
   ~PlayingState() { Compatible::ShowMouseCursor(); }

protected:
   virtual void Init();
   virtual void Update();
   virtual void Draw(Renderer &renderer) const;

private:

   int CalcKeyboardHeight() const;
   void SetupNoteState();

   void ResetSong();
   void Play(microseconds_t delta_microseconds);
   void Listen();

   double CalculateScoreMultiplier() const;

   bool m_paused;

   KeyboardDisplay *m_keyboard;
   microseconds_t m_show_duration;
   NoteSetReference m_notes;
   const TranslatedNoteSet* m_ptr_notes;
   NoteSetReference::iterator m_next_note_to_listen;
   NoteLookupTable m_note_lookup;
   LookupTableMap m_note_lookup_map;

   bool m_any_you_play_tracks;
   size_t m_look_ahead_you_play_note_count;

   ActiveNoteSet m_active_notes = {};
   KeyReleaseTime m_release_time = {};

   bool m_first_update;

   SharedState m_state;
   unsigned int m_current_combo;

   unsigned int m_last_delta;
   uint8_t m_delay_idx;
   HANDLE m_framedump_handle;
   void* m_framedump_fb;
};

#endif
