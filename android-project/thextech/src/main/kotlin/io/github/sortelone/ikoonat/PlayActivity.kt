package io.github.sortelone.ikoonat

import android.app.Activity
import android.content.Intent
import android.os.Bundle

/**
 * Starts an episode straight away, for home-screen shortcuts and frontends
 * (ES-DE, Daijisho, ...). The episode comes as the "episode" extra (a world
 * file or episode folder path) or as a file:// data URI; "slot" (1-3)
 * picks the save slot, otherwise the episode's Continue slot. When the
 * player leaves the episode, Android returns to the shortcut's home screen
 * or the frontend. Without the asset pack or storage access it opens the
 * library instead.
 */
class PlayActivity : Activity()
{
    override fun onCreate(savedInstanceState: Bundle?)
    {
        super.onCreate(savedInstanceState)

        val path = intent.getStringExtra(GameLauncher.EXTRA_EPISODE)
            ?: intent.data?.takeIf { it.scheme == "file" }?.path

        val episode = if(path != null && GameLauncher.canStart(this)) GameLauncher.findEpisode(path) else null

        if(episode == null)
        {
            startActivity(Intent(this, LibraryActivity::class.java))
        }
        else if(GameLauncher.isGameRunning(this))
        {
            // an episode is still running (left with Home): back to it
            GameLauncher.resume(this)
        }
        else
        {
            val slotExtra = intent.extras?.get(GameLauncher.EXTRA_SLOT)
            val slot = when(slotExtra)
            {
                is Int -> slotExtra
                is String -> slotExtra.toIntOrNull() ?: 0
                else -> 0
            }.let { if(it in 1..Episodes.SLOTS) it else episode.continueSlot }
            GameLauncher.play(this, episode, slot)
        }
        finish()
    }
}
