#pragma once

class CTexture;
// File metadata cached until this texture is unloaded; zero when it is not an atlas.
float reticle_atlas_glow_size(CTexture* texture);
