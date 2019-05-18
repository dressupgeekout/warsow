/*
Copyright (C) 2013 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <Rocket/Core/PropertyParser.h>

class PropertyParserSound : public Rocket::Core::PropertyParser
{
public:
	PropertyParserSound();
	virtual ~PropertyParserSound();

	/// Called to parse a RCSS string declaration.
	/// @param[out] property The property to set the parsed value on.
	/// @param[in] value The raw value defined for this property.
	/// @param[in] parameters The parameters defined for this property; not used for this parser.
	/// @return True if the value was validated successfully, false otherwise.
	virtual bool ParseValue(Rocket::Core::Property& property, 
		const Rocket::Core::String& value, 
		const Rocket::Core::ParameterMap& parameters) const;

	// Destroys the parser.
	void Release();
};
