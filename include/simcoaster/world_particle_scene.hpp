#pragma once
#include "mesh_scene.hpp"
#include "particle_runtime.hpp"
#include "sprite_pixels.hpp"
#include <map>

namespace simcoaster {
struct WorldRuntime;

//Render the existing simulator's particles. No emission, lifetime or authored
//attachment state is synthesized by this owner.
class WorldParticleScene {
    SpritePixelCatalog pixels_;
    std::map<std::pair<std::size_t, std::size_t>, TextureImagePtr> textures_;
    void append_effects(WorldRuntime&,
                        const ParticleRuntime&,
                        const SceneCamera*,
                        float aspect,
                        EffectScene&,
                        bool screen_space);

public:
    //World effects participate in the World depth buffer.
    void append(WorldRuntime&, const ParticleRuntime&, const SceneCamera&, float aspect, MeshScene&);
    //Native SCREENAFTERUI pass. Does not require or rebuild the World scene.
    void append_screen(WorldRuntime&, const ParticleRuntime&, float aspect, EffectScene&);
};
} //namespace simcoaster
