#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

// The game's part of Luma's two cbuffers, shared with C++ (HLSL does not allow empty structs).
namespace CB
{
   struct LumaGameSettings
   {
      float sun_intensity_boost;
      float hdr_bloom_scale;
      float sdr_hue_restore;
      float sdr_purity_restore;
      float godray_source_peak;
      float hdr_godray_scale;
      float rolloff_anchor;
      float rolloff_compression;
      float tonemap_type;
      float cinfinity_blowout;
      float psycho_dechroma;
      float psycho_highlight_saturation;
   };

   struct LumaGameData
   {
      float dummy;
   };
}

#endif // LUMA_GAME_CB_STRUCTS
