
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#ifndef __FRAMEDUMP_TILE_H
#define __FRAMEDUMP_TILE_H

#include "GameState.h"
#include "MenuLayout.h"
#include "TrackTile.h"
#include <vector>

#include "libmidi/Midi.h"
#include "libmidi/MidiComm.h"

class Renderer;
class Tga;

const int FramedumpTileWidth = 510;
const int FramedumpTileHeight = 80;

enum TrackTileGraphic;

class FramedumpTile
{
public:
    FramedumpTile(int x, int y, Tga* button_graphics, Tga* frame_graphics, bool enabled);

    void Update(const MouseInfo& translated_mouse);
    void Draw(Renderer& renderer) const;

    int GetX() const { return m_x; }
    int GetY() const { return m_y; }

    bool GetFramedump() const { return m_framedump; }

    const ButtonState WholeTile() const { return whole_tile; }
    const ButtonState ButtonLeft() const { return button_mode_left; }
    const ButtonState ButtonRight() const { return button_mode_right; }

private:
    FramedumpTile operator=(const FramedumpTile&);

    int m_x;
    int m_y;

    bool m_framedump = false;

    Tga* m_button_graphics;
    Tga* m_frame_graphics;

    ButtonState whole_tile;
    ButtonState button_mode_left;
    ButtonState button_mode_right;

    int LookupGraphic(TrackTileGraphic graphic, bool button_hovering) const;
};

#endif
