
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#include "PianoGameError.h"
#include "string_util.h"

using namespace std;

std::wstring PianoGameError::GetErrorDescription() const
{
   switch (m_error)
   {
   case Error_StringSpecified:             return m_optional_string;
   
   case Error_BadPianoType:                return L"Bad piano type specified.";
   case Error_BadGameState:                return L"Internal Error: SFBM entered bad game state!";

   default:                                return WSTRING(L"Unknown SFBM Error Code (" << m_error << L").");
   }
}

