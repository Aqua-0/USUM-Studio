#pragma once
#include "core/binary.h"
namespace studio {
void validate_pokemon_shadow(View main, View shadow);
Bytes update_pokemon_shadow(View original, View replacement, bool editing_shadow);
}
