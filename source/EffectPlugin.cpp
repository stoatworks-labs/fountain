#include "Fountain.h"

/**
    The effect: the clip projected onto the fountain's water from the camera's
    own position, so it lands undistorted on any water and shows only where
    there is water -- on the mist screen, the jets and the beads.

    See SourcePlugin.cpp for why this file is listed in its own target.
    `SW Fountain Over` is exactly sixteen characters, FFGL's limit.
*/
namespace
{
class FountainEffect : public fountain::FountainPlugin
{
public:
	FountainEffect() :
		FountainPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< FountainEffect >,// Create method
	"FT02",                         // Plugin unique ID of maximum length 4
	"SW Fountain Over",             // Plugin name
	2,                              // API major version number
	1,                              // API minor version number
	0,                              // Plugin major version number
	1,                              // Plugin minor version number
	FF_EFFECT,                      // Plugin type
	"The clip projected onto a musical fountain from the camera's position: it shows only where there is water. The "
	"picture tears where the mist screen thins and drifts with the wind; jets fired by the music carry it up, late by "
	"their flight time unless Lead fires them early.",
	"Fountain FFGL effect"// About
);

extern "C" const char* FountainEffectBuildStamp()
{
	return "fountain " FOUNTAIN_VERSION " effect, built " __DATE__ " " __TIME__;
}
