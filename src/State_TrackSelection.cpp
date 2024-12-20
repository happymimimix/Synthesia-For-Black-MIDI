
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#include "State_TrackSelection.h"

#include "State_Title.h"
#include "State_Playing.h"
#include "MenuLayout.h"
#include "Renderer.h"
#include "Textures.h"

#include "libmidi/Midi.h"
#include "libmidi/MidiUtil.h"
#include "libmidi/MidiComm.h"

TrackSelectionState::TrackSelectionState(const SharedState &state)
   : m_state(state), m_preview_on(false), m_preview_track_id(0),
   m_first_update_after_seek(false),
   m_page_count(0), m_current_page(0), m_tiles_per_page(0)
{ }

void TrackSelectionState::Init()
{
    Compatible::ShowMouseCursor();
   if (m_state.midi_out) m_state.midi_out->Reset();

   Midi &m = *m_state.midi;

   // Prepare a very simple count of the playable tracks first
   int track_count = 0;
   for (size_t i = 0; i < m.Tracks().size(); ++i)
   {
      if (m.Tracks()[i].Notes().size()) track_count++;
   }

   m_back_button = ButtonState(Layout::ScreenMarginX,
      GetStateHeight() - Layout::ScreenMarginY/2 - Layout::ButtonHeight/2,
      Layout::ButtonWidth, Layout::ButtonHeight);

   m_continue_button = ButtonState(GetStateWidth() - Layout::ScreenMarginX - Layout::ButtonWidth,
      GetStateHeight() - Layout::ScreenMarginY/2 - Layout::ButtonHeight/2,
      Layout::ButtonWidth, Layout::ButtonHeight);

   // Determine how many track tiles we can fit
   // horizontally and vertically. Integer division
   // helps us round down here.
   int tiles_across = (GetStateWidth() + Layout::ScreenMarginX) / (TrackTileWidth + Layout::ScreenMarginX);
   tiles_across = std::max(tiles_across, 1);

   int tiles_down = (GetStateHeight() - Layout::ScreenMarginX - Layout::ScreenMarginY * 2) / (TrackTileHeight + Layout::ScreenMarginX);
   tiles_down = std::max(tiles_down, 1);

   // Calculate how many pages of tracks there will be
   m_tiles_per_page = tiles_across * tiles_down;

   m_page_count        = track_count / m_tiles_per_page;
   const int remainder = track_count % m_tiles_per_page;
   if (remainder > 0) m_page_count++;

   // If we have fewer than one row of tracks, just
   // center the tracks we do have
   if (track_count < tiles_across) tiles_across = track_count;

   // Determine how wide that many track tiles will
   // actually be, so we can center the list
   int all_tile_widths = tiles_across * TrackTileWidth + (tiles_across-1) * Layout::ScreenMarginX;
   int global_x_offset = (GetStateWidth() - all_tile_widths) / 2;

   const static int starting_y = 100;

   int tiles_on_this_line = 0;
   int tiles_on_this_page = 0;
   int current_y = starting_y;
   for (size_t i = 0; i < m.Tracks().size(); ++i)
   {
      const MidiTrack &t = m.Tracks()[i];
      if (t.Notes().size() == 0) continue;

      int x = global_x_offset + (TrackTileWidth + Layout::ScreenMarginX)*tiles_on_this_line;
      int y = current_y;

      Track::Mode mode = Track::ModePlayedAutomatically;

      Track::TrackColor color = static_cast<Track::TrackColor>((m_track_tiles.size()) % Track::UserSelectableColorCount);

      // If we came back here from StatePlaying, reload all our preferences
      if (m_state.track_properties.size() > i)
      {
         color = m_state.track_properties[i].color;
         mode = m_state.track_properties[i].mode;
      }

      TrackTile tile(x, y, i, color, mode);

      m_track_tiles.push_back(tile);


      tiles_on_this_line++;
      tiles_on_this_line %= tiles_across;
      if (!tiles_on_this_line)
      {
         current_y += TrackTileHeight + Layout::ScreenMarginX;
      }

      tiles_on_this_page++;
      tiles_on_this_page %= m_tiles_per_page;
      if (!tiles_on_this_page)
      {
         current_y = starting_y;
         tiles_on_this_line = 0;
      }
   }
}

std::vector<Track::Properties> TrackSelectionState::BuildTrackProperties() const
{
   std::vector<Track::Properties> props;
   for (size_t i = 0; i < m_state.midi->Tracks().size(); ++i)
   {
      props.push_back(Track::Properties());
   }

   // Populate it with the tracks that have notes
   for (std::vector<TrackTile>::const_iterator i = m_track_tiles.begin(); i != m_track_tiles.end(); ++i)
   {
      props[i->GetTrackId()].color = i->GetColor();
      props[i->GetTrackId()].mode = i->GetMode();
   }

   return props;
}

void TrackSelectionState::Update()
{
   m_continue_button.Update(MouseInfo(Mouse()));
   m_back_button.Update(MouseInfo(Mouse()));

   if (IsKeyPressed(KeyEscape) || m_back_button.hit)
   {
      if (m_state.midi_out) m_state.midi_out->Reset();
      m_state.track_properties = BuildTrackProperties();
      ChangeState(new TitleState(m_state));
      return;
   }

   if (IsKeyPressed(KeyEnter) || m_continue_button.hit)
   {

      if (m_state.midi_out) m_state.midi_out->Reset();
      m_state.track_properties = BuildTrackProperties();
      ChangeState(new PlayingState(m_state));

      return;
   }

   if (IsKeyPressed(KeyDown) || IsKeyPressed(KeyRight))
   {
      m_current_page++;
      if (m_current_page == m_page_count) m_current_page = 0;
   }

   if (IsKeyPressed(KeyUp) || IsKeyPressed(KeyLeft))
   {
      m_current_page--;
      if (m_current_page < 0) m_current_page += m_page_count;
   }

   m_tooltip = L"";

   if (m_back_button.hovering) m_tooltip = L"Click to return to the title screen.";
   if (m_continue_button.hovering) m_tooltip = L"Click to begin playing with these settings.";

   // Our delta milliseconds on the first frame after we seek down to the
   // first note is extra long because the seek takes a while.  By skipping
   // the "Play" that update, we don't have an artificially fast-forwarded
   // start.
   if (!m_first_update_after_seek)
   {
      PlayTrackPreview(static_cast<microseconds_t>(GetDeltaMilliseconds()) * 1000);
   }
   m_first_update_after_seek = false;

   // Do hit testing on each tile button on this page
   size_t start = m_current_page * m_tiles_per_page;
   size_t end = std::min( static_cast<size_t>((m_current_page+1) * m_tiles_per_page), m_track_tiles.size() );
   for (size_t i = start; i < end; ++i)
   {
      TrackTile &t = m_track_tiles[i];

      MouseInfo mouse = MouseInfo(Mouse());
      mouse.x -= t.GetX();
      mouse.y -= t.GetY();

      t.Update(mouse);

      if (t.ButtonLeft().hovering || t.ButtonRight().hovering)
      {
         switch (t.GetMode())
         {
         case Track::ModeNotPlayed: m_tooltip = L"Track won't be played or shown during the game."; break;
         case Track::ModePlayedAutomatically: m_tooltip = L"Track will be played automatically by the game."; break;
         case Track::ModePlayedButHidden: m_tooltip = L"Track will be played automatically by the game, but also hidden from view."; break;
         case Track::ModeYouPlay: m_tooltip = L"'You Play' means you want to play this track yourself."; break;
         }
      }

      if (t.ButtonPreview().hovering)
      {
         if (t.IsPreviewOn()) m_tooltip = L"Turn track preview off.";
         else m_tooltip = L"Preview how this track sounds.";
      }

      if (t.ButtonColor().hovering) m_tooltip = L"Pick a color for this track's notes.";

      if (t.HitPreviewButton())
      {
         if (m_state.midi_out) m_state.midi_out->Reset();

         if (t.IsPreviewOn())
         {
            // Turn off any other preview modes
            for (size_t j = 0; j < m_track_tiles.size(); ++j)
            {
               if (i == j) continue;
               m_track_tiles[j].TurnOffPreview();
            }

            const microseconds_t PreviewLeadIn  = 25000;
            const microseconds_t PreviewLeadOut = 25000;

            m_preview_on = true;
            m_preview_track_id = t.GetTrackId();
            m_state.midi->Reset(PreviewLeadIn, PreviewLeadOut);
            PlayTrackPreview(0);

            // Find the first note in this track so we can skip right to the good part.
            microseconds_t additional_time = -PreviewLeadIn;
            const MidiTrack &track = m_state.midi->Tracks()[m_preview_track_id];
            for (size_t i = 0; i < track.Events().size(); ++i)
            {
               const MidiEvent &ev = track.Events()[i];
               if (ev.Type() == MidiEventType_NoteOn && ev.NoteVelocity() > 0)
               {
                  additional_time += track.EventUsecs()[i] - m_state.midi->GetDeadAirStartOffsetMicroseconds() - 1;
                  break;
               }
            }

            PlayTrackPreview(additional_time);
            m_first_update_after_seek = true;
         }
         else
         {
            m_preview_on = false;
         }
      }
   }

   


}

void TrackSelectionState::PlayTrackPreview(microseconds_t delta_microseconds)
{
   if (!m_preview_on) return;

   MidiEventListWithTrackId evs = m_state.midi->Update(delta_microseconds);

   for (MidiEventListWithTrackId::const_iterator i = evs.begin(); i != evs.end(); ++i)
   {
      const MidiEvent &ev = i->second;
      if (i->first != m_preview_track_id) continue;

      if (m_state.midi_out) m_state.midi_out->Write(ev);
   }
}

void TrackSelectionState::Draw(Renderer &renderer) const
{
   Layout::DrawTitle(renderer, L"Choose Tracks To Play");

   Layout::DrawHorizontalRule(renderer, GetStateWidth(), Layout::ScreenMarginY);
   Layout::DrawHorizontalRule(renderer, GetStateWidth(), GetStateHeight() - Layout::ScreenMarginY);

   Layout::DrawButton(renderer, m_continue_button, GetTexture(ButtonPlaySong));
   Layout::DrawButton(renderer, m_back_button, GetTexture(ButtonBackToTitle));

   // Write our page count on the screen
   TextWriter pagination(GetStateWidth()/2, GetStateHeight() - Layout::SmallFontSize - 30, renderer, true, Layout::ButtonFontSize);
   pagination << Text(WSTRING(L"Page " << (m_current_page+1) << L" of " << m_page_count << L" (arrow keys change page)"), Gray);

   TextWriter tooltip(GetStateWidth()/2, GetStateHeight() - Layout::SmallFontSize - 54, renderer, true, Layout::ButtonFontSize);
   tooltip << m_tooltip;

   Tga *buttons = GetTexture(InterfaceButtons);
   Tga *box = GetTexture(TrackPanel);

   // Draw each track tile on the current page
   size_t start = m_current_page * m_tiles_per_page;
   size_t end = std::min( static_cast<size_t>((m_current_page+1) * m_tiles_per_page), m_track_tiles.size() );
   for (size_t i = start; i < end; ++i)
   {
      m_track_tiles[i].Draw(renderer, m_state.midi, buttons, box);
   }
}
