// Synthesia
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#ifndef __STATE_PLAYING_H
#define __STATE_PLAYING_H

#include <string>
#include <vector>
#include <unordered_set>

#include "SharedState.h"
#include "GameState.h"
#include "KeyboardDisplay.h"

struct TrackProperties;
class Midi;
class MidiCommOut;
class MidiCommIn;

struct ActiveNote
{
   bool operator==(const ActiveNote &n) const
   {
      return note_id == n.note_id && channel == n.channel;
   }

   NoteId note_id;
   unsigned char channel;
   unsigned char velocity;
};
struct ActiveNoteHash
{
   size_t operator()(const ActiveNote &n) const
   {
      return std::hash<unsigned int>()((static_cast<unsigned int>(n.note_id)<<24)|(static_cast<unsigned int>(n.channel)<<16));
   }
};
typedef std::unordered_set<ActiveNote, ActiveNoteHash> ActiveNoteSet;

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
   TranslatedNoteSet m_notes;

   bool m_any_you_play_tracks;
   size_t m_look_ahead_you_play_note_count;

   ActiveNoteSet m_active_notes;

   bool m_first_update;

   SharedState m_state;
   int m_current_combo;

   unsigned long m_last_delta;
   uint8_t m_delay_idx;
   HANDLE m_framedump_handle;
   void* m_framedump_fb;
};

#endif
