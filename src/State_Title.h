
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#ifndef __STATE_TITLE_H
#define __STATE_TITLE_H

#include "SharedState.h"
#include "GameState.h"
#include "MenuLayout.h"
#include "libmidi/MidiTypes.h"
#include "DeviceTile.h"
#include "StringTile.h"
#include "FramedumpTile.h"
#include <vector>

class Midi;
class MidiCommOut;

class Tga;

class TitleState : public GameState
{
public:
   // You can pass 0 in for state.midi_out to have the title
   // screen pick a device for you.
   TitleState(const SharedState &state)
      : m_state(state), m_output_tile(0), m_input_tile(0),
        m_file_tile(0), m_framedump_tile(0), m_skip_next_mouse_up(false)
   { }

   ~TitleState();

protected:
   virtual void Init();
   virtual void Update();
   virtual void Draw(Renderer &renderer) const;

private:
   void PlayDevicePreview(microseconds_t delta_microseconds);

   ButtonState m_continue_button;
   ButtonState m_back_button;

   SharedState m_state;

   std::string m_last_input_note_name;
   std::wstring m_tooltip;

   DeviceTile *m_output_tile;
   DeviceTile *m_input_tile;
   StringTile *m_file_tile;
   FramedumpTile *m_framedump_tile;

   bool m_skip_next_mouse_up;
};

#endif
