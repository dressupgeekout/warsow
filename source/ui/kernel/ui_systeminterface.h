/*
 * UI_SystemInterface.h
 *
 *  Created on: 25.6.2011
 *      Author: hc
 *
 * Implements the SystemInterface and FileInterface for libRocket
 */

#ifndef UI_SYSTEMINTERFACE_H_
#define UI_SYSTEMINTERFACE_H_

#include <Rocket/Core/SystemInterface.h>

namespace WSWUI
{

class UI_SystemInterface : public Rocket::Core::SystemInterface
{
public:
	UI_SystemInterface();
	virtual ~UI_SystemInterface();

	//// Implement SystemInterface

	/// Get the number of seconds elapsed since the start of the application
	/// @returns Seconds elapsed
	virtual float GetElapsedTime();

	/// Log the specified message.
	/// @param[in] type Type of log message, ERROR, WARNING, etc.
	/// @param[in] message Message to log.
	/// @return True to continue execution, false to break into the debugger.
	virtual bool LogMessage(Rocket::Core::Log::Type type, const String& message);

	/// Translate the input string into the translated string.
	/// @param[out] translated Translated string ready for display.
	/// @param[in] input String as received from XML.
	/// @return Number of translations that occured.
	virtual int TranslateString(Rocket::Core::String& translated, const Rocket::Core::String& input);
};

}

#endif /* UI_SYSTEMINTERFACE_H_ */
