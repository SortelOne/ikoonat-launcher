/*
 * TheXTech - A platform game engine ported from old source code for VB6
 *
 * Copyright (c) 2009-2011 Andrew Spinks, original VB6 code
 * Copyright (c) 2020-2026 Vitaly Novichkov <admin@wohlnet.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#ifndef LOAD_GFX_H
#define LOAD_GFX_H

#include <string>

extern bool gfxLoaderTestMode;
extern bool gfxLoaderThreadingMode;

// Public Sub LoadGFX()
void LoadGFX();
// Public Sub UnloadGFX()
void UnloadGFX(bool reload = false);
// smbx2compat: loads the default block/BGO/NPC/effect/background sprites again
// after g_smbx2Ids changed (an episode in the other mode); drops custom sprites
void ReloadDefaultObjectGFX();
// Public Sub LoadCustomGFX()
void LoadCustomGFX(bool include_world = false, const char* preview_players_from = nullptr);
// Public Sub UnloadCustomGFX()
void UnloadCustomGFX();
// Public Sub LoadCustomGFX2(GFXFilePath As String)
//void LoadCustomGFX2(std::string GFXFilePath);

void UnloadPlayerPreviewGFX();

// now a subfunction of LoadCustomGFX
// Public Sub LoadWorldCustomGFX()
// void LoadWorldCustomGFX();
// Public Sub UnloadWorldCustomGFX()
void UnloadWorldCustomGFX();

// smbx2compat: replace an engine sprite with an image file (SMBX2's
// Graphics.sprites overrides). category: LunaLua's name ("block", "npc",
// "effect", "background", "background2", "yoshib", "yoshit", "mario", ...);
// an empty path restores the sprite. False if the engine has no such sprite.
bool SpriteOverride_Set(const std::string &category, int index, const std::string &path);
// Restores every overridden sprite (level unload)
void SpriteOverride_Reset();
// smbx2compat: overridden sprites now (memory log)
size_t SpriteOverride_Count();

// Private Sub cBlockGFX(A As Integer)
// Private Sub cNPCGFX(A As Integer)
// Private Sub cBackgroundGFX(A As Integer)
// Private Sub cTileGFX(A As Integer)
// Private Sub cSceneGFX(A As Integer)
// Private Sub cLevelGFX(A As Integer)
// Private Sub cPathGFX(A As Integer)

void LoaderInit();
void LoaderFinish();
void LoaderUpdateDebugString(const std::string &strig, bool forceUpdate = false);

// Public Sub UpdateLoad()
void UpdateLoadREAL();
void UpdateLoad();


#endif // LOAD_GFX_H
