#include "Fountain.h"

/**
    The source: a musical fountain at night, its water lit from below by
    coloured lamps.

    Listed directly in the FountainSource target, not in fountain_core: both
    plugins share the class and not the `CFFGLPluginInfo` below, and putting
    either registration in the shared library would register both plugins
    into both bundles. The core is an OBJECT library because this registers
    itself from a file-scope constructor nothing references.

    `SW Fountain` is eleven characters; the FFGL name field is char[ 16 ] and
    not null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class FountainSource : public fountain::FountainPlugin
{
public:
	FountainSource() :
		FountainPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< FountainSource >,// Create method
	"FT01",                         // Plugin unique ID of maximum length 4
	"SW Fountain",                  // Plugin name
	2,                              // API major version number
	1,                              // API minor version number
	0,                              // Plugin major version number
	1,                              // Plugin minor version number
	FF_SOURCE,                      // Plugin type
	"A musical fountain whose valves the music opens. The water flies ballistically and arrives late by its flight "
	"time unless Lead fires it early; louder is higher as v^2/2g; tall jets pinch into beads at Rayleigh's "
	"fastest-growing wavelength near their tops; a mist bar raises a screen that drifts with the wind.",
	"Fountain FFGL source"// About
);

extern "C" const char* FountainSourceBuildStamp()
{
	return "fountain " FOUNTAIN_VERSION " source, built " __DATE__ " " __TIME__;
}
