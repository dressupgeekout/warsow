#include "ui_precompiled.h"
#include "kernel/ui_common.h"
#include "widgets/ui_levelshot.h"
#include "widgets/ui_widgets.h"

namespace WSWUI
{
	struct shader_s *LevelShot::fallbackShader = NULL;

	LevelShot::LevelShot(const Rocket::Core::String& tag) : ElementImage(tag), srcProcessed(false)
	{
	} 

	void LevelShot::OnAttributeChange(const Rocket::Core::AttributeNameList& anl)
	{
		ElementImage::OnAttributeChange( anl );

		if(anl.find("src") != anl.end())
		{
			if(!srcProcessed)
			{
				Rocket::Core::String fullPath = getImagePath(GetAttribute<Rocket::Core::String>("src", ""));

				if( !fullPath.Empty() ) {
					// precache fallback shader
					if( !fallbackShader ) {
						fallbackShader = trap::R_RegisterPic( PATH_UKNOWN_MAP_PIC );

						// let the global shader cache know about the fallback shader
						UI_RenderInterface *renderer = dynamic_cast<UI_RenderInterface *>(GetRenderInterface());
						if( renderer ) {
							renderer->AddShaderToCache( PATH_UKNOWN_MAP_PIC );
						}
					}

					srcProcessed = true;
					SetAttribute( "src", fullPath );

					// precache the levelshot shader here, so that
					// the subsequent trap::R_RegisterPic call in UI_RenderInterface::LoadTexture 
					// will return proper shader (with fallback image, etc)
					trap::R_RegisterLevelshot( fullPath.CString(), fallbackShader, NULL );
					return;
				}
			}
			else
			{
				srcProcessed = false;
			}
		}
	}

	Rocket::Core::String LevelShot::getImagePath(const Rocket::Core::String& mapname)
	{
		if (mapname.Empty()) {
			return "";
		}
		return "/levelshots/" + mapname + ".jpg";
	}

	Rocket::Core::ElementInstancer *GetLevelShotInstancer(void)
	{
		return __new__( GenericElementInstancer<LevelShot> )(); 
	}
}
