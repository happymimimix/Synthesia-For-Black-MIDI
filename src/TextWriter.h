
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#ifndef __TEXTWRITER_H
#define __TEXTWRITER_H

#ifndef __cdecl
#define __cdecl
#endif

#include <vector>
#include <string>
#include <iomanip>

#include "os.h"
#include "string_util.h"
#include "TrackProperties.h"

class Renderer;

// A nice ostream-like class for drawing OS-specific (or OpenGL) text to the
// screen in varying colors, fonts, and sizes.
//
// MACNOTE: Mac version of text-placement routine inserts a bizarre space
// after each successive << operator.
//
class TextWriter
{
public:
   // Centering only works for single-write lines... in other words, centered
   // lines can only be 1 color.
   TextWriter(int in_x, int in_y, Renderer &in_renderer, bool in_centered = false, int in_size = 12, std::wstring fontname = L"Trebuchet MS");
   ~TextWriter();

   // Skips at least 1 line, or the height of the last write... whichever is greater
   // (so that you can skip down past a multiline write)
   TextWriter& next_line();

   // Allow manipulators
   TextWriter& operator<<(TextWriter& (__cdecl *_Pfn)(TextWriter&))
   {   
      (*_Pfn)(*(TextWriter *)this);
      return (*this);
   }

private:
   TextWriter operator=(const TextWriter&);
   TextWriter(const TextWriter&);

   int get_point_size();

   int point_size;
   int x, y, size, original_x;
   int last_line_height;
   bool centered;
   Renderer renderer;

   friend class Text;
};

// Some colors to choose from, for convenience
const static Color Black       = { 0x00,0x00,0x00, 0xFF };
const static Color Dk_Blue     = { 0xC4,0x00,0x00, 0xFF };
const static Color Dk_Green    = { 0x00,0xC4,0x00, 0xFF };
const static Color Dk_Cyan     = { 0xFF,0x80,0x00, 0xFF };
const static Color Dk_Red      = { 0x00,0x00,0xC4, 0xFF };
const static Color Dk_Purple   = { 0x80,0x00,0x80, 0xFF };
const static Color Brown       = { 0x00,0x40,0x80, 0xFF };
const static Color Gray        = { 0xBB,0xBB,0xBB, 0xFF };
const static Color Dk_Gray     = { 0x55,0x55,0x55, 0xFF };
const static Color Blue        = { 0xFF,0x00,0x00, 0xFF };
const static Color Green       = { 0x00,0xFF,0x00, 0xFF };
const static Color Cyan        = { 0xFF,0xFF,0x00, 0xFF };
const static Color Red         = { 0x00,0x00,0xFF, 0xFF };
const static Color Magenta     = { 0xFF,0x00,0xFF, 0xFF };
const static Color Yellow      = { 0x00,0xFF,0xFF, 0xFF };
const static Color White       = { 0xFF,0xFF,0xFF, 0xFF };
const static Color Orange      = { 0x20,0x80,0xFF, 0xFF };
const static Color Pink        = { 0xA0,0x80,0xFF, 0xFF };
const static Color CheatYellow = { 0x00,0xCC,0xFF, 0xFF };


// A class to use TextWriter, and write to the screen
class Text
{
public:
   Text(std::wstring t,     Color color) : m_color(color), m_text(t)  { }
   Text(int i,              Color color) : m_color(color), m_text(WSTRING(i)) { }
   Text(double d, int prec, Color color) : m_color(color), m_text(WSTRING(std::setprecision(prec) << d)) { }

   TextWriter& operator<<(TextWriter& tw) const;

private:

   // This will return where the text should be drawn on
   // the screen (determined in an OS dependent way) and
   // advance the TextWriter's position by the width and/or
   // height of the text.
   void calculate_position_and_advance_cursor(TextWriter &tw, int *out_x, int *out_y) const;
   
   Color m_color;
   std::wstring m_text;
};

// newline manipulator
TextWriter& newline(TextWriter& tw);

TextWriter& operator<<(TextWriter& tw, const Text& t);
TextWriter& operator<<(TextWriter& tw, const std::wstring& s);
TextWriter& operator<<(TextWriter& tw, const int& i);
TextWriter& operator<<(TextWriter& tw, const unsigned int& i);
TextWriter& operator<<(TextWriter& tw, const long& l);
TextWriter& operator<<(TextWriter& tw, const long long& l);
TextWriter& operator<<(TextWriter& tw, const unsigned long& l);
TextWriter& operator<<(TextWriter& tw, const unsigned long long& l);

#endif
