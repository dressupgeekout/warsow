/*
 * UI_Main.cpp
 *
 *  Created on: 25.6.2011
 *      Author: hc
 */

#include "ui_precompiled.h"
#include "kernel/ui_common.h"
#include "kernel/ui_main.h"
#include "widgets/ui_widgets.h"

#include "as/asui.h"
#include "as/asmodule.h"

#include "../gameshared/q_keycodes.h"
#include "../gameshared/q_comref.h"

#include "datasources/ui_demos_datasource.h"
#include "datasources/ui_huds_datasource.h"
#include "datasources/ui_video_datasource.h"
#include "datasources/ui_gametypes_datasource.h"
#include "datasources/ui_maps_datasource.h"
#include "datasources/ui_mods_datasource.h"
#include "datasources/ui_models_datasource.h"
#include "datasources/ui_profiles_datasource.h"
#include "datasources/ui_serverbrowser_datasource.h"
#include "datasources/ui_tvchannels_datasource.h"
#include "datasources/ui_ircchannels_datasource.h"
#include "datasources/ui_gameajax_datasource.h"

#include "formatters/ui_levelshot_formatter.h"
#include "formatters/ui_datetime_formatter.h"
#include "formatters/ui_duration_formatter.h"
#include "formatters/ui_filetype_formatter.h"
#include "formatters/ui_colorcode_formatter.h"
#include "formatters/ui_empty_formatter.h"
#include "formatters/ui_serverflags_formatter.h"

namespace WSWUI
{
UI_Main *UI_Main::self = 0;

const std::string UI_Main::ui_index( "index.rml" );
const std::string UI_Main::ui_connectscreen( "connectscreen.rml" );

UI_Main::UI_Main( int vidWidth, int vidHeight, float pixelRatio,
	int protocol, const char *demoExtension, const char *basePath )
	// pointers to zero
	: asmodule(nullptr), rocketModule(nullptr),
	levelshot_fmt(0), datetime_fmt(0), duration_fmt(0), filetype_fmt(0), colorcode_fmt(0), 
	empty_fmt(0), serverflags_fmt(0),
	serverBrowser(0), gameTypes(0), maps(0), vidProfiles(0), huds(0), videoModes(0), 
	demos(0), mods(0), 
	playerModels(0), tvchannels(0), ircchannels(0), gameajax(0),

	// other members
	quickMenuURL(""),
	mousex(0), mousey(0), gameProtocol(protocol),
	menuVisible(false), forceMenu(false), showNavigationStack(false),
	serverName(""), rejectMessage(""), demoExtension(demoExtension),
	connectCount(0), invalidateAjaxCache(false),
	ui_basepath(nullptr), ui_cursor(nullptr), ui_developer(nullptr), ui_preload(nullptr)
{
	// instance
	self = this;

	Vector4Set( colorWhite, 1, 1, 1, 1 );
	ui_basepath = trap::Cvar_Get( "ui_basepath", basePath, CVAR_ARCHIVE );
	ui_cursor = trap::Cvar_Get( "ui_cursor", "cursors/default.rml", CVAR_DEVELOPER );
	ui_developer = trap::Cvar_Get( "developer", "0", 0 );
	ui_preload = trap::Cvar_Get( "ui_preload", "1", CVAR_ARCHIVE );

	// make sure the UI isn't too small
	int minHeight = 600.0f * pixelRatio;
	if( vidHeight < minHeight )
		pixelRatio *= ( float )vidHeight / ( float )minHeight;

	// temp fix for missing background on start.. populate refreshState with some nice values
	refreshState.clientState = CA_UNINITIALIZED;
	refreshState.width = vidWidth;
	refreshState.height = vidHeight;
	refreshState.pixelRatio = pixelRatio;
	refreshState.drawBackground = true;

	demoInfo.setPlaying( false );

	if( !initRocket() )
		throw std::runtime_error( "UI: Failed to initialize libRocket" );

	registerRocketCustoms();

	createDataSources();
	createFormatters();
	createStack( UI_CONTEXT_MAIN );
	createStack( UI_CONTEXT_QUICK );

	streamCache = __new__( StreamCache )();

	streamCache->Init();

	if( !initAS() )
		throw std::runtime_error( "UI: Failed to initialize AngelScript" );

	// this after instantiation
	ASUI::BindGlobals( self->getAS() );

	// load cursor document
	loadCursor();

	// this has to be called after AS API is fully loaded
	preloadUI();

	// commands
	trap::Cmd_AddCommand( "ui_reload", ReloadUI_Cmd_f );
	trap::Cmd_AddCommand( "ui_dumpapi", DumpAPI_f );
	trap::Cmd_AddCommand( "ui_printdocs", PrintDocuments_Cmd );

	trap::Cmd_AddCommand( "menu_force", M_Menu_Force_f );
	trap::Cmd_AddCommand( "menu_open", M_Menu_Open_f );
	trap::Cmd_AddCommand( "menu_modal", M_Menu_Modal_f );
	trap::Cmd_AddCommand( "menu_close", M_Menu_Close_f );
	trap::Cmd_AddCommand( "menu_quick", M_Menu_Quick_f );

	trap::Cmd_AddCommand( "menu_tvchannel_add", &M_Menu_AddTVChannel_f );
	trap::Cmd_AddCommand( "menu_tvchannel_remove", &M_Menu_RemoveTVChannel_f );
}

UI_Main::~UI_Main()
{
	// remove commands
	trap::Cmd_RemoveCommand( "ui_reload" );
	trap::Cmd_RemoveCommand( "ui_dumpapi" );
	trap::Cmd_RemoveCommand( "ui_printdocs" );

	trap::Cmd_RemoveCommand( "menu_tvchannel_add" );
	trap::Cmd_RemoveCommand( "menu_tvchannel_remove" );

	trap::Cmd_RemoveCommand( "menu_force" );
	trap::Cmd_RemoveCommand( "menu_open" );
	trap::Cmd_RemoveCommand( "menu_modal" );
	trap::Cmd_RemoveCommand( "menu_close" );

	unregisterRocketCustoms();

	// shutdown AS before rocket, thus script objects get a chance to release
	// their references to rocket elements
	shutdownAS();

	shutdownRocket();

	streamCache->Shutdown();

	self = 0;
}

//==========

bool UI_Main::initAS( void )
{
	asmodule = ASUI::GetASModule( this );
	if( !asmodule->Init() )
		return false;

	// and now our API
	ASUI::BindAPI( asmodule );

	return true;
}

void UI_Main::shutdownAS( void )
{
	ASUI::BindShutdown( asmodule );
	asmodule->Shutdown();
	asmodule = NULL;
}


void UI_Main::preloadUI( void )
{
	int i;
	NavigationStack *navigator;

	for( i = 0; i < UI_NUM_CONTEXTS; i++ ) {
		UI_Navigation &navigation = navigations[i];
		navigator = navigation.front();

		while( !navigation.empty() ) {
			NavigationStack *stack = navigation.front();
			navigation.pop_front();

			// clear the navigation stack
			stack->popAllDocuments();
			if( stack != navigator ) {
				__delete__( stack );
			}
		}

		navigation.push_front( navigator );

		navigator->setDefaultPath( ui_basepath->string );
	}

	// load translation strings

	trap::L10n_ClearDomain();

	// load base UI strings: l10n/ui
	trap::L10n_LoadLangPOFile( "l10n/ui" );
	
	// load strings provided by the theme: e.g. ui/l10n/porkui

	// initialize with default document

	navigator = navigations[UI_CONTEXT_MAIN].front();

	String l10nLocalPath( navigator->getDefaultPath().c_str() );
	l10nLocalPath += "l10n";
	l10nLocalPath.Erase( 0, 1 );
	trap::L10n_LoadLangPOFile( l10nLocalPath.CString() );

	// postpone displaying the document until the first valid refresh state
	navigator->pushDocument( ui_index, false, false );
	showNavigationStack = navigator->hasDocuments();

	// initial cursor setup
	if( trap::IN_SupportedDevices() & IN_DEVICE_TOUCHSCREEN ) {
		mouseMove( UI_CONTEXT_MAIN, 0, 0, true, false );
	} else {
		mouseMove( UI_CONTEXT_MAIN, refreshState.width >> 1, refreshState.height >> 1, true, true );
	}

	if( !quickMenuURL.Empty() ) {
		navigator = navigations[UI_CONTEXT_QUICK].front();
		navigator->pushDocument( quickMenuURL.CString(), false );
	}

	rocketModule->update();
}

void UI_Main::reloadUI( void )
{
	int i;

	for( i = 0; i < UI_NUM_CONTEXTS; i++ ) {
		UI_Navigation &navigation = navigations[i];
		NavigationStack *navigator = navigation.front();

		while( !navigation.empty() ) {
			NavigationStack *stack = navigation.front();
			navigation.pop_front();

			// clear the navigation stack
			stack->popAllDocuments();
			stack->getCache()->clearCaches();
			if( stack != navigator ) {
				__delete__( stack );
			}
		}

		navigation.push_front( navigator );
	}

	if( serverBrowser ) {
		serverBrowser->stopUpdate();
	}
	if( demos ) {
		demos->Reset();
	}

	destroyDataSources();

	createDataSources();

	preloadUI();

	showUI( true );
}

void UI_Main::loadCursor( void )
{
	assert( rocketModule != NULL );

	// setup cursor
	std::string basecursor( ui_basepath->string );

	basecursor += "/";
	basecursor += ui_cursor->string;

	rocketModule->loadCursor( UI_CONTEXT_MAIN, basecursor.c_str() );
	//rocketModule->loadCursor( UI_CONTEXT_QUICK, basecursor.c_str() );
}

bool UI_Main::initRocket( void )
{
	// this may throw runtime_error.. ok pass it back up
	rocketModule = __new__(RocketModule)( refreshState.width, refreshState.height, refreshState.pixelRatio );
	return true;
}

void UI_Main::registerRocketCustoms( void )
{
	rocketModule->registerCustoms();
}

void UI_Main::unregisterRocketCustoms( void )
{
	rocketModule->unregisterCustoms();
}

void UI_Main::shutdownRocket( void )
{
	int i;

	for( i = 0; i < UI_NUM_CONTEXTS; i++ ) {
		UI_Navigation &navigation = navigations[i];
		for( UI_Navigation::iterator it = navigation.begin(); it != navigation.end(); ++it ) {
			// clear the navigation stack
			(*it)->popAllDocuments();
			(*it)->getCache()->clearCaches();
		}
	}

	// forget about all previously registed shaders
	rocketModule->clearShaderCache();

	destroyDataSources();
	destroyFormatters();

	for( i = 0; i < UI_NUM_CONTEXTS; i++ ) {
		UI_Navigation &navigation = navigations[i];
		while( !navigation.empty() ) {
			NavigationStack *stack = navigation.front();
			__SAFE_DELETE_NULLIFY( stack );
			navigation.pop_front();
		}
	}

	__SAFE_DELETE_NULLIFY( rocketModule );
}

void UI_Main::clearShaderCache( void )
{
	if( rocketModule != NULL ) {
		rocketModule->clearShaderCache();
	}
	this->connectCount++;
}

void UI_Main::touchAllCachedShaders( void )
{
	int i;
	if( rocketModule != NULL ) {
		rocketModule->touchAllCachedShaders();
	}

	for( i = 0; i < UI_NUM_CONTEXTS; i++ ) {
		UI_Navigation &navigation = navigations[i];
		for( UI_Navigation::iterator it = navigation.begin(); it != navigation.end(); ++it ) {
			(*it)->invalidateAssets();
		}
	}
}

void UI_Main::flushAjaxCache( void )
{
	this->invalidateAjaxCache = true;
}

NavigationStack *UI_Main::createStack( int contextId )
{
	NavigationStack *stack = __new__( NavigationStack )( contextId );
	if( !stack ) {
		return NULL;
	}
	if( contextId < 0 || contextId >= UI_NUM_CONTEXTS ) {
		return NULL;
	}
	stack->setDefaultPath( ui_basepath->string );
	navigations[contextId].push_back( stack );
	return stack;
}

void UI_Main::createDataSources( void )
{
	serverBrowser = __new__( ServerBrowserDataSource )();
	gameTypes = __new__(GameTypesDataSource)();
	maps = __new__(MapsDataSource)();
	huds = __new__( HudsDataSource )();
	videoModes = __new__( VideoDataSource )();
	demos = __new__( DemosDataSource )( demoExtension );
	mods = __new__( ModsDataSource )();
	tvchannels = __new__( TVChannelsDataSource )();
	ircchannels = __new__( IrcChannelsDataSource )();
	gameajax = __new__( GameAjaxDataSource )();
	playerModels = __new__( ModelsDataSource )();
	vidProfiles = __new__( ProfilesDataSource )();
}

void UI_Main::destroyDataSources( void )
{
	__SAFE_DELETE_NULLIFY( serverBrowser );
	__SAFE_DELETE_NULLIFY( gameTypes );
	__SAFE_DELETE_NULLIFY( maps );
	__SAFE_DELETE_NULLIFY( huds );
	__SAFE_DELETE_NULLIFY( videoModes );
	__SAFE_DELETE_NULLIFY( demos );
	__SAFE_DELETE_NULLIFY( mods );
	__SAFE_DELETE_NULLIFY( tvchannels );
	__SAFE_DELETE_NULLIFY( ircchannels );
	__SAFE_DELETE_NULLIFY( gameajax );
	__SAFE_DELETE_NULLIFY( playerModels );
	__SAFE_DELETE_NULLIFY( vidProfiles );
}

void UI_Main::createFormatters( void )
{
	levelshot_fmt = __new__(LevelShotFormatter)();
	datetime_fmt = __new__( DatetimeFormatter )();
	duration_fmt = __new__( DurationFormatter )();
	filetype_fmt = __new__( FiletypeFormatter )();
	colorcode_fmt = __new__( ColorCodeFormatter )();
	empty_fmt = __new__( EmptyFormatter )();
	serverflags_fmt = __new__( ServerFlagsFormatter )();
}

void UI_Main::destroyFormatters( void )
{
	__SAFE_DELETE_NULLIFY( levelshot_fmt );
	__SAFE_DELETE_NULLIFY( datetime_fmt );
	__SAFE_DELETE_NULLIFY( duration_fmt );
	__SAFE_DELETE_NULLIFY( filetype_fmt );
	__SAFE_DELETE_NULLIFY( colorcode_fmt );
	__SAFE_DELETE_NULLIFY( empty_fmt );
	__SAFE_DELETE_NULLIFY( serverflags_fmt );
}

//==============================================

void UI_Main::forceUI( bool force )
{
	forceMenu = force;
}

void UI_Main::showUI( bool show )
{
	// only disable menu if not forced to display it
	if( !show && forceMenu ) {
		return;
	}

	menuVisible = show;
	trap::CL_SetKeyDest( show ? key_menu : key_game );

	if( !show ) {
		cancelTouches( UI_CONTEXT_MAIN );

		UI_Navigation &navigation = navigations[UI_CONTEXT_MAIN];
		NavigationStack *navigator = navigation.front();
		for( UI_Navigation::iterator it = navigation.begin(); it != navigation.end(); ++it ) {
			NavigationStack *stack = *it;
			if( stack->isTopModal() ) {
				stack->popDocument();
			}
			if( stack == navigator ) {
				stack->popAllDocuments();
			}
		}

		rocketModule->hideCursor( UI_CONTEXT_MAIN, RocketModule::HIDECURSOR_REFRESH, 0 );
	}
}

void UI_Main::showQuickMenu( bool show )
{
	quickMenuVisible = show;

	if( !show ) {
		cancelTouches( UI_CONTEXT_QUICK );
	}
}

bool UI_Main::haveQuickMenu( void )
{
	NavigationStack *nav = self->navigations[UI_CONTEXT_QUICK].front();
	if( !nav )
		return false;
	return nav->hasDocuments();
}

void UI_Main::drawConnectScreen( const char *serverName, const char *rejectMessage, 
	int downloadType, const char *downloadFilename, float downloadPercent, int downloadSpeed, 
	int connectCount, bool backGround )
{
	DownloadInfo dlinfo( downloadFilename, downloadType );

	dlinfo.setPercent( downloadPercent );
	dlinfo.setSpeed( downloadSpeed );

	this->serverName = serverName ? serverName : "";
	this->rejectMessage = rejectMessage ? rejectMessage : "";
	this->downloadInfo = dlinfo;

	UI_Navigation &navigation = navigations[UI_CONTEXT_MAIN];
	NavigationStack *navigator = navigation.front();
	navigator->pushDocument( ui_connectscreen, false, true );

	forceUI( true );
	showUI( true );
}

int UI_Main::getGameProtocol( void ) 
{
	return self != nullptr ? self->gameProtocol : 0;
}

void UI_Main::customRender( void )
{
	// NO-OP for now
}

bool UI_Main::preloadEnabled( void )
{
#if defined(NDEBUG) && !defined( __ANDROID__ )
	return ( self != nullptr && self->ui_preload && self->ui_preload->integer != 0 );
#else
	return false;
#endif
}

void UI_Main::gamepadStickCursorMove( float frameTime )
{
	const float threshold = 7849.0f / 32767.0f; // Xbox controller left stick dead zone.

	vec4_t sticks;
	trap::IN_GetThumbsticks( sticks );

	float sx = sticks[0] * ( ( float )( fabsf( sticks[0] ) > threshold ) );
	sx += sticks[2] * ( ( float )( fabsf( sticks[2] ) > threshold ) );
	clamp( sx, -1.0f, 1.0f );
	float sy = sticks[1] * ( ( float )( fabsf( sticks[1] ) > threshold ) );
	sy += sticks[3] * ( ( float )( fabsf( sticks[3] ) > threshold ) );
	clamp( sy, -1.0f, 1.0f );

	static float x, y;
	if( !sx && !sy ) {
		x = 0.0f;
		y = 0.0f;
		return;
	}

	float speed = ( 600.0f * 1.5f ) * refreshState.pixelRatio * frameTime;
	x += sx * sx * sx * speed;
	y += sy * sy * sy * speed;

	int mx = ( int )x, my = ( int )y;
	x -= ( float )mx;
	y -= ( float )my;
	mouseMove( UI_CONTEXT_MAIN, mx, my, false, true );
}

void UI_Main::gamepadDpadCursorMove( float frameTime )
{
	static float holdTime;
	static float x, y;

	int dx = trap::Key_IsDown( K_DPAD_RIGHT ) - trap::Key_IsDown( K_DPAD_LEFT );
	int dy = trap::Key_IsDown( K_DPAD_DOWN ) - trap::Key_IsDown( K_DPAD_UP );
	if( !dx && !dy ) {
		holdTime = x = y = 0.0f;
		return;
	}

	// Goes from half minimum screen height to double minimum screen height.
	float speed = ( 600.0f * 0.5f ) + bound( 0.0f, holdTime - 0.25f, 1.5f ) * 600.0f;
	if( dx && dy ) {
		speed *= 0.707106f;
	}
	speed *= refreshState.pixelRatio * frameTime;

	if( dx ) {
		x += ( ( dx < 0 ) ? -1.0f : 1.0f ) * speed;
	} else {
		x = 0.0f;
	}

	if( dy ) {
		y += ( ( dy < 0 ) ? -1.0f : 1.0f ) * speed;
	} else {
		y = 0.0f;
	}

	holdTime += frameTime;

	int mx = ( int )x, my = ( int )y;
	x -= ( float )mx;
	y -= ( float )my;
	mouseMove( UI_CONTEXT_MAIN, mx, my, false, true );
}

void UI_Main::gamepadCursorMove( void )
{
	unsigned int time = trap::Milliseconds();

	static unsigned int lastTime;
	if( !lastTime ) {
		lastTime = time;
		return;
	}

	float frameTime = ( time - lastTime ) * 0.001f;
	lastTime = time;

	if( !frameTime ) {
		return;
	}

	clamp_high( frameTime, 0.1f );

	gamepadStickCursorMove( frameTime );
	gamepadDpadCursorMove( frameTime );
}

//===========================================

// CALLBACKS FROM MAIN PROGRAM

void UI_Main::mouseMove( int contextId, int x, int y, bool absolute, bool showCursor )
{
	// change the delta to window coordinates.
	if( absolute ) {
		mousex = x;
		mousey = y;
	} else {
		mousex += x;
		mousey += y;
	}

	if( mousex < 0 )
		mousex = 0;
	else if( mousex > refreshState.width )
		mousex = refreshState.width;
	if( mousey < 0 )
		mousey = 0;
	else if( mousey > refreshState.height )
		mousey = refreshState.height;

	rocketModule->mouseMove( contextId, mousex, mousey );

	if( showCursor ) {
		rocketModule->hideCursor( contextId, 0, RocketModule::HIDECURSOR_INPUT );
	} else {
		rocketModule->hideCursor( contextId, RocketModule::HIDECURSOR_INPUT, 0 );
	}
}

void UI_Main::textInput( int contextId, wchar_t c )
{
	rocketModule->textInput( contextId, c );
}

void UI_Main::keyEvent( int contextId, int key, bool pressed )
{
	// TODO: handle some special keys here?
	rocketModule->keyEvent( contextId, key, pressed );
}

bool UI_Main::touchEvent( int contextId, int id, touchevent_t type, int x, int y )
{
	return rocketModule->touchEvent( contextId, id, type, x, y );
}

bool UI_Main::isTouchDown( int contextId, int id )
{
	return rocketModule->isTouchDown( contextId, id );
}

void UI_Main::cancelTouches( int contextId )
{
	rocketModule->cancelTouches( contextId );
}

void UI_Main::addToServerList(const char *adr, const char *info)
{
	if( !serverBrowser )
		return;

	serverBrowser->addToServerList( adr, info );
}

void UI_Main::forceMenuOff( void )
{
	forceUI( false );
	showUI( false );
}

bool UI_Main::debugOn( void )
{
	return ui_developer->integer != 0;
}

void UI_Main::refreshScreen( unsigned int time, int clientState, int serverState, 
	bool demoPlaying, const char *demoName, bool demoPaused, unsigned int demoTime, 
	bool backGround, bool showCursor )
{
	int i;
	UI_Navigation::iterator it, it_next;

	refreshState.time = time;
	refreshState.clientState = clientState;
	refreshState.serverState = serverState;
	refreshState.drawBackground = backGround;

	if( demoPlaying && !demoInfo.getPlaying() ) {
		demoInfo.setName( demoName );
	}
	demoInfo.setTime( demoTime );
	demoInfo.setPaused( demoPaused );
	demoInfo.setPlaying( demoPlaying );

	// postponed showing of the stacked document, we need to set the refresh state first
	if( showNavigationStack ) {
		UI_Navigation &navigation = navigations[UI_CONTEXT_MAIN];
		NavigationStack *navigator = navigation.front();
		navigator->showStack( true );
		showNavigationStack = false;
	}

	// update necessary modules
	if( serverBrowser )
		serverBrowser->updateFrame();
	if( demos )
		demos->UpdateFrame();
	if( ircchannels )
		ircchannels->UpdateFrame();

	if( clientState == CA_ACTIVE && invalidateAjaxCache ) {
		gameajax->FlushCache();
		invalidateAjaxCache = false;
	}

	// TODO: handle the intervalled functions in AS somehow,
	// taking care that they are not called when menu is hidden.
	// i may need to make the interface public..
	BindFrame( asmodule );

	// run incremental garbage collection
	asmodule->garbageCollectOneStep();

	for( i = 0; i < UI_NUM_CONTEXTS; i++ ) {
		UI_Navigation &navigation = navigations[i];
		NavigationStack *navigator = navigation.front();

		// free empty navigation stacks
		for( it = navigation.begin(); it != navigation.end(); it = it_next ) {
			it_next = it;
			it_next++;

			NavigationStack *stack = *it;
			if( stack != navigator && stack->empty() ) {
				__delete__( stack );
				navigation.erase( it );
			}
		}
	}

	// handle main menu context
	if( menuVisible ) {
		NavigationStack *navigator = navigations[UI_CONTEXT_MAIN].front();
		if( !navigator->hasDocuments() ) {
			// no documents on stack, release the key dest
			showUI( false );
		}
		else {
			if( showCursor ) { 
				rocketModule->hideCursor( UI_CONTEXT_MAIN, 0, RocketModule::HIDECURSOR_REFRESH );
				gamepadCursorMove();
			}
			else {
				rocketModule->hideCursor( UI_CONTEXT_MAIN, RocketModule::HIDECURSOR_REFRESH, 0 );
			}
		}
	}

	// rocket update+render
	rocketModule->update();

	if( quickMenuVisible ) {
		rocketModule->render( UI_CONTEXT_QUICK );
	}
	if( menuVisible ) {
		rocketModule->render( UI_CONTEXT_MAIN );
	}

	// mark the top stack document as viwed for history tracking
	for( i = 0; i < UI_NUM_CONTEXTS; i++ ) {
		UI_Navigation &navigation = navigations[i];
		for( it = navigation.begin(); it != navigation.end(); ++it ) {
			(*it)->markTopAsViewed();
		}
	}

	// stuff we need to render without using rocket
	customRender();
}

//==================================

UI_Main *UI_Main::Instance( int vidWidth, int vidHeight, float pixelRatio,
	int protocol, const char *demoExtension, const char *basePath )
{
	if( !self ) {
		self = __new__( UI_Main )( vidWidth, vidHeight, pixelRatio,
			protocol, demoExtension, basePath );
	}
	return self;
}

UI_Main *UI_Main::Get( void )
{
	return self;
}

void UI_Main::Destroy( void )
{
	if( self ) {
		__delete__( self );
		self = NULL;
	}
}

//==================================

void UI_Main::ReloadUI_Cmd_f( void )
{
	if( !self )
		return;

	self->reloadUI();
}

void UI_Main::DumpAPI_f( void )
{
	if( !self || !self->asmodule )
		return;

	self->asmodule->dumpAPI( va( "AS_API/v%.g-ui/", trap::Cvar_Value( "version" ) ) );
}

void UI_Main::M_Menu_Force_f( void )
{
	if( !self )
		return;

	//Com_Printf("UI_Main::M_Menu_Force_F..\n");

	NavigationStack *nav = self->navigations[UI_CONTEXT_MAIN].front();
	if( !nav )
		return;

	bool force = atoi( trap::Cmd_Argv( 1 ) ) != 0;
	self->forceUI( force );

	if( !force ) {
		return;
	}

	// if forced, ensure we have at least the default page on stack
	if( !nav->hasDocuments() ) {
		nav->pushDocument( self->ui_index );
	}
	self->showUI( true );
}

void UI_Main::M_Menu_Open_Cmd_f_( bool modal )
{
	int i;

	if( !self )
		return;
	if( trap::Cmd_Argc() < 2 )
		return;

	Rocket::Core::URL url;

	url.SetFileName( trap::Cmd_Argv( 1 ) );
	url.SetExtension( "rml" );

	for( i = 2; i < trap::Cmd_Argc() - 1; i += 2 ) {
		url.SetParameter( trap::Cmd_Argv( i ), trap::Cmd_Argv( i+1 ) );
	}

	Rocket::Core::String urlString = url.GetURL();
	//Com_Printf( "UI_Main::M_Menu_Open_f %s\n", urlString.CString() );

	NavigationStack *nav = self->navigations[UI_CONTEXT_MAIN].front();
	if( !nav )
		return;

	nav->pushDocument( urlString.CString(), modal );
	self->showUI( true );
}

void UI_Main::M_Menu_Open_f( void )
{
	M_Menu_Open_Cmd_f_( false );
}

void UI_Main::M_Menu_Modal_f( void )
{
	M_Menu_Open_Cmd_f_( true );
}

void UI_Main::M_Menu_Quick_f( void )
{
	int i;

	if( !self )
		return;

	if( !( trap::IN_SupportedDevices() & ( IN_DEVICE_KEYBOARD|IN_DEVICE_TOUCHSCREEN ) ) )
		return;

	NavigationStack *nav = self->navigations[UI_CONTEXT_QUICK].front();
	if( !nav )
		return;

	if( trap::Cmd_Argc() <= 2 ) {
		self->quickMenuURL = "";
		nav->popAllDocuments();
		return;
	}

	Rocket::Core::URL url;

	url.SetFileName( trap::Cmd_Argv( 1 ) );
	url.SetExtension( "rml" );

	for( i = 2; i < trap::Cmd_Argc() - 1; i += 2 ) {
		url.SetParameter( trap::Cmd_Argv( i ), trap::Cmd_Argv( i+1 ) );
	}

	Rocket::Core::String urlString = url.GetURL();
	if( urlString == self->quickMenuURL )
		return;

	if( nav->hasDocuments() ) {
		nav->popAllDocuments();
	}

	nav->pushDocument( urlString.CString(), false );

	self->quickMenuURL = urlString;
}

void UI_Main::M_Menu_Close_f( void )
{
	if( !self )
		return;
	self->showUI( false );
}


void UI_Main::M_Menu_AddTVChannel_f( void )
{
	int id;

	if( !self || !self->tvchannels ) {
		return;
	}
	if( trap::Cmd_Argc() < 5 ) {
		return;
	}

	id = atoi( trap::Cmd_Argv( 1 ) );
	if( id <= 0 ) {
		return;
	}

	TVChannel chan;
	chan.name = trap::Cmd_Argv( 2 );
	chan.realname = trap::Cmd_Argv( 3 );
	chan.address = trap::Cmd_Argv( 4 );
	chan.numPlayers = atoi( trap::Cmd_Argv( 5 ) );
	chan.numSpecs = atoi( trap::Cmd_Argv( 6 ) );
	chan.gametype = trap::Cmd_Argv( 7 );
	chan.mapname = trap::Cmd_Argv( 8 );
	chan.matchname = trap::Cmd_Argv( 9 );
	if( chan.name.empty() ) {
		return;
	}

	self->tvchannels->AddChannel( id, chan );
}

void UI_Main::M_Menu_RemoveTVChannel_f( void )
{
	int id;

	if( !self || !self->tvchannels ) {
		return;
	}
	if( trap::Cmd_Argc() != 2 ) {
		return;
	}

	id = atoi( trap::Cmd_Argv( 1 ) );
	if( id <= 0 ) {
		return;
	}

	self->tvchannels->RemoveChannel( id );
}

// DEBUG
void UI_Main::PrintDocuments_Cmd( void )
{
	int i;

	if( !self )
		return;

	for( i = 0; i < UI_NUM_CONTEXTS; i++ ) {
		UI_Navigation &navigation = self->navigations[i];

		Com_Printf("Context %i navigation stack:\n", i);
		for( UI_Navigation::iterator it = navigation.begin(); it != navigation.end(); ++it ) {
			NavigationStack *nav = *it;

			nav->printStack();

			DocumentCache *cache = nav->getCache();
			if( cache ) {
				Com_Printf("Document cache:\n");
				cache->printCache();
			}

			Com_Printf("\n");
		}
	}
}

}
